#pragma once

#include "RmlUi/Core/DataModelHandle.h"

namespace jucePluginEditorLib
{
	class Editor;

	class PluginDataModel
	{
	public:
		PluginDataModel(const Editor& _editor, Rml::Context& _context, const std::function<void(PluginDataModel&)>& _bindCallback);

		void registerKey(const std::string& _key, const std::string& _value);

		void set(const std::string& _key, const std::string& _value);

	private:
		std::string m_name;
		std::string m_vendor;
		std::string m_fourCC;

		std::string m_versionName;
		std::string m_versionString;
		uint32_t m_versionNumber;
		std::string m_versionDate;
		std::string m_versionTime;
		std::string m_versionDateTime;

		Rml::DataModelConstructor* m_dataModelConstructor = nullptr;
		Rml::DataModelHandle m_handle;

		std::unordered_map<std::string, std::string> m_keyValues;
	};
}
