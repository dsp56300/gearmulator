#pragma once

#include <string>

#include "convertedObject.h"
#include "coStyle.h"
#include "skinConverterOptions.h"

#include "RmlUi/Core/Element.h"

namespace rmlPlugin::skinConverter
{
	struct SkinConverterOptions;
}

namespace genericUI
{
	class UiObject;
}

namespace rmlPlugin::skinConverter
{
	class SkinConverter
	{
	public:
		SkinConverter(genericUI::Editor& _editor, const genericUI::UiObject& _root, std::string _outputPath, std::string _rmlFileName, std::string _rcssFileName, SkinConverterOptions&& _options);

	private:
		void collectTabGroupsRecursive(const genericUI::UiObject& _object);

		bool convertUiObject(ConvertedObject& _co, const genericUI::UiObject& _object);
		void writeRmlFile(const std::string& _fileName);
		void writeRcssFile(const std::string& _fileName);
		void writeStyles(std::stringstream& _out, uint32_t _depth = 0);

		void setDefaultProperties(ConvertedObject& _co, const genericUI::UiObject& _object);

		void convertUiObjectButton(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectComboBox(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectImage(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectLabel(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectRoot(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectRotary(ConvertedObject& _co, const genericUI::UiObject& _object);
		void convertUiObjectTextButton(ConvertedObject& _co, const genericUI::UiObject& _object, bool _isHyperlink);

		std::string getId(const genericUI::UiObject& _object);

		std::string addTextStyle(const genericUI::UiObjectStyle& _style);
		static CoStyle createTextStyle(const genericUI::UiObjectStyle& _style, float _fallbackFontSize = 0.0f, float _fontSizeScale = 1.0f);

		std::string createImageStyle(const std::string& _imageName, const std::vector<std::string>& _states);
		std::string createKnobStyle(const std::string& _imageName);

		std::string addStyle(const std::string& _prefix, const CoStyle& _style);

		std::string createSpritesheet(const genericUI::UiObject& _object);
		void addSpritesheet(const std::string& _key, CoSpritesheet&& _spritesheet);
		bool spriteExists(const std::string& _spriteName) const;

		bool createCondition(ConvertedObject& _co, const genericUI::UiObject& _obj);
		std::string createConditionDisabledAlphaClass(float _disabledAlpha);

		std::string getAndValidateTextureName(const genericUI::UiObject& _object) const;

		const genericUI::UiObject* getTemplate(const std::string& _name) const;

		bool createGlobalStyles();

		genericUI::UiObject* findChildByName(const genericUI::UiObject& _object, const std::string& _name);

		genericUI::Editor& m_editor;
		const genericUI::UiObject& m_rootObject;
		ConvertedObject m_root;

		std::string m_outputPath;
		std::string m_rmlFileName;
		std::string m_rcssFileName;

		Rml::ElementDocument* m_doc;

		std::map<std::string, CoStyle> m_styles;
		std::map<std::string, CoSpritesheet> m_spritesheets;
		std::set<std::string> m_knownSprites;

		SkinConverterOptions m_options;

		std::map<std::string, genericUI::TabGroup> m_tabGroups;

		std::map<std::string, std::shared_ptr<genericUI::UiObject>> m_templates;
	};
}
