#pragma once

#include <string>

#include "convertedObject.h"
#include "coStyle.h"

#include "RmlUi/Core/Element.h"

namespace genericUI
{
	class UiObject;
}

namespace rmlPlugin::skinConverter
{
	class SkinConverter
	{
	public:
		SkinConverter(genericUI::Editor& _editor, const genericUI::UiObject& _root, std::string _outputPath, std::string _rmlFileName, std::string _rcssFileName, std::map<std::string, std::string>&& _idReplacements = {});

	private:
		bool convertUiObject(ConvertedObject& _co, const genericUI::UiObject& _object);
		void writeRmlFile(const std::string& _fileName);
		void writeRcssFile(const std::string& _fileName);
		void writeStyles(std::stringstream& _out, uint32_t _depth = 0);

		void setDefaultProperties(ConvertedObject& _co, const genericUI::UiObject& _object);

		void convertUiObjectComboBox(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectImage(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectRoot(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectRotary(ConvertedObject& _co, const genericUI::UiObject& _object);

		std::string getId(const genericUI::UiObject& _object);

		std::string createTextStyle(const genericUI::UiObjectStyle& _style);
		std::string addStyle(const std::string& _prefix, const CoStyle& _style);

		genericUI::Editor& m_editor;
		const genericUI::UiObject& m_rootObject;
		ConvertedObject m_root;

		std::string m_outputPath;
		std::string m_rmlFileName;
		std::string m_rcssFileName;

		Rml::ElementDocument* m_doc;

		std::map<std::string, CoStyle> m_styles;

		std::map<std::string, std::string> m_idReplacements;

		std::map<std::string, genericUI::TabGroup> m_tabGroups;
	};
}
