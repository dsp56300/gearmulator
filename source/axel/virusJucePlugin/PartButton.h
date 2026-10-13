#pragma once

#include "jucePluginEditorLib/partbutton.h"

#include <mutex>
#include <set>

namespace genericVirusUI
{
	class VirusEditor;

	class PartButton final : public jucePluginEditorLib::PartButton
	{
	public:
		explicit PartButton(Rml::Element* _button, VirusEditor& _editor);
		~PartButton() override;

		void onClick(Rml::Event& _e) override;
		void setButtonText(const std::string& _text);

	private:
		void selectPreset(const Rml::Event& _event, uint8_t _part) const;

		static std::mutex& getInstancesMutex();
		static std::set<const PartButton*>& getInstances();
		static bool isAlive(const PartButton* _button);

		VirusEditor& m_editor;
	};
}
