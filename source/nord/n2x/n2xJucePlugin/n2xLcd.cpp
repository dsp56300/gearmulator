#include "n2xLcd.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	constexpr char space = 0x21;	// unit separator, i.e. full width space

	constexpr uint32_t g_animDelayStart = 1000;
	constexpr uint32_t g_animDelayScroll = 500;
	constexpr uint32_t g_animDelayEnd = 1000;

	Lcd::Lcd(Editor& _editor) : m_editor(_editor)
	{
		m_label = _editor.findComponentT<juce::Label>("PatchName");
		setText("---");

		m_onProgramChanged.set(_editor.getN2xController().onProgramChanged, [this]
		{
			onProgramChanged();
		});

		m_onPartChanged.set(_editor.getN2xController().onCurrentPartChanged, [this](const uint8_t&)
		{
			onProgramChanged();
		});
	}

	void Lcd::setText(const std::string& _text)
	{
		if(m_text == _text)
			return;
		m_text = _text;
		onTextChanged();
	}

	void Lcd::timerCallback()
	{
		stopTimer();

		switch(m_animState)
		{
		case AnimState::Start:
			if(updateClippedText(m_text, ++m_currentOffset))
			{
				m_animState = AnimState::Scroll;
				startTimer(g_animDelayScroll);
			}
			break;
		case AnimState::Scroll:
			if(!updateClippedText(m_text, ++m_currentOffset))
			{
				m_animState = AnimState::End;
				startTimer(g_animDelayEnd);
			}
			else
			{
				startTimer(g_animDelayScroll);
			}
			break;
		case AnimState::End:
			m_animState = AnimState::Start;
			m_currentOffset = 0;
			updateClippedText(m_text, m_currentOffset);
			startTimer(g_animDelayStart);
			break;
		}
	}

	void Lcd::setClippedText(const std::string& _text)
	{
		if(m_clippedText == _text)
			return;
		m_clippedText = _text;

		auto t = _text;

		for (char& c : t)
		{
			if(c == ' ')
				c = space;
		}

		m_label->setText(t, juce::dontSendNotification);
	}

	bool Lcd::updateClippedText(const std::string& _text, const uint32_t _offset)
	{
		const auto str = _text.substr(_offset, 3);
		if(str.size() < 3)
			return false;
		setClippedText(str);
		return true;
	}

	void Lcd::startAnim()
	{
		m_currentOffset = 0;
		m_animState = AnimState::Start;
		startTimer(g_animDelayStart);
	}

	void Lcd::onTextChanged()
	{
		updateClippedText(m_text, 0);

		if(m_clippedText != m_text)
		{
			startAnim();
		}
		else
		{
			stopTimer();
		}
	}

	void Lcd::onProgramChanged()
	{
		setText(m_editor.getCurrentPatchName());
	}
}
