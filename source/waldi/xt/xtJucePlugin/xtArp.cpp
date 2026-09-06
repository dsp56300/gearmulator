#include "xtArp.h"

#include "xtController.h"
#include "xtEditor.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

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
		, m_arpMode      (_editor.findChildByParam("ArpMode"))
		, m_arpClock     (_editor.findChildByParam("ArpClock"))
		, m_arpPattern   (_editor.findChildByParam("ArpPattern"))
		, m_arpDirection (_editor.findChildByParam("ArpDirection"))
		, m_arpOrder     (_editor.findChildByParam("ArpNoteOrder"))
		, m_arpVelocity  (_editor.findChildByParam("ArpVelocity"))
		, m_arpTempo     (_editor.findChildByParam("ArpTempo"))
		, m_arpRange     (_editor.findChildByParam("ArpRange"))
	{
		m_arpReset = _editor.findChildreByParam("ArpReset", 2);

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
	
	void Arp::bind(Rml::Element* _component, const char** _bindings) const
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

		// not to current part because in multi mode its the multi arp and single single mode the part is 0
		constexpr uint8_t part = 0;
		m_editor.getRmlParameterBinding()->bind(*_component, paramName, part);
	}

	void Arp::bind() const
	{
		bind(m_arpMode, g_bindArpMode);
		bind(m_arpClock, g_bindArpClock);
		bind(m_arpPattern, g_bindArpPattern);
		bind(m_arpDirection, g_bindArpDirection);
		bind(m_arpOrder, g_bindArpNoteOrder);
		bind(m_arpVelocity, g_bindArpVelocity);
		bind(m_arpTempo, g_bindArpTempo);
		bind(m_arpRange, g_bindArpRange);

		for (auto* arpReset : m_arpReset)
			bind(arpReset, g_bindArpReset);
	}
}
