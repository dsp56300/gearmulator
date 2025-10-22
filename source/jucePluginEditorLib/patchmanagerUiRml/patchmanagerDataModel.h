#pragma once

#include <string>

#include "RmlUi/Core/DataModelHandle.h"

namespace Rml
{
	class Context;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;

	class PatchManagerDataModel
	{
	public:
		explicit PatchManagerDataModel(Rml::Context& _context);

		void setStatus(const std::string& _status);
		void setPatchName(const std::string& _name);
		void setPatchDatasource(const std::string& _name);
		void setPatchCategories(const std::string& _categories);
		void setPatchTags(const std::string& _tags);

	private:
		template<typename T> void updateVariable(const std::string& _name, T& _currentValue, const T& _newValue);

		Rml::Context& m_context;
		Rml::DataModelHandle m_handle;

		std::string m_status;
		std::string m_patchName;
		std::string m_patchDatasource;
		std::string m_patchCategories;
		std::string m_patchTags;
	};
}
