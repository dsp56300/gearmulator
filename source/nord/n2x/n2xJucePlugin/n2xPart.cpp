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

			const auto paramMidiChannel = static_cast<n2x::MultiParam>(n2x::MultiParam::SlotAMidiChannel + getPart());
			const auto ch = controller.getMultiParameter(paramMidiChannel);
			for(uint8_t c=0; c<16; ++c)
			{
				menuChannel.addItem((std::string("Channel ") + std::to_string(c+1)).c_str(), true, c == ch, [&controller, c, paramMidiChannel]
				{
					controller.setMultiParameter(paramMidiChannel, c);
				});
			}

			menuChannel.addSeparator();
			menuChannel.addItem("Off", true, ch == 16, [&controller, paramMidiChannel]
			{
				controller.setMultiParameter(paramMidiChannel, 16);
			});

			menu.addSubMenu("Midi Channel", menuChannel);
		}

		menu.showMenuAsync({});
	}
}
