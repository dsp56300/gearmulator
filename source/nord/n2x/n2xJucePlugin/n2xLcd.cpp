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

		if(m_overrideText.empty())
			onTextChanged();
	}

	void Lcd::timerCallback()
	{
		stopTimer();

		switch(m_animState)
		{
		case AnimState::Start:
			if(updateClippedText(getCurrentText(), ++m_currentOffset))
			{
				m_animState = AnimState::Scroll;
				startTimer(g_animDelayScroll);
			}
			break;
		case AnimState::Scroll:
			if(!updateClippedText(getCurrentText(), ++m_currentOffset))
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
			updateClippedText(getCurrentText(), m_currentOffset);
			startTimer(g_animDelayStart);
			break;
		}
	}

	void Lcd::updatePatchName()
	{
		onProgramChanged();
	}

	void Lcd::setOverrideText(const std::string& _text)
	{
		std::string t = _text;
		if(!t.empty())
		{
			while(t.size() < 3)
				t = ' ' + t;
		}

		if(t == m_overrideText)
			return;
		m_overrideText = t;
		onTextChanged();
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

	std::string Lcd::substring(const std::string& _text, uint32_t _offset, uint32_t _len)
	{
		auto findIndex = [&_text](const uint32_t _off)
		{
			uint32_t o = _off;

			for(uint32_t i=0; i<_text.size(); ++i)
			{
				if(_text[i] == '.' && i < o)
					++o;
			}
			return o;
		};

		const auto start = findIndex(_offset);
		const auto end = findIndex(_offset + _len);

		return _text.substr(start, end - start);
	}

	bool Lcd::updateClippedText(const std::string& _text, const uint32_t _offset)
	{
		const auto str = substring(_text, _offset, 3);
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
		updateClippedText(getCurrentText(), 0);

		if(m_clippedText != getCurrentText())
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

	const std::string& Lcd::getCurrentText() const
	{
		if(m_overrideText.empty())
			return m_text;
		return m_overrideText;
	}
}
