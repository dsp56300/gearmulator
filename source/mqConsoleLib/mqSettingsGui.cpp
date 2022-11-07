#include "mqSettingsGui.h"

#include <iostream>
#include <cpp-terminal/input.hpp>

#include "audioOutputPA.h"
#include "midiDevice.h"
#include "../portmidi/pm_common/portmidi.h"
#include "../portaudio/include/portaudio.h"

#include "dsp56kEmu/logging.h"

using Key = Term::Key;

constexpr auto g_itemActive = Term::fg::bright_yellow;
constexpr auto g_itemDefault = Term::fg::white;
constexpr auto g_itemHovered = Term::bg::gray;
constexpr auto g_itemNotHovered = Term::bg::black;

constexpr int32_t g_maxEntries = 17;

SettingsGui::SettingsGui()
	: m_win(140, 21)
{
}

void SettingsGui::render(int _midiInput, int _midiOutput, int _audioOutput)
{
	if(!handleTerminalSize())
		m_win.clear();

	m_midiInput.clear();
	m_midiOutput.clear();
	m_audioOutput.clear();

	int x = 1;
	int y = 1;

	m_win.fill_bg(1,1, static_cast<int>(m_win.get_w()), 1, Term::bg::gray);
	m_win.fill_fg(1,1, static_cast<int>(m_win.get_w()), 1, Term::fg::black);

	x = renderSettings(x, y, _midiInput, 0, "MIDI Input");	x += 3;
	x = renderSettings(x, y, _midiOutput, 1, "MIDI Output");	x += 3;
	renderSettings(x, y, _audioOutput, 2, "Audio Output");

	m_win.fill_fg(1,21, 90, 21, Term::fg::gray);
	m_win.print_str(1, 21, "Use Cursor Keys to navigate, Return to select a device. Escape to go back.");

	std::cout << m_win.render(1,1,true) << std::flush;
}

void SettingsGui::onOpen()
{
	GuiBase::onOpen();
	findSettings();
}

void SettingsGui::onEnter()
{
	m_enter = true;
}

void SettingsGui::onDown()
{
	moveCursor(0,1);
}

void SettingsGui::onLeft()
{
	moveCursor(-1,0);
}

void SettingsGui::onRight()
{
	moveCursor(1,0);
}

void SettingsGui::onUp()
{
	moveCursor(0,-1);
}

bool SettingsGui::processKey(int _ch)
{
	switch (_ch)
	{
	case Key::ESC:
	case Key::ENTER:
		onEnter();
		return true;
	case Key::ARROW_DOWN:
		onDown();
		return true;
	case Key::ARROW_LEFT:
		onLeft();
		return true;
	case Key::ARROW_RIGHT:
		onRight();
		return true;
	case Key::ARROW_UP:
		onUp();
		return true;
	default:
		return false;
	}
}

void SettingsGui::findSettings()
{
	for (auto& setting : m_settings)
		setting.clear();

	// MIDI
	const auto devCount = Pm_CountDevices();

	for(int i=0; i<devCount; ++i)
	{
		const auto* di = Pm_GetDeviceInfo(i);
		if(!di)
			continue;

		const auto name = MidiDevice::getDeviceNameFromId(i);
		if(di->input)
			m_settings[0].push_back({i, name});
		if(di->output)
			m_settings[1].push_back({i, name});
	}

	const auto count = Pa_GetDeviceCount();

	for(int i=0; i<count; ++i)
	{
		const auto* di = Pa_GetDeviceInfo(i);
		if(!di)
			continue;

		if(di->maxOutputChannels < 2)
			continue;

		PaStreamParameters p{};

		p.channelCount = 2;
		p.sampleFormat = paFloat32;
		p.device = i;
		p.suggestedLatency = di->defaultLowOutputLatency;

		const auto supportResult = Pa_IsFormatSupported(nullptr, &p, 44100.0);
		if(supportResult != paFormatIsSupported)
		{
			std::stringstream ss;

			ss << "Audio Device not supported: " << AudioOutputPA::getDeviceNameFromId(i) << ", result " << Pa_GetErrorText(supportResult);

			const auto* hostError = Pa_GetLastHostErrorInfo();
			if(hostError)
				ss << ", host error info: " << hostError->errorCode << ", msg: " << (hostError->errorText ? hostError->errorText : "null");

			const auto msg(ss.str());
			LOG( msg);
			continue;
		}

		const std::string name = AudioOutputPA::getDeviceNameFromId(i);

		m_settings[2].push_back({i, name});
	}
}

void SettingsGui::changeDevice(const Setting& _value, int column)
{
	switch (column)
	{
	case 0:
		m_midiInput = _value.name;
		break;
	case 1:
		m_midiOutput = _value.name;
		break;
	case 2:
		m_audioOutput = _value.name;
		break;
	default:
		break;
	}
}

int SettingsGui::renderSettings(int x, int y, int _selectedId, const int _column, const std::string& _headline)
{
	m_win.print_str(x, y, _headline);
	y += 2;

	int maxX = x + static_cast<int>(_headline.size());

	const auto& settings = m_settings[_column];

	const bool scroll = settings.size() > g_maxEntries;

	auto& scrollPos = m_scrollPos[_column];

	if(scroll)
	{
		int focusPos = 0;

		if(m_cursorX == _column)
		{
			focusPos = m_cursorY;
		}
		else
		{
			for(size_t i=0; i<settings.size(); ++i)
			{
				if(_selectedId == settings[i].devId)
				{
					focusPos = static_cast<int>(i);
					break;
				}
			}
		}

		while((focusPos - scrollPos) < 3 && scrollPos > 0)
			--scrollPos;
		while((focusPos - scrollPos) > g_maxEntries - 3 && (scrollPos + g_maxEntries) < static_cast<int>(settings.size()))
			++scrollPos;

		if(scrollPos > 0)
		{
			constexpr char32_t arrowUp = 0x2191;
			m_win.set_char(x, y-1, arrowUp);
			m_win.set_char(x + 3, y-1, arrowUp);
			m_win.set_char(x + 6, y-1, arrowUp);
		}

		if((scrollPos + g_maxEntries) < static_cast<int>(settings.size()))
		{
			constexpr char32_t arrowDown = 0x2193;
			m_win.set_char(x, y + g_maxEntries, arrowDown);
			m_win.set_char(x + 3, y + g_maxEntries, arrowDown);
			m_win.set_char(x + 6, y + g_maxEntries, arrowDown);
		}
	}
	else
	{
		scrollPos = 0;
	}
	for(size_t i=0; i<std::min(static_cast<int>(settings.size()), g_maxEntries); ++i)
	{
		const int idx = static_cast<int>(i) + m_scrollPos[_column];

		if(idx < 0)
			continue;

		if(idx >= settings.size())
			break;

		const auto& s = settings[idx];

		const auto len = static_cast<int>(s.name.size());

		if(x + len > maxX)
			maxX = x + len;

		if(m_cursorY == idx && m_cursorX == _column)
		{
			m_win.fill_bg(x,y, x + len - 1, y, g_itemHovered);
			if(m_enter)
			{
				m_enter = false;
				changeDevice(s, _column);
			}
		}
		else
		{
			m_win.fill_bg(x,y, x + len - 1, y, g_itemNotHovered);
		}
		m_win.fill_fg(x,y, x + len, y, s.devId == _selectedId ? g_itemActive : g_itemDefault);
		m_win.print_str(x, y, s.name);
		++y;
	}
	return maxX;
}

void SettingsGui::moveCursor(int x, int y)
{
	m_cursorX += x;
	m_cursorY += y;

	if(m_cursorX >= static_cast<int>(m_settings.size()))
		m_cursorX = 0;
	if(m_cursorX < 0)		
		m_cursorX = static_cast<int>(m_settings.size() - 1);
	if(m_cursorY < 0)
		m_cursorY = m_settings[m_cursorX].empty() ? 0 : static_cast<int>(m_settings[m_cursorX].size()) - 1;
	if(m_cursorY >= static_cast<int>(m_settings[m_cursorX].size()))
		m_cursorY = 0;
}
