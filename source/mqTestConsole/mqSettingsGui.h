#pragma once

#include <array>

#include <cpp-terminal/window.hpp>

#include "mqGuiBase.h"

class SettingsGui : GuiBase
{
public:
	SettingsGui();

	void render(int _midiInput, int _midiOutput, int _audioOutput);

	void onEnter();
	void onDown();
	void onLeft();
	void onRight();
	void onUp();

	const std::string& getMidiInput() const { return m_midiInput; }
	const std::string& getMidiOutput() const { return m_midiOutput; }
	const std::string& getAudioOutput() const { return m_audioOutput; }

private:
	struct Setting
	{
		int devId;
		std::string name;
	};

	void findSettings();
	void changeDevice(const Setting& _value, int column);
	int renderSettings(int x, int y, int _selectedId, int _column, const std::string& _headline);
	void moveCursor(int x, int y);
	Term::Window m_win;

	int m_cursorX = 0;
	int m_cursorY = 0;
	bool m_enter = false;

	std::array<std::vector<Setting>, 3> m_settings;
	std::array<int, 3> m_scrollPos{0};

	std::string m_midiInput;
	std::string m_midiOutput;
	std::string m_audioOutput;
};
