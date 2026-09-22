#include "88lib/c_interface.h"

#include "88lib/deviceModel.h"
#include "88lib/hardwareDevice.h"
#include "88lib/rom/romloader.h"

#include "baseLib/filesystem.h"

#include "synthLib/audioTypes.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiTypes.h"
#include "synthLib/resampler.h"
#include "synthLib/romLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef EMU88_VERSION_STRING
#	define EMU88_VERSION_STRING "unknown"
#endif

struct emu88_data
{
	// The widest board tells four inputs apart (the SC-8850's USB cables).
	static constexpr size_t MaxPorts = 4;

	emu88Lib::DeviceModel model = emu88Lib::DeviceModel::Sc88Pro;
	unsigned bootFlags = EMU88_BOOT_DEFAULT;
	double outputSamplerate = 0.0;	// 0: the device's own rate

	std::unique_ptr<emu88Lib::HardwareDevice> device;
	unsigned portCount = 1;
	// The card the next device boots with, see emu88_set_pcm_card().
	std::vector<uint8_t> pcmCard;

	std::array<synthLib::MidiBufferParser, MaxPorts> parsers{
		synthLib::MidiBufferParser{synthLib::MidiEventSource::Host}, synthLib::MidiBufferParser{synthLib::MidiEventSource::Host},
		synthLib::MidiBufferParser{synthLib::MidiEventSource::Host}, synthLib::MidiBufferParser{synthLib::MidiEventSource::Host}};
	std::vector<synthLib::SMidiEvent> parsed;
	// Played since the last frame was rendered; goes to the device with the next one.
	std::vector<synthLib::SMidiEvent> midiIn;
	std::vector<synthLib::SMidiEvent> midiOut;

	std::unique_ptr<synthLib::Resampler> resampler;
	std::vector<float> left, right;

	float deviceSamplerate() const { return device ? device->getSamplerate() : 0.0f; }

	float actualSamplerate() const
	{
		if(!device)
			return 0.0f;
		return outputSamplerate > 0.0 ? static_cast<float>(outputSamplerate) : deviceSamplerate();
	}

	// Frames at the device's rate. MIDI waiting in midiIn goes in with the first call.
	void renderDevice(const synthLib::TAudioOutputs& _outputs, const size_t _frames)
	{
		const synthLib::TAudioInputs inputs{};
		device->process(inputs, _outputs, _frames, midiIn, midiOut);
		midiIn.clear();
	}

	void updateResampler()
	{
		const auto in = deviceSamplerate();
		const auto out = actualSamplerate();
		if(!device || std::fabs(in - out) < 0.5f)
		{
			resampler.reset();
			return;
		}
		if(resampler && resampler->getSamplerateIn() == in && resampler->getSamplerateOut() == out)
			return;
		resampler = std::make_unique<synthLib::Resampler>(in, out, synthLib::Resampler::Mode::MameHq);
	}

	// Non-interleaved frames at the output rate into left/right.
	void render(const uint32_t _frames)
	{
		left.resize(_frames);
		right.resize(_frames);

		synthLib::TAudioOutputs outputs{};
		outputs[0] = left.data();
		outputs[1] = right.data();

		if(!resampler)
		{
			renderDevice(outputs, _frames);
			return;
		}
		resampler->process(outputs, 2, _frames, false, [this](synthLib::TAudioOutputs& _out, const uint32_t _count)
		{
			renderDevice(_out, _count);
		});
	}
};

namespace
{
	bool toModel(const emu88_device_id _device, emu88Lib::DeviceModel& _model)
	{
		if(_device < 0 || !emu88Lib::isDeviceModelValue(static_cast<uint32_t>(_device)))
			return false;
		_model = static_cast<emu88Lib::DeviceModel>(_device);
		return true;
	}

	emu88_return_code checkPath(const char* _path)
	{
		if(!_path || !*_path)
			return EMU88_RC_INVALID_ARGUMENT;
		return baseLib::filesystem::isDirectory(_path) ? EMU88_RC_OK : EMU88_RC_PATH_NOT_FOUND;
	}

	emu88_return_code play(const emu88_context _context, const unsigned _port, const uint8_t* _data, const size_t _length)
	{
		if(!_context || (!_data && _length))
			return EMU88_RC_INVALID_ARGUMENT;
		if(!_context->device)
			return EMU88_RC_NOT_OPENED;

		const auto port = _port % _context->portCount;
		auto& parser = _context->parsers[port];
		for(size_t i = 0; i < _length; ++i)
			parser.write(_data[i]);

		_context->parsed.clear();
		parser.getEvents(_context->parsed);
		for(auto& event : _context->parsed)
		{
			event.port = static_cast<uint8_t>(port);
			event.offset = 0;
			_context->midiIn.push_back(std::move(event));
		}
		return EMU88_RC_OK;
	}
}

extern "C" {

const char* emu88_get_library_version_string(void)
{
	return EMU88_VERSION_STRING;
}

// ---- devices and ROMs

emu88_return_code emu88_add_rom_path(const char* _path)
{
	if(const auto rc = checkPath(_path); rc != EMU88_RC_OK)
		return rc;
	synthLib::RomLoader::addSearchPath(_path, true);
	emu88Lib::RomLoader::rescan();
	return EMU88_RC_OK;
}

emu88_return_code emu88_set_rom_path(const char* _path)
{
	if(const auto rc = checkPath(_path); rc != EMU88_RC_OK)
		return rc;
	synthLib::RomLoader::setSearchPath(_path);
	emu88Lib::RomLoader::rescan();
	return EMU88_RC_OK;
}

void emu88_rescan_roms(void)
{
	emu88Lib::RomLoader::rescan();
}

int emu88_get_device_count(void)
{
	return static_cast<int>(emu88Lib::g_deviceMenuOrder.size());
}

emu88_device_id emu88_get_device_id(const int _index)
{
	if(_index < 0 || static_cast<size_t>(_index) >= emu88Lib::g_deviceMenuOrder.size())
		return -1;
	return static_cast<emu88_device_id>(emu88Lib::g_deviceMenuOrder[static_cast<size_t>(_index)]);
}

const char* emu88_get_device_name(const emu88_device_id _device)
{
	emu88Lib::DeviceModel model;
	return toModel(_device, model) ? emu88Lib::getDeviceProfile(model).displayName : nullptr;
}

int emu88_get_device_midi_port_count(const emu88_device_id _device)
{
	emu88Lib::DeviceModel model;
	if(!toModel(_device, model))
		return 0;
	return std::clamp<int>(emu88Lib::getDeviceProfile(model).groupCount, 1, emu88_data::MaxPorts);
}

int emu88_is_device_available(const emu88_device_id _device)
{
	emu88Lib::DeviceModel model;
	return toModel(_device, model) && emu88Lib::RomLoader::isDeviceAvailable(model) ? 1 : 0;
}

int emu88_get_device_first_midi_channel(const emu88_device_id _device)
{
	emu88Lib::DeviceModel model;
	return toModel(_device, model) ? emu88Lib::firstMidiChannel(model) : 0;
}

int emu88_device_has_pcm_card_slot(const emu88_device_id _device)
{
	emu88Lib::DeviceModel model;
	return toModel(_device, model) && emu88Lib::hasPcmCardSlot(model) ? 1 : 0;
}

size_t emu88_describe_device_roms(const emu88_device_id _device, char* _buffer, const size_t _bufferSize)
{
	std::string text;
	emu88Lib::DeviceModel model;
	if(toModel(_device, model))
		text = emu88Lib::RomLoader::scan().describeRequirements(emu88Lib::RomLoader::toRomDevice(model));

	if(_buffer && _bufferSize)
	{
		const auto count = std::min(text.size(), _bufferSize - 1);
		std::memcpy(_buffer, text.data(), count);
		_buffer[count] = 0;
	}
	return text.size();
}

// ---- context

emu88_context emu88_create_context(void)
{
	return new emu88_data();
}

void emu88_free_context(const emu88_context _context)
{
	delete _context;
}

emu88_return_code emu88_select_device(const emu88_context _context, const emu88_device_id _device)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	emu88Lib::DeviceModel model;
	if(!toModel(_device, model))
		return EMU88_RC_UNKNOWN_DEVICE;
	_context->model = model;
	return EMU88_RC_OK;
}

void emu88_set_boot_flags(const emu88_context _context, const unsigned _flags)
{
	if(_context)
		_context->bootFlags = _flags;
}

emu88_return_code emu88_set_pcm_card(const emu88_context _context, const uint8_t* _image, const size_t _length)
{
	if(!_context || (!_image && _length))
		return EMU88_RC_INVALID_ARGUMENT;
	std::vector<uint8_t> card;
	if(_length)
	{
		card.assign(_image, _image + _length);
		if(!emu88Lib::HardwareDevice::isPcmCardImage(card))
			return EMU88_RC_INVALID_ARGUMENT;
	}
	_context->pcmCard = std::move(card);
	return EMU88_RC_OK;
}

void emu88_set_stereo_output_samplerate(const emu88_context _context, const double _samplerate)
{
	if(!_context)
		return;
	_context->outputSamplerate = _samplerate > 0.0 ? _samplerate : 0.0;
	_context->updateResampler();
}

emu88_return_code emu88_open_synth(const emu88_context _context)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	emu88_close_synth(_context);

	if(!emu88Lib::RomLoader::isDeviceAvailable(_context->model))
		return EMU88_RC_MISSING_ROMS;

	synthLib::DeviceCreateParams params;
	params.customData = static_cast<uint32_t>(_context->model);

	emu88Lib::BootOptions boot;
	boot.factoryReset = (_context->bootFlags & EMU88_BOOT_FACTORY_RESET) != 0;
	boot.fastBoot = (_context->bootFlags & EMU88_BOOT_SKIP_INTRO) != 0;

	auto device = std::make_unique<emu88Lib::HardwareDevice>(params, boot, _context->pcmCard);
	if(!device->isValid())
		return EMU88_RC_FAILED;

	_context->device = std::move(device);
	_context->portCount = static_cast<unsigned>(emu88_get_device_midi_port_count(static_cast<emu88_device_id>(_context->model)));
	_context->updateResampler();
	return EMU88_RC_OK;
}

void emu88_close_synth(const emu88_context _context)
{
	if(!_context)
		return;
	_context->resampler.reset();
	_context->device.reset();
	_context->midiIn.clear();
	for(auto& parser : _context->parsers)
		parser = synthLib::MidiBufferParser{synthLib::MidiEventSource::Host};
}

int emu88_is_open(const emu88_context _context)
{
	return _context && _context->device ? 1 : 0;
}

uint32_t emu88_get_actual_stereo_output_samplerate(const emu88_context _context)
{
	return _context ? static_cast<uint32_t>(std::lround(_context->actualSamplerate())) : 0;
}

uint32_t emu88_get_device_samplerate(const emu88_context _context)
{
	return _context ? static_cast<uint32_t>(std::lround(_context->deviceSamplerate())) : 0;
}

int emu88_get_midi_port_count(const emu88_context _context)
{
	return _context && _context->device ? static_cast<int>(_context->portCount) : 0;
}

// ---- MIDI

emu88_return_code emu88_play_msg(const emu88_context _context, const uint32_t _msg)
{
	return emu88_play_msg_on_port(_context, 0, _msg);
}

emu88_return_code emu88_play_msg_on_port(const emu88_context _context, const unsigned _port, const uint32_t _msg)
{
	const uint8_t bytes[3] = {static_cast<uint8_t>(_msg), static_cast<uint8_t>(_msg >> 8), static_cast<uint8_t>(_msg >> 16)};
	if(bytes[0] < 0x80 || bytes[0] == 0xf0 || bytes[0] == 0xf7)
		return EMU88_RC_INVALID_ARGUMENT;
	// Program change, channel pressure, MTC quarter frame and song select carry one data byte;
	// tune request and the realtime messages none.
	const auto status = bytes[0] < 0xf0 ? bytes[0] & 0xf0 : bytes[0];
	const size_t length = (status == 0xc0 || status == 0xd0 || status == 0xf1 || status == 0xf3) ? 2 : status > 0xf3 ? 1 : 3;
	return play(_context, _port, bytes, length);
}

emu88_return_code emu88_play_sysex(const emu88_context _context, const uint8_t* _sysex, const uint32_t _length)
{
	return emu88_play_sysex_on_port(_context, 0, _sysex, _length);
}

emu88_return_code emu88_play_sysex_on_port(const emu88_context _context, const unsigned _port, const uint8_t* _sysex, const uint32_t _length)
{
	if(!_sysex || _length < 2 || _sysex[0] != 0xf0 || _sysex[_length - 1] != 0xf7)
		return EMU88_RC_INVALID_ARGUMENT;
	return play(_context, _port, _sysex, _length);
}

emu88_return_code emu88_parse_stream(const emu88_context _context, const uint8_t* _stream, const uint32_t _length)
{
	return play(_context, 0, _stream, _length);
}

emu88_return_code emu88_parse_stream_on_port(const emu88_context _context, const unsigned _port, const uint8_t* _stream, const uint32_t _length)
{
	return play(_context, _port, _stream, _length);
}

emu88_return_code emu88_play_device_reset(const emu88_context _context)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	if(!_context->device)
		return EMU88_RC_NOT_OPENED;
	const auto reset = emu88Lib::deviceResetSysex(_context->model);
	for(unsigned port = 0; port < _context->portCount; ++port)
		if(const auto rc = play(_context, port, reset.data(), reset.size()); rc != EMU88_RC_OK)
			return rc;
	return EMU88_RC_OK;
}

emu88_return_code emu88_play_silence(const emu88_context _context)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	if(!_context->device)
		return EMU88_RC_NOT_OPENED;
	const auto controllers = emu88Lib::HardwareDevice::silenceControllers(_context->model);
	for(unsigned port = 0; port < _context->portCount; ++port)
		for(uint8_t channel = 0; channel < 16; ++channel)
			for(const auto& [controller, value] : controllers)
			{
				const uint8_t bytes[3] = {static_cast<uint8_t>(0xb0 | channel), controller, value};
				if(const auto rc = play(_context, port, bytes, 3); rc != EMU88_RC_OK)
					return rc;
			}
	return EMU88_RC_OK;
}

// ---- front panel

emu88_return_code emu88_set_panel_buttons(const emu88_context _context, const uint32_t _buttons)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	if(!_context->device)
		return EMU88_RC_NOT_OPENED;
	_context->device->setPanelButtons(_buttons);
	return EMU88_RC_OK;
}

emu88_return_code emu88_turn_panel_encoder(const emu88_context _context, const int _detents)
{
	if(!_context)
		return EMU88_RC_INVALID_ARGUMENT;
	if(!_context->device)
		return EMU88_RC_NOT_OPENED;
	_context->device->turnPanelEncoder(_detents);
	return EMU88_RC_OK;
}

uint32_t emu88_get_panel_leds(const emu88_context _context)
{
	if(!_context || !_context->device)
		return 0;
	return _context->device->displaySnapshot().leds;
}

size_t emu88_get_display_text(const emu88_context _context, const unsigned _screen, char* _buffer, const size_t _bufferSize)
{
	std::string text;
	if(_context && _context->device && _screen < 2)
	{
		const auto snapshot = _context->device->displaySnapshot();
		for(const auto& line : snapshot.screens[_screen].text)
		{
			if(!text.empty())
				text += '\n';
			text += line;
		}
	}
	if(_buffer && _bufferSize)
	{
		const auto count = std::min(text.size(), _bufferSize - 1);
		std::memcpy(_buffer, text.data(), count);
		_buffer[count] = 0;
	}
	return text.size();
}

int emu88_is_display_on(const emu88_context _context, const unsigned _screen)
{
	if(!_context || !_context->device || _screen >= 2)
		return 0;
	return _context->device->displaySnapshot().screens[_screen].displayOn ? 1 : 0;
}

// ---- audio

void emu88_render_float(const emu88_context _context, float* _stream, const uint32_t _length)
{
	if(!_stream || !_length)
		return;
	if(!_context || !_context->device)
	{
		std::fill_n(_stream, static_cast<size_t>(_length) * 2, 0.0f);
		return;
	}
	_context->render(_length);
	for(uint32_t i = 0; i < _length; ++i)
	{
		_stream[i * 2] = _context->left[i];
		_stream[i * 2 + 1] = _context->right[i];
	}
}

void emu88_render_bit16s(const emu88_context _context, int16_t* _stream, const uint32_t _length)
{
	if(!_stream || !_length)
		return;
	if(!_context || !_context->device)
	{
		std::fill_n(_stream, static_cast<size_t>(_length) * 2, static_cast<int16_t>(0));
		return;
	}
	_context->render(_length);
	const auto convert = [](const float _sample)
	{
		return static_cast<int16_t>(std::lround(std::clamp(_sample, -1.0f, 1.0f) * 32767.0f));
	};
	for(uint32_t i = 0; i < _length; ++i)
	{
		_stream[i * 2] = convert(_context->left[i]);
		_stream[i * 2 + 1] = convert(_context->right[i]);
	}
}

} // extern "C"
