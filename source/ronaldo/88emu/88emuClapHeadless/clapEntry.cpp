/*
 * 88emuClapHeadless: 88emu as a headless CLAP instrument.
 *
 * Not for music production. There is no editor, no parameter and no saved state; a DAW user wants
 * 88emuPlayer. This exists so that emulators can play their MIDI through 88emu: DOSBox Staging's
 * generic CLAP MIDI device first of all, which offers a plugin no host extension at all (no log,
 * state, params, GUI, timer or main thread callback). So everything here works without one: the
 * device is chosen by picking its plugin from the factory, and the ROMs are found on the search
 * paths that EMU88_ROM_PATH and SOUNDCANVAS_ROM_PATH add to the defaults.
 *
 * Written against c_interface.h only. One plugin per device, in presentation order; a device
 * whose ROMs are missing is still listed, so plugin indices stay put, and fails to init.
 * The firmware boots in activate(), on the main thread, and takes MIDI from the first process().
 */

#include "88lib/c_interface.h"

#include "clap/clap.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace emu88ClapHeadless
{
	namespace
	{
#ifdef _WIN32
		constexpr char g_pathListSeparator = ';';
#else
		constexpr char g_pathListSeparator = ':';
#endif

		// Variables that name ROM folders, as path lists. SOUNDCANVAS_ROM_PATH is what DOSBox Staging
		// sets from its 'soundcanvas_rom_dir'.
		constexpr const char* g_romPathVariables[] = {"EMU88_ROM_PATH", "SOUNDCANVAS_ROM_PATH"};

		const char* const g_features[] = {
			CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

		struct Descriptor
		{
			emu88_device_id device = 0;
			std::string id;
			std::string name;
			clap_plugin_descriptor_t clap{};
		};

		std::vector<Descriptor> g_descriptors;

		// "CM-300 / SCC-1" -> "cm300scc1". A note in parentheses is not part of it, so that
		// "MT-32 1.x (experimental)" keeps its id when it stops being experimental.
		std::string toIdSuffix(const char* _name)
		{
			std::string suffix;
			for(const char* c = _name; *c && *c != '('; ++c)
			{
				if(std::isalnum(static_cast<unsigned char>(*c)))
					suffix += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
			}
			return suffix;
		}

		void createDescriptors()
		{
			const auto count = emu88_get_device_count();
			g_descriptors.resize(static_cast<size_t>(std::max(count, 0)));

			for(int i = 0; i < count; ++i)
			{
				auto& d = g_descriptors[static_cast<size_t>(i)];
				d.device = emu88_get_device_id(i);
				const char* name = emu88_get_device_name(d.device);
				d.name = name ? name : "unknown";
				d.id = "com.theusualsuspects.88emuclapheadless." + toIdSuffix(d.name.c_str());
			}

			// The strings do not move any more
			for(auto& d : g_descriptors)
			{
				auto& c = d.clap;
				c.clap_version = CLAP_VERSION_INIT;
				c.id = d.id.c_str();
				c.name = d.name.c_str();
				c.vendor = "The Usual Suspects";
				c.url = "https://dsp56300.wordpress.com";
				c.manual_url = "";
				c.support_url = "";
				c.version = emu88_get_library_version_string();
				c.description = "88emu headless, for DOSBox and other emulators";
				c.features = g_features;
			}
		}

		// Adds the folders named by the environment to the ROM search paths. Each folder only once per
		// process, as every addition sweeps the paths again.
		void addRomPathsFromEnvironment()
		{
			static std::mutex mutex;
			static std::set<std::string> added;

			std::lock_guard lock(mutex);

			for(const auto* variable : g_romPathVariables)
			{
				const char* value = std::getenv(variable);
				if(!value)
					continue;

				const std::string list = value;
				size_t start = 0;
				while(start <= list.size())
				{
					auto end = list.find(g_pathListSeparator, start);
					if(end == std::string::npos)
						end = list.size();

					const auto path = list.substr(start, end - start);
					if(!path.empty() && added.insert(path).second)
						emu88_add_rom_path(path.c_str());

					start = end + 1;
				}
			}
		}

		class Plugin
		{
		public:
			Plugin(const clap_host_t* _host, const Descriptor& _descriptor) : m_host(_host), m_descriptor(_descriptor)
			{
				m_plugin.desc = &_descriptor.clap;
				m_plugin.plugin_data = this;
				m_plugin.init = [](const clap_plugin_t* _p) { return self(_p)->init(); };
				m_plugin.destroy = [](const clap_plugin_t* _p) { delete self(_p); };
				m_plugin.activate = [](const clap_plugin_t* _p, const double _samplerate, uint32_t, const uint32_t _maxFrames)
				{
					return self(_p)->activate(_samplerate, _maxFrames);
				};
				m_plugin.deactivate = [](const clap_plugin_t* _p) { self(_p)->deactivate(); };
				m_plugin.start_processing = [](const clap_plugin_t*) { return true; };
				m_plugin.stop_processing = [](const clap_plugin_t*) {};
				m_plugin.reset = [](const clap_plugin_t* _p) { self(_p)->reset(); };
				m_plugin.process = [](const clap_plugin_t* _p, const clap_process_t* _process)
				{
					return self(_p)->process(*_process);
				};
				m_plugin.get_extension = [](const clap_plugin_t* _p, const char* _id) { return self(_p)->getExtension(_id); };
				m_plugin.on_main_thread = [](const clap_plugin_t*) {};

				m_audioPorts.count = [](const clap_plugin_t*, const bool _isInput) -> uint32_t { return _isInput ? 0 : 1; };
				m_audioPorts.get = [](const clap_plugin_t*, const uint32_t _index, const bool _isInput, clap_audio_port_info_t* _info)
				{
					if(_isInput || _index != 0)
						return false;
					*_info = {};
					_info->id = 0;
					std::snprintf(_info->name, sizeof(_info->name), "%s", "Output");
					_info->flags = CLAP_AUDIO_PORT_IS_MAIN;
					_info->channel_count = 2;
					_info->port_type = CLAP_PORT_STEREO;
					_info->in_place_pair = CLAP_INVALID_ID;
					return true;
				};

				m_notePorts.count = [](const clap_plugin_t* _p, const bool _isInput) -> uint32_t
				{
					return _isInput ? self(_p)->midiPortCount() : 0;
				};
				m_notePorts.get = [](const clap_plugin_t* _p, const uint32_t _index, const bool _isInput, clap_note_port_info_t* _info)
				{
					if(!_isInput || _index >= self(_p)->midiPortCount())
						return false;
					*_info = {};
					_info->id = _index;
					_info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
					_info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
					std::snprintf(_info->name, sizeof(_info->name), "MIDI %c", static_cast<char>('A' + _index));
					return true;
				};
			}

			~Plugin()
			{
				deactivate();
			}

			Plugin(const Plugin&) = delete;
			Plugin& operator=(const Plugin&) = delete;

			const clap_plugin_t* clapPlugin() const { return &m_plugin; }

		private:
			static Plugin* self(const clap_plugin_t* _p) { return static_cast<Plugin*>(_p->plugin_data); }

			uint32_t midiPortCount() const
			{
				return static_cast<uint32_t>(std::max(emu88_get_device_midi_port_count(m_descriptor.device), 1));
			}

			void log(const clap_log_severity _severity, const char* _format, ...) const
			{
				char text[4096];
				va_list args;
				va_start(args, _format);
				std::vsnprintf(text, sizeof(text), _format, args);
				va_end(args);

				if(m_hostLog && m_hostLog->log)
					m_hostLog->log(m_host, _severity, text);
				else
					std::fprintf(stderr, "88emuClapHeadless: %s\n", text);
			}

			bool init()
			{
				if(m_host && m_host->get_extension)
					m_hostLog = static_cast<const clap_host_log_t*>(m_host->get_extension(m_host, CLAP_EXT_LOG));

				addRomPathsFromEnvironment();

				if(emu88_is_device_available(m_descriptor.device))
					return true;

				const auto length = emu88_describe_device_roms(m_descriptor.device, nullptr, 0);
				std::string roms(length + 1, '\0');
				emu88_describe_device_roms(m_descriptor.device, roms.data(), roms.size());
				roms.resize(length);

				log(CLAP_LOG_ERROR, "%s: ROMs missing. Set EMU88_ROM_PATH to the folder that holds them.\n%s",
					m_descriptor.name.c_str(), roms.c_str());
				return false;
			}

			bool activate(const double _samplerate, const uint32_t _maxFrames)
			{
				deactivate();

				m_context = emu88_create_context();
				if(!m_context)
					return false;

				emu88_select_device(m_context, m_descriptor.device);
				emu88_set_stereo_output_samplerate(m_context, _samplerate);

				const auto rc = emu88_open_synth(m_context);
				if(rc != EMU88_RC_OK)
				{
					log(CLAP_LOG_ERROR, "%s: failed to start (%d)", m_descriptor.name.c_str(), static_cast<int>(rc));
					deactivate();
					return false;
				}

				m_interleaved.assign(static_cast<size_t>(std::max<uint32_t>(_maxFrames, 1)) * 2, 0.0f);

				log(CLAP_LOG_INFO, "%s started, %u Hz device rate, rendering at %u Hz", m_descriptor.name.c_str(),
					emu88_get_device_samplerate(m_context), emu88_get_actual_stereo_output_samplerate(m_context));
				return true;
			}

			void deactivate()
			{
				if(!m_context)
					return;
				emu88_free_context(m_context);
				m_context = nullptr;
			}

			void reset() const
			{
				if(m_context)
					emu88_play_silence(m_context);
			}

			// Renders frames [_begin, _end) of the block
			void render(const clap_process_t& _process, const uint32_t _begin, const uint32_t _end)
			{
				if(_end <= _begin)
					return;

				const auto& out = _process.audio_outputs[0];
				float* left = out.data32[0];
				float* right = out.data32[1];

				const auto maxChunk = static_cast<uint32_t>(m_interleaved.size() / 2);

				for(auto pos = _begin; pos < _end;)
				{
					const auto count = std::min(_end - pos, maxChunk);
					emu88_render_float(m_context, m_interleaved.data(), count);

					for(uint32_t i = 0; i < count; ++i)
					{
						left[pos + i] = m_interleaved[i * 2];
						right[pos + i] = m_interleaved[i * 2 + 1];
					}
					pos += count;
				}
			}

			void playEvent(const clap_event_header_t& _header) const
			{
				if(_header.space_id != CLAP_CORE_EVENT_SPACE_ID)
					return;

				switch(_header.type)
				{
				case CLAP_EVENT_MIDI:
					{
						const auto& e = reinterpret_cast<const clap_event_midi_t&>(_header);
						const uint32_t msg = e.data[0] | (e.data[1] << 8) | (e.data[2] << 16);
						emu88_play_msg_on_port(m_context, e.port_index, msg);
					}
					break;
				case CLAP_EVENT_MIDI_SYSEX:
					{
						const auto& e = reinterpret_cast<const clap_event_midi_sysex_t&>(_header);
						if(e.buffer && e.size)
							emu88_play_sysex_on_port(m_context, e.port_index, e.buffer, e.size);
					}
					break;
				default:
					break;
				}
			}

			clap_process_status process(const clap_process_t& _process)
			{
				if(!m_context || _process.audio_outputs_count < 1 || _process.audio_outputs[0].channel_count < 2)
					return CLAP_PROCESS_ERROR;

				_process.audio_outputs[0].constant_mask = 0;

				const auto frames = _process.frames_count;
				const auto* events = _process.in_events;
				const auto eventCount = events ? events->size(events) : 0;

				// Everything played is delivered with the next frame rendered, so render up to each event
				uint32_t pos = 0;
				for(uint32_t i = 0; i < eventCount; ++i)
				{
					const auto* header = events->get(events, i);
					if(!header)
						continue;
					const auto time = std::min(header->time, frames);
					render(_process, pos, time);
					pos = std::max(pos, time);
					playEvent(*header);
				}
				render(_process, pos, frames);

				return CLAP_PROCESS_CONTINUE;
			}

			const void* getExtension(const char* _id) const
			{
				if(!std::strcmp(_id, CLAP_EXT_AUDIO_PORTS))
					return &m_audioPorts;
				if(!std::strcmp(_id, CLAP_EXT_NOTE_PORTS))
					return &m_notePorts;
				return nullptr;
			}

			const clap_host_t* const m_host;
			const Descriptor& m_descriptor;
			const clap_host_log_t* m_hostLog = nullptr;

			clap_plugin_t m_plugin{};
			clap_plugin_audio_ports_t m_audioPorts{};
			clap_plugin_note_ports_t m_notePorts{};

			emu88_context m_context = nullptr;
			std::vector<float> m_interleaved;
		};

		// ---- factory

		uint32_t getPluginCount(const clap_plugin_factory_t*)
		{
			return static_cast<uint32_t>(g_descriptors.size());
		}

		const clap_plugin_descriptor_t* getPluginDescriptor(const clap_plugin_factory_t*, const uint32_t _index)
		{
			return _index < g_descriptors.size() ? &g_descriptors[_index].clap : nullptr;
		}

		const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* _host, const char* _id)
		{
			if(!_id || !clap_version_is_compatible(_host->clap_version))
				return nullptr;

			for(const auto& d : g_descriptors)
			{
				if(d.id == _id)
					return (new Plugin(_host, d))->clapPlugin();
			}
			return nullptr;
		}

		const clap_plugin_factory_t g_factory = {getPluginCount, getPluginDescriptor, createPlugin};

		// ---- entry

		int g_initCount = 0;

		bool entryInit(const char*)
		{
			if(g_initCount++ == 0)
				createDescriptors();
			return true;
		}

		void entryDeinit()
		{
			if(--g_initCount == 0)
				g_descriptors.clear();
		}

		const void* entryGetFactory(const char* _id)
		{
			return !std::strcmp(_id, CLAP_PLUGIN_FACTORY_ID) ? &g_factory : nullptr;
		}
	}
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	CLAP_VERSION_INIT, emu88ClapHeadless::entryInit, emu88ClapHeadless::entryDeinit, emu88ClapHeadless::entryGetFactory};
