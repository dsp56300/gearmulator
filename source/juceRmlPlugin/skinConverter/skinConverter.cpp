#include "skinConverter.h"

#include <algorithm>
#include <cassert>
#include <fstream>

#include "convertedObject.h"
#include "scHelper.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

#include "jucePluginEditorLib/pluginDataModel.h"

#include "juceUiLib/buttonStyle.h"
#include "juceUiLib/comboboxStyle.h"
#include "juceUiLib/editor.h"
#include "juceUiLib/labelStyle.h"
#include "juceUiLib/rotaryStyle.h"
#include "juceUiLib/textbuttonStyle.h"
#include "juceUiLib/uiObject.h"

namespace genericUI
{
	class RotaryStyle;
}

namespace rmlPlugin::skinConverter
{
	SkinConverter::SkinConverter(genericUI::Editor& _editor, const genericUI::UiObject& _root, std::string _outputPath, std::string _rmlFileName, std::string _rcssFileName, SkinConverterOptions&& _options)
		: m_editor(_editor)
		, m_rootObject(_root)
		, m_outputPath(std::move(_outputPath))
		, m_rmlFileName(std::move(_rmlFileName))
		, m_rcssFileName(std::move(_rcssFileName))
		, m_options(std::move(_options))
	{
		collectTabGroupsRecursive(m_rootObject);

		convertUiObject(m_root, m_rootObject);

		createGlobalStyles();

		writeRmlFile(m_rmlFileName);
		writeRcssFile(m_rcssFileName);
	}

	void SkinConverter::collectTabGroupsRecursive(const genericUI::UiObject& _object)
	{
		if (const auto& tabGroup = _object.getTabGroup(); tabGroup.isValid())
			m_tabGroups.insert({tabGroup.getName(), tabGroup});

		for (const auto& link : _object.getControllerLinks())
		{
			ControllerLinkDesc l;

			l.source = link->getSourceName();
			l.target = link->getDestName();
			l.conditionButton = link->getConditionButtonName();

			auto it = m_controllerLinks.find(link->getSourceName());
			if (it != m_controllerLinks.end())
				it->second.push_back(l);
			else
				m_controllerLinks.insert({ link->getSourceName(), {l} });
		}

		for (const auto& child : _object.getChildren())
			collectTabGroupsRecursive(*child);
	}

	bool SkinConverter::convertUiObject(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		if (_object.hasComponent("rmlui"))
			return false;

		setDefaultProperties(_co, _object);

		if (_object.hasComponent("button"))					convertUiObjectButton(_co, _object);
		else if (_object.hasComponent("combobox"))			convertUiObjectComboBox(_co, _object);
		else if (_object.hasComponent("image"))				convertUiObjectImage(_co, _object);
		else if (_object.hasComponent("label"))				convertUiObjectLabel(_co, _object);
		else if (_object.hasComponent("root"))				convertUiObjectRoot(_co, _object);
		else if (_object.hasComponent("rotary"))			convertUiObjectRotary(_co, _object);
		else if (_object.hasComponent("textbutton"))		convertUiObjectTextButton(_co, _object, false);
		else if (_object.hasComponent("hyperlinkbutton"))	convertUiObjectTextButton(_co, _object, true);

		if (getId(_object) == "container-patchmanager")
		{
			if (findChildByName(_object, "container-patchmanager") == nullptr)
			{
				// create exactly one child, for the patchmanager
				ConvertedObject pmCo;
				pmCo.tag = "template";
				pmCo.attribs.set("src", "patchmanager");
				_co.children.emplace_back(std::move(pmCo));
				return true;
			}
		}

		for (const auto& child : _object.getChildren())
		{
			ConvertedObject childCo;
			if (convertUiObject(childCo, *child))
				_co.children.emplace_back(std::move(childCo));
		}

		for (const auto& t : _object.getTemplates())
		{
			const auto name = t->getName();

			if (m_templates.find(name) == m_templates.end())
				m_templates.insert({name, t});
		}

		return true;
	}

	void SkinConverter::writeRmlFile(const std::string& _fileName)
	{
		std::stringstream ss;
		ss << "<rml>\n";
		ss << "\t<head>\n";
		ss << "\t\t" << R"(<link type="text/template" href="tus_colorpicker.rml"/>)" << '\n';
		ss << "\t\t" << R"(<link type="text/template" href="tus_patchmanager.rml"/>)" << '\n';
		ss << "\t\t" << R"(<link type="text/rcss" href="tus_default.rcss"/>)" << '\n';
		ss << "\t\t" << R"(<link type="text/rcss" href="tus_juceskin.rcss"/>)" << '\n';

		for (const auto& style : m_options.includeStyles)
			ss << "\t\t" << R"(<link type="text/rcss" href=")" << style << R"("/>)" << '\n';

		if (!m_styles.empty())
		{
			if (!m_rcssFileName.empty())
			{
				ss << "\t\t<link type=\"text/rcss\" href=\"" << m_rcssFileName << "\"/>\n";
			}
			else
			{
				ss << "\t\t<style>\n";
				writeStyles(ss, 3);
				ss << "\t\t</style>\n";
			}
		}
		ss << "\t</head>\n";

		m_root.write(ss, 1);

		ss << "</rml>\n";

		std::ofstream file(m_outputPath + _fileName);
		if (!file.is_open())
			throw std::runtime_error("Failed to open RML file for writing: " + _fileName);
		file << ss.str();
		file.close();
	}
	void SkinConverter::writeRcssFile(const std::string& _fileName)
	{
		std::stringstream ss;
		writeStyles(ss);
		ss << '\n';
		std::ofstream file(m_outputPath + _fileName);
		if (!file.is_open())
			throw std::runtime_error("Failed to open RCSS file for writing: " + _fileName);
		file << ss.str();
		file.close();
	}

	void SkinConverter::writeStyles(std::stringstream& _out, const uint32_t _depth/* = 0*/)
	{
		for (const auto& [name, style] : m_spritesheets)
		{
			if (style.properties.size() > 1)
				style.write(_out, "@spritesheet " + name, _depth);
		}

		for (const auto& [name, style] : m_styles)
			style.write(_out, name, _depth);
	}

	void SkinConverter::setDefaultProperties(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.set(getId(_object), _object);

		genericUI::UiObjectStyle style(m_editor);
		style.apply(m_editor, _object);

		if (!_object.hasComponent("root"))
			_co.classes.emplace_back("jucePos");

		const auto backgroundColor = style.getBackgroundColor();

		if (backgroundColor.getAlpha())
			_co.style.add("background-color", helper::toRmlColorString(backgroundColor));

		auto param = _object.getProperty("parameter");
		if (!param.empty())
		{
			_co.attribs.set("data-model", RmlParameterBinding::getDataModelName(RmlParameterBinding::CurrentPart));
			_co.attribs.set("param", param);
		}

		const auto& componentProperties = _object.getComponentProperties();

		for (auto prop : componentProperties)
			_co.attribs.set(prop.first, prop.second);

		// check if object is part of any tab group
		for (const auto& [name, group] : m_tabGroups)
		{
			const auto& buttons = group.getButtonNames();
			const auto& pages = group.getPageNames();

			bool isButton = false;

			for (size_t i=0; i<buttons.size(); ++i)
			{
				if (buttons[i] != _object.getName())
					continue;
				_co.attribs.set("tabgroup", name);
				_co.attribs.set("tabbutton", std::to_string(i));
				isButton = true;
				break;
			}

			if (!isButton)
			{
				for (size_t i=0; i<pages.size(); ++i)
				{
					if (pages[i] != _object.getName())
						continue;
					_co.attribs.set("tabgroup", name);
					_co.attribs.set("tabpage", std::to_string(i));
					break;
				}
			}
		}

		// check if any controller link source
		const auto it = m_controllerLinks.find(_object.getName());
		if (it != m_controllerLinks.end())
		{
			assert(it->second.size() == 1);

			for (const auto& link : it->second)
			{
				_co.attribs.set("controllerLinkTarget", link.target);
				_co.attribs.set("controllerLinkCondition", link.conditionButton);
			}
		}

		createCondition(_co, _object);

		auto url = _object.getProperty("url");

		if (!url.empty())
		{
			url = Rml::StringUtilities::EncodeRml(url);
			_co.attribs.set("url", url);
		}
	}

	void SkinConverter::convertUiObjectButton(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		const auto imageName = createSpritesheet(_object);

		_co.classes.emplace_back("juceButton");

		if (!imageName.empty())
		{
			const auto className = createImageStyle(imageName, {}).substr(1);
			createImageStyle(imageName, {"checked"});
			createImageStyle(imageName, {"checked", "active"});
			createImageStyle(imageName, {"checked", "hover"});
			createImageStyle(imageName, {"hover"});

			_co.classes.emplace_back(className);
		}

		const auto isToggle = _object.getPropertyInt("isToggle");

		_co.tag = "button";
		_co.attribs.set("isToggle", isToggle ? "1" : "0");

		auto param = _object.getProperty("parameter");
		if (!param.empty())
		{
			auto valueOn = _object.getPropertyInt("value", -1);
			if (valueOn != -1)
				_co.attribs.set("valueOn", std::to_string(valueOn));

			auto valueOff = _object.getPropertyInt("valueOff", -1);
			if (valueOff != -1)
				_co.attribs.set("valueOff", std::to_string(valueOff));
		}

		genericUI::ButtonStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto& hitAreaOffset = style.getHitAreaOffset();

		if (hitAreaOffset.getX() || hitAreaOffset.getY() || hitAreaOffset.getWidth() || hitAreaOffset.getHeight())
		{
			auto& hitBox = _co.children.emplace_back();

			hitBox.tag = "buttonhittest";

			hitBox.classes.emplace_back("jucePos");

			hitBox.position.x = static_cast<float>(hitAreaOffset.getX());
			hitBox.position.y = static_cast<float>(hitAreaOffset.getY());
			hitBox.position.width = _co.position.width + static_cast<float>(hitAreaOffset.getWidth());
			hitBox.position.height = _co.position.height + static_cast<float>(hitAreaOffset.getHeight());
		}
	}

	void SkinConverter::convertUiObjectComboBox(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "combo";
		genericUI::ComboboxStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = addTextStyle(style);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.push_back(className.substr(1));

		auto& textElem = _co.children.emplace_back();

		textElem.tag = "combotext";

		textElem.style.add("pointer-events", "none"); // text element must not block mouse events

		textElem.classes.emplace_back("jucePos");

		textElem.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));

		textElem.position.x = static_cast<float>(style.getComboOffsetL());
		textElem.position.y = static_cast<float>(style.getComboOffsetT());
		textElem.position.width = _co.position.width + static_cast<float>(style.getComboOffsetR());
		textElem.position.height = _co.position.height + static_cast<float>(style.getComboOffsetB());
	}

	void SkinConverter::convertUiObjectImage(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		const auto texture = getAndValidateTextureName(_object);

		// if the texture is empty, rmlui displays a white rectangle, so we don't create an image element in that case
		if (texture.empty())
			return;

		// if the image size does not match the size of the object, the image is put into the upper left corner. In this case, we use a decorator instead of an image element
		const auto* drawable = m_editor.getImageDrawable(_object.getProperty("texture"));
		if (drawable && (drawable->getWidth() != static_cast<int>(_co.position.width) || drawable->getHeight() != static_cast<int>(_co.position.height)))
		{
			_co.style.add("decorator", "image(" + texture + ".png scale-none left top)");
		}
		else
		{
			_co.tag = "img";
			_co.attribs.set("src", texture + ".png");
		}
	}

	void SkinConverter::convertUiObjectLabel(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "div";
		genericUI::LabelStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = addTextStyle(style);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.emplace_back("juceLabel");
		_co.classes.push_back(className.substr(1));

		auto param = _object.getProperty("parameter");
		if (!param.empty())
			_co.innerText = "{{" + RmlParameterRef::createVariableName(param) + "_text}}";
		else
			_co.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));
	}

	void SkinConverter::convertUiObjectRoot(ConvertedObject& _co, const genericUI::UiObject& _object) const
	{
		_co.tag = "body";

		if (m_editor.getScale() != 1.0f)
			_co.attribs.set("rootScale", std::to_string(m_editor.getScale()));

		// https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html
		// "Putting the data-model attribute on the <body> tag may cause issues when combined with templates."
		// yes, causes issues indeed, so we don't do it and use "data-model" for every element that has a parameter instead. Blows up the RML a bit, but works fine.
//		_co.attribs.set("data-model", RmlParameterBinding::getDataModelName(RmlParameterBinding::CurrentPart));
	}

	void SkinConverter::convertUiObjectRotary(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		genericUI::RotaryStyle style(m_editor);
		(static_cast<genericUI::UiObjectStyle&>(style)).apply(m_editor, _object);

		if (style.getStyle() == genericUI::RotaryStyle::Style::Rotary)
		{
			const auto imageName = createSpritesheet(_object);

			if (!imageName.empty())
			{
				const auto styleName = createKnobStyle(imageName);

				_co.tag = "knob";
				_co.classes.emplace_back("juceRotary");
				_co.classes.emplace_back(styleName.substr(1));
			}
		}
		else if (style.getStyle() == genericUI::RotaryStyle::Style::LinearHorizontal || style.getStyle() == genericUI::RotaryStyle::Style::LinearVertical)
		{
			const auto isHorizontal = style.getStyle() == genericUI::RotaryStyle::Style::LinearHorizontal;

			_co.tag = "input";
			_co.attribs.set("type", "range");

			CoStyle sliderStyle;

			const auto styleName = addStyle(isHorizontal ? ".sliderH" : ".sliderV", sliderStyle);

			CoStyle barStyle;
			barStyle.add("decorator", "image(" + getAndValidateTextureName(_object) + ".png contain)");

			const auto textureName = _object.getProperty("texture");
			const auto* drawable = m_editor.getImageDrawable(textureName);
			assert(drawable);
			if (!drawable)
				throw std::runtime_error("Failed to find image drawable for '" + textureName + "'");

			barStyle.add("width", std::to_string(drawable->getWidth()) + "dp");
			barStyle.add("height", std::to_string(drawable->getHeight()) + "dp");

			m_styles.insert({ styleName + " sliderbar", barStyle });

			_co.classes.emplace_back(styleName.substr(1));
			_co.classes.emplace_back("juceSlider");
			_co.classes.emplace_back("juceSliderH");
		}
	}

	void SkinConverter::convertUiObjectTextButton(ConvertedObject& _co, const genericUI::UiObject& _object, const bool _isHyperlink)
	{
		_co.tag = "div";
		genericUI::TextButtonStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = addTextStyle(style, true);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.emplace_back("juceTextButton");
		if (_isHyperlink)
			_co.classes.emplace_back("juceHyperlinkButton");
		_co.classes.push_back(className.substr(1));

		_co.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));
	}

	std::string SkinConverter::getId(const genericUI::UiObject& _object)
	{
		return getId(_object.getName());
	}

	std::string SkinConverter::getId(const std::string& _sourceId)
	{
		if (_sourceId.empty())
			return {};
		const auto it = m_options.idReplacements.find(_sourceId);
		return it != m_options.idReplacements.end() ? it->second : _sourceId;
	}

	std::string SkinConverter::addTextStyle(const genericUI::UiObjectStyle& _style, bool _isButton/* = false*/)
	{
		CoStyle style = createTextStyle(_style, 0, 1, _isButton);
		return addStyle(".txt", style);
	}

	CoStyle SkinConverter::createTextStyle(const genericUI::UiObjectStyle& _style, float _fallbackFontSize/* = 0.0f*/, float _fontSizeScale/* = 1.0f*/, bool _isButton/* = false*/)
	{
		CoStyle style;
		const auto& fontName = _style.getFontName();
		if (!fontName.empty())
			style.add("font-family", fontName);

		float fontSize = static_cast<float>(_style.getTextHeight());
		if (fontSize <= 0.0f)
			fontSize = _fallbackFontSize;
		fontSize *= _fontSizeScale;
		if (fontSize > 0.0f)
			style.add("font-size", std::to_string(fontSize) + "dp");

		if (_style.getBold())
			style.add("font-weight", "bold");

		if (_style.getItalic())
			style.add("font-style", "italic");

		juce::Justification alignH = _isButton ? juce::Justification::horizontallyCentred : _style.getAlign().getOnlyHorizontalFlags();

		if (alignH == juce::Justification::horizontallyCentred)
		{
			style.add("text-align", "center");
		}
		else if (alignH == juce::Justification::right)
		{
			style.add("text-align", "right");
		}
		else
		{
			style.add("text-align", "left");
		}

		style.add("color", helper::toRmlColorString(_style.getColor()));

		return style;
	}

	std::string SkinConverter::createImageStyle(const std::string& _imageName, const std::vector<std::string>& _states)
	{
		auto styleName = "." + _imageName;

		if (styleName == ".button")
			styleName = ".btn"; // collides with the global button element style

		for (const auto& state : _states)
			styleName += ":" + state;

		if (m_styles.find(styleName) != m_styles.end())
			return styleName;

		CoStyle style;

		std::string spriteName = _imageName;

		if (_states.empty())
		{
			spriteName += "_default";
		}
		else
		{
			spriteName += '_' + _states[0];

			for (size_t i=1; i<_states.size(); ++i)
				spriteName += '-' + _states[i];
		}

		if (!spriteExists(spriteName))
			return {};

		style.add("decorator", "image(" + spriteName + " fill)");

		m_styles.insert({ styleName, style });

		return styleName;
	}

	std::string SkinConverter::createKnobStyle(const std::string& _imageName)
	{
		auto spritesheet = m_spritesheets.find(_imageName);
		if (spritesheet == m_spritesheets.end())
			return {};
		const auto frameCount = spritesheet->second.spriteCount;
		CoStyle style;
		style.add("frames", std::to_string(frameCount));
		style.add("spriteprefix", _imageName + "_");
		return addStyle(".knob", style);
	}

	std::string SkinConverter::addStyle(const std::string& _prefix, const CoStyle& _style)
	{
		for (const auto& [name,s] : m_styles)
		{
			if (name.size() < _prefix.size() || name.substr(0, _prefix.size()) != _prefix)
				continue;
			if (s == _style)
				return name;
		}

		if (m_styles.find(_prefix) == m_styles.end())
		{
			m_styles.insert({ _prefix, _style });
			return _prefix;
		}

		uint32_t uid = 1;
		while (m_styles.find(_prefix + std::to_string(uid)) != m_styles.end())
			++uid;
		const auto name = _prefix + std::to_string(uid);
		m_styles.insert({ name, _style });
		return name;
	}

	std::string SkinConverter::createSpritesheet(const genericUI::UiObject& _object)
	{
		const auto originalImageName = _object.getProperty("texture");

		if (originalImageName.empty())
			return {};

		const auto imageName = getAndValidateTextureName(_object);

		if (m_spritesheets.find(imageName) != m_spritesheets.end())
			return imageName; // already created

		const auto* drawable = dynamic_cast<const juce::DrawableImage*>(m_editor.getImageDrawable(originalImageName));
		assert(drawable);
		if (!drawable)
			throw std::runtime_error("Failed to find image drawable for '" + imageName + "'");

		auto tileSizeX = _object.getPropertyInt("tileSizeX");
		auto tileSizeY = _object.getPropertyInt("tileSizeY");

		const auto& image = drawable->getImage();

		ButtonProperties buttonProperties;

		if (_object.hasComponent("button"))
		{
			buttonProperties.isButton = true;

			buttonProperties.normalImage = _object.getPropertyInt("normalImage", -1);
			buttonProperties.overImage = _object.getPropertyInt("overImage", -1);
			buttonProperties.downImage = _object.getPropertyInt("downImage", -1);
			buttonProperties.normalImageOn = _object.getPropertyInt("normalImageOn", -1);
			buttonProperties.overImageOn = _object.getPropertyInt("overImageOn", -1);
			buttonProperties.downImageOn = _object.getPropertyInt("downImageOn", -1);
		}

		auto spriteSheets = createSpritesheet(m_outputPath, static_cast<int>(m_options.maxTextureWidth), static_cast<int>(m_options.maxTextureHeight), tileSizeX, tileSizeY, imageName, image, buttonProperties);

		for (auto& [key, spritesheet] : spriteSheets)
			addSpritesheet(key, std::move(spritesheet));

		return imageName;
	}

	std::vector<std::pair<std::string, CoSpritesheet>> SkinConverter::createSpritesheet(const std::string& _outputPath, int maxTextureWidth, int maxTextureHeight, int tileSizeX, int tileSizeY, const std::string& imageName, const juce::Image& image, const ButtonProperties& _buttonProperties)
	{
		const auto w = image.getWidth();
		const auto h = image.getHeight();

		// correct tile size if it is too large, some skins have incorrect values
		tileSizeX = std::min(tileSizeX, w);
		tileSizeY = std::min(tileSizeY, h);

		struct Sprite
		{
			int page = -1;
			Rml::Rectanglei rect;
		};

		std::vector<Sprite> sprites;

		maxTextureWidth = std::max(tileSizeX, maxTextureWidth);
		maxTextureHeight = std::max(tileSizeY, maxTextureHeight);

		for (int y=0; y<h; y += tileSizeY)
		{
			for (int x=0; x<w; x += tileSizeX)
			{
				if (x + tileSizeX > w || y + tileSizeY > h)
					continue; // skip if the tile would be out of bounds
				Sprite sprite;
				sprite.rect = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(x, y), Rml::Vector2i(tileSizeX, tileSizeY));
				sprites.emplace_back(sprite);
			}
		}

		std::vector<CoSpritesheet> pageSpritesheets;

		if (w > maxTextureWidth || h > maxTextureHeight)
		{
			struct RemappedSprite
			{
				uint32_t index;
				Rml::Rectanglei from;
				Rml::Rectanglei to;
			};

			struct Page
			{
				std::vector<RemappedSprite> sprites;
				int width = 0;
				int height = 0;
			};

			std::vector<Page> pages;

			{
				int x = 0;
				int y = 0;

				Page page;

				for (size_t i=0; i<sprites.size(); ++i)
				{
					auto& sprite = sprites[i];

					if (x + sprite.rect.Width() > maxTextureWidth)
					{
						x = 0;
						y += tileSizeY;
					}
					if (y + sprite.rect.Height() > maxTextureHeight)
					{
						pages.push_back(std::move(page));
						page = {};
						x = y = 0;
					}

					RemappedSprite rs;

					rs.index = static_cast<uint32_t>(i);
					rs.from = sprite.rect;
					rs.to = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(x, y), Rml::Vector2i(sprite.rect.Width(), sprite.rect.Height()));

					page.width = std::max(page.width, rs.to.Right());
					page.height = std::max(page.height, rs.to.Bottom());

					page.sprites.push_back(rs);

					sprites[i].page = static_cast<int>(pages.size());
					sprites[i].rect = rs.to;

					x += tileSizeX;
				}

				if (!page.sprites.empty())
					pages.push_back(std::move(page));
			}

			juce::Image::BitmapData sourceData(const_cast<juce::Image&>(image), 0, 0, w, h, juce::Image::BitmapData::readOnly);

			for (size_t i=0; i<pages.size(); ++i)
			{
				const auto& p = pages[i];

				juce::Image spritesheetImage(image.getFormat(), p.width, p.height, true);

				juce::Image::BitmapData destData(spritesheetImage, 0, 0, p.width, p.height, juce::Image::BitmapData::writeOnly);

				for (const auto& s : p.sprites)
				{
					const auto& from = s.from;
					const auto& to = s.to;

					int xSrc = from.Left();
					int ySrc = from.Top();

					int xDest = to.Left();
					int yDest = to.Top();

					int width = from.Width();
					int height = from.Height();

					const auto copySize = width * destData.pixelStride;

					for (int y=0; y<height; ++y)
					{
						const auto* srcRow = sourceData.getLinePointer(ySrc + y) + static_cast<ptrdiff_t>(xSrc * destData.pixelStride);
						auto* destRow = destData.getLinePointer(yDest + y) + static_cast<ptrdiff_t>(xDest * destData.pixelStride);

						std::memcpy(destRow, srcRow, copySize);
					}
				}

				const auto filename = _outputPath + imageName + "_page" + std::to_string(i) + ".png";
				juce::File file(filename);
				file.deleteFile();
				file.create();
				juce::PNGImageFormat pngFormat;
				auto filestream = file.createOutputStream();

				pngFormat.writeImageToStream(spritesheetImage, *filestream);
				filestream->flush();

				auto& pageSpritesheet = pageSpritesheets.emplace_back();
				pageSpritesheet.add("src", imageName + "_page" + std::to_string(i) + ".png");
				pageSpritesheet.spriteCount = static_cast<uint32_t>(p.sprites.size());
			}
		}

		CoSpritesheet spritesheet;
		spritesheet.add("src", imageName + ".png");
		spritesheet.spriteCount = static_cast<uint32_t>(sprites.size());

		auto addSprite = [&spritesheet, &pageSpritesheets, &imageName](const std::string& _name, const Sprite& _sprite)
		{
			const auto& rect = _sprite.rect;

			auto& ss = (_sprite.page >= 0) ? pageSpritesheets[_sprite.page] : spritesheet;

			ss.add(imageName + '_' + _name, 
				std::to_string(rect.Left()) + "px " + 
				std::to_string(rect.Top()) + "px " + 
				std::to_string(rect.Width()) + "px " + 
				std::to_string(rect.Height()) + "px");
		};

		auto addIndex = [&addSprite, &sprites](const std::string& _name, const int _index)
		{
			if (_index >= 0 && static_cast<size_t>(_index) < sprites.size())
				addSprite(_name, sprites[_index]);
		};

		// the type of sprite naming is determined by the type of object
		if (_buttonProperties.isButton)
		{
			// create sprites for button states
			addIndex("default", _buttonProperties.normalImage);

			addIndex("hover", _buttonProperties.overImage);
			addIndex("active", _buttonProperties.downImage);
			addIndex("checked", _buttonProperties.normalImageOn);
			addIndex("checked-hover", _buttonProperties.overImageOn);
			addIndex("checked-active", _buttonProperties.downImageOn);
		}
		else
		{
			for (size_t i=0; i<sprites.size(); ++i)
			{
				char n[32];
				(void)snprintf(n, sizeof(n), "%03u", static_cast<uint32_t>(i));
				addSprite(n, sprites[i]);
			}
		}

		std::vector<std::pair<std::string, CoSpritesheet>> result;

		auto addSpritesheet = [&result](const std::string& _key, CoSpritesheet&& _spritesheet)
		{
			result.emplace_back(_key, std::move(_spritesheet));
		};

		addSpritesheet(imageName, std::move(spritesheet));

		for (size_t i=0; i<pageSpritesheets.size(); ++i)
		{
			auto& ss = pageSpritesheets[i];
			if (ss.properties.size() <= 1)
				continue;
			addSpritesheet(imageName + "_page" + std::to_string(i), std::move(ss));
		}

		return result;
	}

	void SkinConverter::addSpritesheet(const std::string& _key, CoSpritesheet&& _spritesheet)
	{
		if (m_spritesheets.find(_key) != m_spritesheets.end())
			return; // already exists

		for (const auto& [k, v] : _spritesheet.properties)
		{
			if (k == "src")
				continue;
			m_knownSprites.insert(k);
		}

		m_spritesheets.insert({ _key, std::move(_spritesheet) });
	}

	bool SkinConverter::spriteExists(const std::string& _spriteName) const
	{
		return m_knownSprites.find(_spriteName) != m_knownSprites.end();
	}

	bool SkinConverter::createCondition(ConvertedObject& _co, const genericUI::UiObject& _obj)
	{
		const auto disabledAlpha = _obj.getPropertyFloat("disabledAlpha", -1.0f);

		if (disabledAlpha >= 1.0f)
			return true;	// the object is always visible anyway, why would anyone do this?

		// what is the class name? If there is no disabledAlpha, the visibility is toggled. Otherwise, the opacity is modified.
		std::string className;

		if (disabledAlpha > 0.0f)
			className = createConditionDisabledAlphaClass(disabledAlpha);
		else
			className = "juceConditionDisabled";

		std::string attrib = "data-class-" + className;

		auto paramName = _obj.getProperty("enableOnParameter");

		if (!paramName.empty())
		{
			const auto paramVar = RmlParameterRef::createVariableName(paramName) + "_value";

			// create groups for contiuous parameter values to have easier if-statements in RML
			std::vector<std::pair<pluginLib::ParamValue, pluginLib::ParamValue>> valueRanges;

			const auto valueStrings = _obj.readConditionValues();
			std::vector<pluginLib::ParamValue> values;
			values.reserve(valueStrings.size());

			for (const auto& v : valueStrings)
				values.emplace_back(strtol(v.c_str(), nullptr, 10));

			std::sort(values.begin(), values.end());

			for (size_t i=0; i<values.size(); ++i)
			{
				auto first = values[i];
				auto last = first;

				for (size_t j=i+1; j<values.size(); ++j)
				{
					if (values[j] != last + 1)
						break;

					last = values[j];
					i = j;
				}

				valueRanges.emplace_back(first, last);
			}

			std::stringstream ss;
			ss << "!(";
			for (size_t i=0; i<valueRanges.size(); ++i)
			{
				if (i > 0)
					ss << " || ";
				const auto& range = valueRanges[i];
				if (range.first == range.second)
					ss << paramVar << " == " << range.first;
				else
					ss << "(" << paramVar << " >= " << range.first << " && " << paramVar << " <= " << range.second << ")";
			}
			ss << ")";

			// conditions were always based on the current part
			_co.attribs.set("data-model", RmlParameterBinding::getDataModelName(RmlParameterBinding::CurrentPart));

			_co.attribs.set(attrib, ss.str());

			return true;
		}

		auto key = _obj.getProperty("enableOnKey");

		if (!key.empty())
		{
			std::set<std::string> values = _obj.readConditionValues();

			if (!values.empty())
			{
				std::stringstream ss;

				bool isFirst = true;

				for (const auto& v : values)
				{
					if (isFirst)
						isFirst = false;
					else
						ss << " && ";

					ss << key << "!='" << Rml::StringUtilities::EncodeRml(v) << "'";
				}

				_co.attribs.set("data-model", jucePluginEditorLib::PluginDataModel::getModelName());
				_co.attribs.set(attrib, ss.str());
			}
		}

		return false;
	}

	std::string SkinConverter::createConditionDisabledAlphaClass(const float _disabledAlpha)
	{
		const auto opacity = static_cast<int>(_disabledAlpha * 100.0f);

		const auto className = "juceConditionDisabledAlpha" + std::to_string(opacity);
		if (m_styles.find("." + className) != m_styles.end())
			return className; // already exists
		CoStyle style;
		style.add("opacity", std::to_string(_disabledAlpha));
		style.add("pointer-events", "none");
		m_styles.insert({ "." + className, style });
		return className;
	}

	std::string SkinConverter::getAndValidateTextureName(const genericUI::UiObject& _object) const
	{
		auto tex = _object.getProperty("texture");
		if (tex.empty())
			return tex;

		// spaces need to be replaced with underscores
		auto newName = tex;
		for (auto& c : newName)
		{
			if (c == ' ')
				c = '_';
		}

		if (newName == tex)
			return tex; // no change needed

		const juce::File source(m_outputPath + tex + ".png");
		const juce::File target(m_outputPath + newName + ".png");

		if (source.existsAsFile())
		{
			if (target.existsAsFile())
				return newName;	// already copied before

			if (!source.copyFileTo(target))
				throw std::runtime_error("Failed to copy texture file from '" + source.getFullPathName().toStdString() + "' to '" + target.getFullPathName().toStdString() + "'");
		}

		return newName;
	}

	const genericUI::UiObject* SkinConverter::getTemplate(const std::string& _name) const
	{
		const auto it = m_templates.find(_name);
		return it != m_templates.end() ? it->second.get() : nullptr;
	}

	bool SkinConverter::createGlobalStyles()
	{
		bool res = false;

		constexpr float defaultFontSize = 14.0f;					// juce default font size
		const float fontSizeScale = 1.0f / m_editor.getScale();

		// if there are no patch manager styles, we have to specify the old default ones
		const std::string defaultBackgroundColor = "rgb(32,32,32)";
		const std::string defaultBorderColor = "rgb(67,67,67)";

		{
			CoStyle styleTree;

			if(const auto* pmTreeview = getTemplate("pm_treeview"))
			{
				genericUI::UiObjectStyle os(m_editor);
				os.apply(m_editor, *pmTreeview);

				styleTree.add("background-color", os.getBackgroundColor());

				CoStyle styleTreeNode = createTextStyle(os, defaultFontSize, fontSizeScale);
				if (!styleTree.empty())
					m_styles.insert({ "treenode", styleTreeNode });

				CoStyle styleTreeNodeSelected;
				styleTreeNodeSelected.add("background-color", os.getSelectedItemBackgroundColor());
				if (!styleTreeNodeSelected.empty())
					m_styles.insert({ "treenode:selected", styleTreeNodeSelected });

				res = true;
			}
			else
			{
				styleTree.add("background-color", defaultBackgroundColor);
			}

			if (!styleTree.empty())
				m_styles.insert({ "tree", styleTree });
		}

		{
			CoStyle styleSearch;

			if (const auto* pmSearch = getTemplate("pm_search"))
			{
				genericUI::UiObjectStyle os(m_editor);
				os.apply(m_editor, *pmSearch);
				styleSearch = createTextStyle(os, defaultFontSize, fontSizeScale);

				styleSearch.add("background-color", os.getBackgroundColor());

				if (os.getOutlineColor().getAlpha() > 0)
					styleSearch.add("border", "1dp " + helper::toRmlColorString(os.getOutlineColor()));
				else
					styleSearch.add("border", "0dp transparent");

				res = true;
			}
			else
			{
				styleSearch.add("background-color", defaultBackgroundColor);
				styleSearch.add("border", "1dp " + defaultBorderColor);
			}

			m_styles.insert({ "input.text", styleSearch });
		}

		if (const auto* pmScrollbar = getTemplate("pm_scrollbar"))
		{
			genericUI::UiObjectStyle os(m_editor);
			os.apply(m_editor, *pmScrollbar);
			CoStyle styleScrollbar;
			styleScrollbar.add("background-color", os.getColor());
			if (!styleScrollbar.empty())
			{
				m_styles.insert({ "scrollbarvertical sliderbar, scrollbarhorizontal sliderbar, splitter:active", styleScrollbar });
			}
			res = true;
		}

		{
			CoStyle styleList;

			if(const auto* pmListBox = getTemplate("pm_listbox"))
			{
				genericUI::UiObjectStyle os(m_editor);
				os.apply(m_editor, *pmListBox);

				styleList.add("background-color", os.getBackgroundColor());

				CoStyle styleListEntry = createTextStyle(os, defaultFontSize, fontSizeScale);
				if (!styleList.empty())
					m_styles.insert({ ".listentry, .gridentry", styleListEntry });

				CoStyle styleListEntrySelected;
				styleListEntrySelected.add("background-color", os.getSelectedItemBackgroundColor());
				if (!styleListEntrySelected.empty())
					m_styles.insert({ ".listentry:selected, .gridentry:selected", styleListEntrySelected });

				res = true;
			}
			else
			{
				styleList.add("background-color", defaultBackgroundColor);
			}

			if (!styleList.empty())
				m_styles.insert({ ".list, .grid, .pm-infopanel", styleList });
		}

		auto creatPatchManagerInfoPanelTextStyle = [&](const std::string& _templateName, const std::string& _styleName)
		{
			if (const auto* pmInfoLabel = getTemplate(_templateName))
			{
				genericUI::UiObjectStyle os(m_editor);
				os.apply(m_editor, *pmInfoLabel);
				CoStyle style;
				if (os.getTextHeight())
					style.add("font-size", std::to_string(static_cast<float>(os.getTextHeight()) * fontSizeScale) + "dp");
				style.add("color", os.getColor());
				if (!style.empty())
					m_styles.insert({ _styleName, style });
				res = true;
			}
		};

		creatPatchManagerInfoPanelTextStyle("pm_info_name", ".pm-info-name");
		creatPatchManagerInfoPanelTextStyle("pm_info_label", ".pm-info-headline");
		creatPatchManagerInfoPanelTextStyle("pm_info_text", ".pm-info-text");

		{
			CoStyle statusStyle;

			if (const auto* pmStatus = getTemplate("pm_status_label"))
			{
				genericUI::UiObjectStyle os(m_editor);
				os.apply(m_editor, *pmStatus);
				statusStyle = createTextStyle(os, defaultFontSize, fontSizeScale);

				const auto itSearch = m_styles.find("input.text");
				if (itSearch != m_styles.end())
				{
					const auto itBorder = itSearch->second.properties.find("border");
					if (itBorder != itSearch->second.properties.end())
						statusStyle.add(itBorder->first, itBorder->second);
				}

				res = true;
			}
			else
			{
				statusStyle.add("background-color", defaultBackgroundColor);
				statusStyle.add("border", "1dp " + defaultBorderColor);
			}

			if (!statusStyle.empty())
				m_styles.insert({ ".pm-status", statusStyle });
		}

		CoStyle style;
		style.add("gap", "4dp");
		m_styles.insert({ ".pm-vlayout", style });

		CoStyle defaultFontStyle;

		defaultFontStyle.add("font-size", std::to_string(static_cast<int>(defaultFontSize * fontSizeScale)) + "dp");

		m_styles.insert({ "body", defaultFontStyle });

		// PM background was always black
		CoStyle pmBackgroundStyle;
		pmBackgroundStyle.add("background-color", "black");
		m_styles.insert({ ".pm-hlayout", pmBackgroundStyle });

		return res;
	}

	genericUI::UiObject* SkinConverter::findChildByName(const genericUI::UiObject& _object, const std::string& _name)
	{
		for (const std::unique_ptr<genericUI::UiObject>& child : _object.getChildren())
		{
			if (getId(*child) == _name)
				return child.get();
			auto* found = findChildByName(*child, _name);
			if (found)
				return found;
		}
		return nullptr;
	}
}
