#include "pluginDataModel.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "jucePluginLib/pluginVersion.h"

#include "RmlUi/Core/Context.h"

namespace jucePluginEditorLib
{
	PluginDataModel::PluginDataModel(const Editor& _editor, Rml::Context& _context, const std::function<void(PluginDataModel&)>& _bindCallback)
		: m_context(_context)
		, m_name(_editor.getProcessor().getProperties().name)
		, m_vendor(_editor.getProcessor().getProperties().vendor)
		, m_fourCC(_editor.getProcessor().getProperties().plugin4CC)

		, m_versionString(pluginLib::Version::getVersionString())
		, m_versionNumber(pluginLib::Version::getVersionNumber())
		, m_versionDate(pluginLib::Version::getVersionDate())
		, m_versionTime(pluginLib::Version::getVersionTime())
		, m_versionDateTime(pluginLib::Version::getVersionDateTime())
	{
		auto dmc = _context.CreateDataModel(getModelName());

		m_dataModelConstructor = &dmc;

		dmc.Bind("name", &m_name);
		dmc.Bind("vendor", &m_vendor);
		dmc.Bind("fourCC", &m_fourCC);

		dmc.Bind("versionString", &m_versionString);
		dmc.Bind("versionNumber", &m_versionNumber);
		dmc.Bind("versionDate", &m_versionDate);
		dmc.Bind("versionTime", &m_versionTime);
		dmc.Bind("versionDateTime", &m_versionDateTime);

		_bindCallback(*this);

		m_handle = dmc.GetModelHandle();

		m_dataModelConstructor = nullptr;
	}

	std::string PluginDataModel::getModelName()
	{
		return "plugin";
	}

	void PluginDataModel::set(const std::string& _key, const std::string& _value)
	{
		if (m_dataModelConstructor)
		{
			if (m_keyValues.find(_key) == m_keyValues.end())
			{
				m_keyValues.insert({ _key, _value });

				m_dataModelConstructor->BindFunc(_key, [this, _key](Rml::Variant& _variant)
				{
					const auto it = m_keyValues.find(_key);
					if (it == m_keyValues.end())
						_variant = std::string();
					else
						_variant = it->second;
				}, [](const Rml::Variant&) {});
			}
		}

		const auto it = m_keyValues.find(_key);

		if (it == m_keyValues.end())
			assert(false && "key has to be registered before data model handle is retrieved");

		if (it->second == _value)
			return;

		it->second = _value;

		m_handle.DirtyVariable(_key);
	}

	void PluginDataModel::setFunc(const std::string& _key, const FuncGet& _get, const FuncSet& _set) const
	{
		assert(m_dataModelConstructor && "setFunc can only be called before the data model handle is retrieved");
		m_dataModelConstructor->BindFunc(_key, 
			[this, _get](Rml::Variant& _variant)
			{
				_variant = _get();
			}, 
			[this, _set](const Rml::Variant& _variant)
			{
				_set(_variant.Get<std::string>(m_context.GetCoreInstance()));
			}
		);
	}
}
