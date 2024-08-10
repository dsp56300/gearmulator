#include "n2xPart.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Part::Part(Editor& _editor, const std::string& _name, const ButtonStyle _buttonStyle) : PartButton(_editor, _name, _buttonStyle), m_editor(_editor)
	{
	}

	void Part::onClick()
	{
		m_editor.getN2xController().setCurrentPart(getPart());
	}

	void Part::mouseDown(const juce::MouseEvent& _e)
	{
		if(!_e.mods.isPopupMenu())
		{
			PartButton<DrawableButton>::mouseDown(_e);
			return;
		}

		juce::PopupMenu menu;

		auto& controller = m_editor.getN2xController();

		{
			juce::PopupMenu menuChannel;

			const auto mp = static_cast<n2x::MultiParam>(n2x::MultiParam::SlotAMidiChannel + getPart());
			const auto name = std::string("PerfMidiChannel") + static_cast<char>('A' + getPart());
			auto* param = controller.getParameter(name, 0);
			const auto ch = param->getUnnormalizedValue();

			for(uint8_t c=0; c<16; ++c)
			{
				menuChannel.addItem((std::string("Channel ") + std::to_string(c+1)).c_str(), true, c == ch, [param, c]
				{
					param->setUnnormalizedValueNotifyingHost(c, pluginLib::Parameter::Origin::Ui);
				});
			}

			menuChannel.addSeparator();
			menuChannel.addItem("Off", true, ch >= 16, [param]
			{
				param->setUnnormalizedValueNotifyingHost(16, pluginLib::Parameter::Origin::Ui);
			});

			menu.addSubMenu("Midi Channel", menuChannel);
		}
		{
			juce::PopupMenu menuOut;

			constexpr auto mp = n2x::MultiParam::OutModeABCD;
			auto o = controller.getMultiParameter(mp);
			auto out = o;

			if(getPart() >= 2)
				out >>= 4;
			else
				out &= 0xf;

			auto setMode = [this, &controller, mp, o](uint8_t _mode)
			{
				auto newO = o;
				if(getPart() < 2)
				{
					newO &= 0xf0;
					newO |= _mode;
				}
				else
				{
					newO &= 0x0f;
					newO |= _mode << 4u;
				}
				controller.setMultiParameter(mp, newO);
			};

			if(getPart() < 2)
			{
				menuOut.addItem("A & B Mono/Stereo", true, out == 0, [setMode, mp]		{ setMode(0); });
				menuOut.addItem("A & B Mono"       , true, out == 1, [setMode, mp]		{ setMode(1); });
				menuOut.addItem("A & B Alternating", true, out == 2, [setMode, mp]		{ setMode(2); });
				menuOut.addItem("A to A / B to B"  , true, out == 3, [setMode, mp]		{ setMode(3); });
			}
			else
			{
				menuOut.addItem("Same as A & B "    , true, out == 0, [setMode, mp]	{ setMode(0); });
				menuOut.addItem("C & D Mono/Stereo" , true, out == 1, [setMode, mp]	{ setMode(1); });
				menuOut.addItem("C & D Mono"        , true, out == 2, [setMode, mp]	{ setMode(2); });
				menuOut.addItem("C & D Alternating" , true, out == 3, [setMode, mp]	{ setMode(3); });
				menuOut.addItem("C to C / D to D"   , true, out == 4, [setMode, mp]	{ setMode(4); });
			}

			menu.addSubMenu("Output Mode", menuOut);
		}

		menu.showMenuAsync({});
	}
}
