#include "xtArp.h"

#include "xtController.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	// single/instrument/multi => instrument is prefixed with "MIn" where n is the instrument number from 0-7
	namespace
	{
		const char* g_bindArpMode []     = {"ArpMode"     , "ArpActive"   , nullptr};
		const char* g_bindArpClock[]     = {"ArpClock"    , "ArpClock"    , nullptr};
		const char* g_bindArpPattern[]   = {"ArpPattern"  , "ArpPattern"  , nullptr};
		const char* g_bindArpDirection[] = {"ArpDirection", "ArpDirection", nullptr};
		const char* g_bindArpNoteOrder[] = {"ArpNoteOrder", "ArpNoteOrder", nullptr};
		const char* g_bindArpVelocity[]  = {"ArpVelocity" , "ArpVelocity" , nullptr};
		const char* g_bindArpTempo[]     = {"ArpTempo"    , nullptr       , "MArpTempo"};
		const char* g_bindArpRange[]     = {"ArpRange"    , "ArpRange"    , nullptr};
		const char* g_bindArpReset[]     = {"ArpReset"    , "ArpReset"    , nullptr};
	}

	Arp::Arp(Editor& _editor)
	: m_editor(_editor)
		, m_arpMode      (_editor.findComponentByParamT<juce::ComboBox>("ArpMode"))
		, m_arpClock     (_editor.findComponentByParamT<juce::ComboBox>("ArpClock"))
		, m_arpPattern   (_editor.findComponentByParamT<juce::ComboBox>("ArpPattern"))
		, m_arpDirection (_editor.findComponentByParamT<juce::ComboBox>("ArpDirection"))
		, m_arpOrder     (_editor.findComponentByParamT<juce::ComboBox>("ArpNoteOrder"))
		, m_arpVelocity  (_editor.findComponentByParamT<juce::ComboBox>("ArpVelocity"))
		, m_arpTempo     (_editor.findComponentByParamT<juce::Slider>  ("ArpTempo"))
		, m_arpRange     (_editor.findComponentByParamT<juce::Slider>  ("ArpRange"))
	{
		const auto resetButtons = _editor.findComponentsByParam("ArpReset", 2);

		for (auto* resetButton : resetButtons)
		{
			auto* button = dynamic_cast<juce::Button*>(resetButton);
			if(!button)
				continue;
			m_arpReset.push_back(button);
		}

		m_onPartChanged.set(_editor.getXtController().onCurrentPartChanged, [this](const uint8_t& _part)
		{
			bind();
		});

		m_onPlayModeChanged.set(_editor.getXtController().onPlayModeChanged, [this](const bool& _multiMode)
		{
			bind();
		});

		bind();
	}
	
	template <typename T> void Arp::bindT(T* _component, const char** _bindings)
	{
		const auto multi = m_editor.getXtController().isMultiMode();

		std::string paramName;
		if(multi)
		{
			if(_bindings[1])
				paramName = "MI" + std::to_string(m_editor.getXtController().getCurrentPart()) + _bindings[1];
			else
				paramName = _bindings[2];
		}
		else
		{
			paramName = _bindings[0];
		}
		
		auto param = m_editor.getXtController().getParameterIndexByName(paramName);

		m_editor.getParameterBinding().unbind(_component);
		m_editor.getParameterBinding().bind(*_component, param, 0);
	}

	void Arp::bind()
	{
		bindT(m_arpMode, g_bindArpMode);
		bindT(m_arpClock, g_bindArpClock);
		bindT(m_arpPattern, g_bindArpPattern);
		bindT(m_arpDirection, g_bindArpDirection);
		bindT(m_arpOrder, g_bindArpNoteOrder);
		bindT(m_arpVelocity, g_bindArpVelocity);
		bindT(m_arpTempo, g_bindArpTempo);
		bindT(m_arpRange, g_bindArpRange);

		for (auto* arpReset : m_arpReset)
			bindT(arpReset, g_bindArpReset);
	}
}
