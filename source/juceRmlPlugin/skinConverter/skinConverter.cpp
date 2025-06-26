#include "skinConverter.h"

#include <cassert>
#include <fstream>

#include "convertedObject.h"
#include "scHelper.h"
#include "dsp56kEmu/logging.h"
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
	namespace
	{
		constexpr int g_maxTextureWidth = 2048;
		constexpr int g_maxTextureHeight = 2048;
	}

	SkinConverter::SkinConverter(genericUI::Editor& _editor, const genericUI::UiObject& _root, std::string _outputPath, std::string _rmlFileName, std::string _rcssFileName, std::map<std::string, std::string>&& _idReplacements)
		: m_editor(_editor)
		, m_rootObject(_root)
		, m_outputPath(std::move(_outputPath))
		, m_rmlFileName(std::move(_rmlFileName))
		, m_rcssFileName(std::move(_rcssFileName))
		, m_idReplacements(std::move(_idReplacements))
	{
		CoStyle styleJucePos;
		styleJucePos.add("position", "absolute");

		m_styles.insert({ ".jucePos", styleJucePos });

		CoStyle styleBody;
		styleBody.add("position", "relative");

		m_styles.insert({ "body", styleBody });

		convertUiObject(m_root, m_rootObject);
		writeRmlFile(m_rmlFileName);
		writeRcssFile(m_rcssFileName);
	}
	bool SkinConverter::convertUiObject(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		if (_object.hasComponent("rmlui"))
			return false;

		if (const auto& tabGroup = _object.getTabGroup(); tabGroup.isValid())
			m_tabGroups.insert({tabGroup.getName(), tabGroup});

		setDefaultProperties(_co, _object);

		if (_object.hasComponent("button"))
			convertUiObjectButton(_co, _object);
		else if (_object.hasComponent("combobox"))
			convertUiObjectComboBox(_co, _object);
		else if (_object.hasComponent("image"))
			convertUiObjectImage(_co, _object);
		else if (_object.hasComponent("label"))
			convertUiObjectLabel(_co, _object);
		else if (_object.hasComponent("root"))
			convertUiObjectRoot(_co, _object);
		else if (_object.hasComponent("rotary"))
			convertUiObjectRotary(_co, _object);
		else if (_object.hasComponent("textbutton"))
			convertUiObjectTextButton(_co, _object);
		else
			LOG("Unknown component type");

		if (getId(_object) == "container-patchmanager")
		{
			// create exactly one child, for the patchmanager
			ConvertedObject pmCo;
			pmCo.tag = "template";
			pmCo.attribs.set("src", "patchmanager");
			_co.children.emplace_back(std::move(pmCo));
			return true;
		}

		for (const auto& child : _object.getChildren())
		{
			ConvertedObject childCo;
			if (convertUiObject(childCo, *child))
				_co.children.emplace_back(std::move(childCo));
		}

		return true;
	}

	void SkinConverter::writeRmlFile(const std::string& _fileName)
	{
		std::stringstream ss;
		ss << "<rml>\n";
		ss << "\t<head>\n";
		ss << "\t\t" << R"(<link type="text/template" href="tus_patchmanager.rml"/>)" << '\n';
		ss << "\t\t" << R"(<link type="text/rcss" href="tus_default.rcss"/>)" << '\n';
		ss << "\t\t" << R"(<link type="text/rcss" href="tus_juceskin.rcss"/>)" << '\n';

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

		if (!_object.hasComponent("root"))
			_co.classes.emplace_back("jucePos");

		auto param = _object.getProperty("parameter");
		if (!param.empty())
			_co.attribs.set("param", param);

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
	}

	void SkinConverter::convertUiObjectButton(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		const auto imageName = createSpritesheet(_object);

		createImageStyle(imageName, {});
		createImageStyle(imageName, {"checked"});
		createImageStyle(imageName, {"checked", "active"});
		createImageStyle(imageName, {"checked", "hover"});
		createImageStyle(imageName, {"hover"});

		_co.classes.emplace_back("juceButton");
		_co.classes.emplace_back(imageName);
	}

	void SkinConverter::convertUiObjectComboBox(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "combo";
		genericUI::ComboboxStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = createTextStyle(style);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.push_back(className.substr(1));

		_co.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));
	}

	void SkinConverter::convertUiObjectImage(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "img";
		_co.attribs.set("src", _object.getProperty("texture") + ".png");
	}

	void SkinConverter::convertUiObjectLabel(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "div";
		genericUI::LabelStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = createTextStyle(style);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.emplace_back("juceLabel");
		_co.classes.push_back(className.substr(1));

		_co.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));
	}

	void SkinConverter::convertUiObjectRoot(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "body";
		_co.attribs.set("data-model", "partCurrent");
	}

	void SkinConverter::convertUiObjectRotary(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		genericUI::RotaryStyle style(m_editor);
		(static_cast<genericUI::UiObjectStyle&>(style)).apply(m_editor, _object);

		if (style.getStyle() == genericUI::RotaryStyle::Style::Rotary)
		{
			const auto imageName = createSpritesheet(_object);
			const auto styleName = createKnobStyle(imageName);

			_co.tag = "knob";
			_co.classes.emplace_back("juceRotary");
			_co.classes.emplace_back(styleName.substr(1));
		}
	}

	void SkinConverter::convertUiObjectTextButton(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "div";
		genericUI::TextButtonStyle style(m_editor);
		static_cast<genericUI::UiObjectStyle&>(style).apply(m_editor, _object);

		const auto className = createTextStyle(style);

		// TODO: vertical text alignment, this always centers the text vertically for now
		_co.style.add("line-height", _object.getProperty("height") + "dp");

		_co.classes.emplace_back("juceTextButton");
		_co.classes.push_back(className.substr(1));

		_co.innerText = Rml::StringUtilities::EncodeRml(_object.getProperty("text"));
	}

	std::string SkinConverter::getId(const genericUI::UiObject& _object)
	{
		const auto& id = _object.getName();
		if (id.empty())
			return {};
		auto it = m_idReplacements.find(id);
		return it != m_idReplacements.end() ? it->second : id;
	}

	std::string SkinConverter::createTextStyle(const genericUI::UiObjectStyle& _style)
	{
		CoStyle style;

		const auto& fontName = _style.getFontName();
		if (!fontName.empty())
			style.add("font-family", fontName);

		const auto fontSize = _style.getTextHeight();
		style.add("font-size", std::to_string(fontSize) + "dp");

		if (_style.getBold())
			style.add("font-weight", "bold");

		if (_style.getItalic())
			style.add("font-style", "italic");

		juce::Justification alignH = _style.getAlign().getOnlyHorizontalFlags();

		if (alignH == juce::Justification::centred)
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

		return addStyle(".text", style);
	}

	std::string SkinConverter::createImageStyle(const std::string& _imageName, const std::vector<std::string>& _states)
	{
		auto styleName = "." + _imageName;

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
		
		style.add("decorator", "image(" + spriteName + " contain)");

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
		const auto name = _prefix + "Style" + std::to_string(m_styles.size());
		m_styles.insert({ name, _style });
		return name;
	}

	std::string SkinConverter::createSpritesheet(const genericUI::UiObject& _object)
	{
		const auto imageName = _object.getProperty("texture");

		if (m_spritesheets.find(imageName) != m_spritesheets.end())
			return imageName; // already created

		const auto* drawable = dynamic_cast<const juce::DrawableImage*>(m_editor.getImageDrawable(imageName));
		assert(drawable);
		if (!drawable)
			throw std::runtime_error("Failed to find image drawable for '" + imageName + "'");

		const auto tileSizeX = _object.getPropertyInt("tileSizeX");
		const auto tileSizeY = _object.getPropertyInt("tileSizeY");

		const auto& image = drawable->getImage();

		const auto w = image.getWidth();
		const auto h = image.getHeight();

		struct Sprite
		{
			int page = -1;
			Rml::Rectanglei rect;
		};

		std::vector<Sprite> sprites;

		auto maxTextureWidth = std::max(tileSizeX, g_maxTextureWidth);
		auto maxTextureHeight = std::max(tileSizeY, g_maxTextureHeight);

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

				const auto filename = m_outputPath + imageName + "_page" + std::to_string(i) + ".png";
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
		if (_object.hasComponent("button"))
		{
			// create sprites for button states
			addIndex("default", _object.getPropertyInt("normalImage", -1));
			addIndex("hover", _object.getPropertyInt("overImage", -1));
			addIndex("active", _object.getPropertyInt("downImage", -1));
			addIndex("checked", _object.getPropertyInt("normalImageOn", -1));
			addIndex("checked-hover", _object.getPropertyInt("overImageOn", -1));
			addIndex("checked-active", _object.getPropertyInt("downImageOn", -1));
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

		m_spritesheets.insert({ imageName, std::move(spritesheet) });

		for (size_t i=0; i<pageSpritesheets.size(); ++i)
		{
			auto& ss = pageSpritesheets[i];
			if (ss.properties.size() <= 1)
				continue;
			m_spritesheets.insert({ imageName + "_page" + std::to_string(i), std::move(ss) });
		}

		return imageName;
	}

	bool SkinConverter::spriteExists(const std::string& _spriteName) const
	{
		for (const auto& spritesheet : m_spritesheets)
		{
			const auto& props = spritesheet.second.properties;

			if (props.find(_spriteName) != props.end())
				return true;
		}
		return false;
	}
}
