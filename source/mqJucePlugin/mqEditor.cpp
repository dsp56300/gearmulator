#include "mqEditor.h"

#include "BinaryData.h"
#include "PluginProcessor.h"

#include "mqController.h"

#include "../jucePluginLib/parameterbinding.h"

#include "../mqLib/mqbuildconfig.h"

namespace mqJucePlugin
{
	static constexpr uint32_t PlayModeListenerId = 1;

	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
		create(_jsonFilename);

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		if(findComponent("ContainerPatchList", false))
			m_patchBrowser.reset(new PatchBrowser(*this, _processor.getController(), _processor.getConfig()));

		auto disableButton = [](juce::Component* _comp)
		{
			_comp->setAlpha(0.5f);
			_comp->setEnabled(false);
		};

		auto disableByName = [this, &disableButton](const std::string& _button)
		{
			if (auto* bt = findComponentT<juce::Button>(_button, false))
				disableButton(bt);
		};

		m_btPlayModeMulti = findComponentT<juce::Button>("btPlayModeMulti", false);
		if(m_btPlayModeMulti)
		{
			if constexpr(mqLib::g_pluginDemo)
			{
				disableButton(m_btPlayModeMulti);
			}
			else
			{
				m_btPlayModeMulti->onClick = [this]()
				{
					m_controller.setPlayMode(m_btPlayModeMulti->getToggleState());
				};
			}
		}

		m_btSave = findComponentT<juce::Button>("btSave", false);
		if (m_btSave)
		{
			if constexpr (mqLib::g_pluginDemo)
			{
				disableButton(m_btSave);
			}
			else
			{
				m_btSave->onClick = [this]()
				{
					onBtSave();
				};
			}
		}

		if constexpr(mqLib::g_pluginDemo)
		{
			disableByName("btPageMulti");
			disableByName("btPageDrum");
		}

		m_btPresetPrev = findComponentT<juce::Button>("btPresetPrev", false);
		m_btPresetNext = findComponentT<juce::Button>("btPresetNext", m_btPresetPrev != nullptr);

		if (m_btPresetPrev)
		{
			m_btPresetPrev->onClick = [this]			{ onBtPresetPrev();	};
			m_btPresetNext->onClick = [this]			{ onBtPresetNext();	};
		}

		m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(m_controller, _binding, *this));

		if(!findComponents("partSelectButton", false).empty())
			m_partSelect.reset(new mqPartSelect(*this, m_controller, _binding));

		addMouseListener(this, true);

		m_controller.onPlayModeChanged.addListener(PlayModeListenerId, [this]()
		{
			if(m_btPlayModeMulti)
				m_btPlayModeMulti->setToggleState(m_controller.isMultiMode(), juce::dontSendNotification);
			if(m_partSelect)
				m_partSelect->onPlayModeChanged();
		});
	}

	Editor::~Editor()
	{
		m_controller.onPlayModeChanged.removeListener(PlayModeListenerId);

		m_partSelect.reset();
		m_focusedParameter.reset();
		m_frontPanel.reset();
	}

	const char* Editor::findEmbeddedResource(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	const char* Editor::findResourceByFilename(const std::string& _filename, uint32_t& _size)
	{
		return findEmbeddedResource(_filename, _size);
	}

	void Editor::mouseEnter(const juce::MouseEvent& _event)
	{
		m_focusedParameter->onMouseEnter(_event);
	}

	void Editor::onBtSave()
	{
	}

	void Editor::onBtPresetPrev()
	{
		if (m_patchBrowser->selectPrevPreset())
			return;
//		m_controller.selectPrevPreset();
	}

	void Editor::onBtPresetNext()
	{
		if (m_patchBrowser->selectNextPreset())
			return;
//		m_controller.selectNextPreset();
	}
}
