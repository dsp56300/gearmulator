#include "skinConverter.h"

#include <fstream>

#include "convertedObject.h"
#include "dsp56kEmu/logging.h"
#include "juceUiLib/uiObject.h"

namespace rmlPlugin::skinConverter
{
	SkinConverter::SkinConverter(const genericUI::UiObject& _root, std::string _outputPath, std::string _rmlFileName, std::string _rcssFileName, std::map<std::string, std::string>&& _idReplacements)
		: m_rootObject(_root)
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

		setDefaultProperties(_co, _object);

		if (_object.hasComponent("root"))
			convertUiObjectRoot(_co, _object);
		else if (_object.hasComponent("image"))
			convertUiObjectImage(_co, _object);
		else if (_object.hasComponent("combobox"))
			convertUiObjectComboBox(_co, _object);
		else if (_object.hasComponent("rotary"))
			convertUiObjectRotary(_co, _object);
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
		for (const auto& [name, style] : m_styles)
		{
			style.write(_out, name, _depth);
		}
	}

	void SkinConverter::setDefaultProperties(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.set(getId(_object), _object);
		_co.classes.emplace_back("jucePos");

		auto param = _object.getProperty("parameter");
		if (!param.empty())
			_co.attribs.set("param", param);
	}

	void SkinConverter::convertUiObjectComboBox(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "combo";
	}

	void SkinConverter::convertUiObjectImage(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "img";
		_co.attribs.set("src", _object.getProperty("texture") + ".png");
	}

	void SkinConverter::convertUiObjectRoot(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
		_co.tag = "body";
		_co.attribs.set("data-model", "partCurrent");
	}

	void SkinConverter::convertUiObjectRotary(ConvertedObject& _co, const genericUI::UiObject& _object)
	{
	}

	std::string SkinConverter::getId(const genericUI::UiObject& _object)
	{
		const auto id = _object.getName();
		if (id.empty())
			return {};
		auto it = m_idReplacements.find(id);
		return it != m_idReplacements.end() ? it->second : id;
	}
}
