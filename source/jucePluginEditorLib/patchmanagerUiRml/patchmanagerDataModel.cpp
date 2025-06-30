#include "patchmanagerDataModel.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"

namespace jucePluginEditorLib::patchManagerRml
{
	PatchManagerDataModel::PatchManagerDataModel(Rml::Context& _context) : m_context(_context)
	{
		auto dm = m_context.CreateDataModel("patchmanager");

		dm.Bind("status", &m_status);
		dm.Bind("patchName", &m_patchName);
		dm.Bind("patchCategories", &m_patchCategories);
		dm.Bind("patchTags", &m_patchTags);

		m_handle = dm.GetModelHandle();
	}

	template <typename T> void PatchManagerDataModel::updateVariable(const std::string& _name, T& _currentValue, const T& _newValue)
	{
		if (_currentValue == _newValue)
			return;
		_currentValue = _newValue;
		m_handle.DirtyVariable(_name);
	}

	void PatchManagerDataModel::setStatus(const std::string& _status)
	{
		updateVariable("status", m_status, _status);
	}

	void PatchManagerDataModel::setPatchName(const std::string& _name)
	{
		updateVariable("patchName", m_patchName, _name);
	}

	void PatchManagerDataModel::setPatchCategories(const std::string& _categories)
	{
		updateVariable("patchCategories", m_patchCategories, _categories);
	}

	void PatchManagerDataModel::setPatchTags(const std::string& _tags)
	{
		updateVariable("patchTags", m_patchTags, _tags);
	}
}
