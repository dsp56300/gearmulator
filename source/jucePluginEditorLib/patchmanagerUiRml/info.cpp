#include "info.h"

#include "patchmanagerDataModel.h"
#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "jucePluginLib/patchdb/patch.h"

namespace jucePluginEditorLib::patchManagerRml
{
	Info::Info(const PatchManagerUiRml& _pm) : m_patchManager(_pm.getDB()), m_dataModel(*_pm.getEditor().getPatchManagerDataModel())
	{
	}

	void Info::setPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		if (!_patch)
		{
			clear();
			return;
		}

		if(_patch != m_patch)
		{
			m_patchManager.cancelSearch(m_searchHandle);

			pluginLib::patchDB::SearchRequest req;
			req.patch = _patch;

			m_searchHandle = m_patchManager.search(std::move(req));

			m_patch = _patch;
		}

		m_dataModel.setPatchName(_patch->getName());
		m_dataModel.setPatchDatasource(toText(_patch->source.lock()));
		m_dataModel.setPatchCategories(toText(_patch->getTags().get(pluginLib::patchDB::TagType::Category)));
		m_dataModel.setPatchTags(toText(_patch->getTags().get(pluginLib::patchDB::TagType::Tag)));
	}

	void Info::clear()
	{
		m_patch.reset();
		m_patchManager.cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;

		m_dataModel.setPatchName({});
		m_dataModel.setPatchDatasource({});
		m_dataModel.setPatchCategories({});
		m_dataModel.setPatchTags({});
	}

	std::string Info::toText(const pluginLib::patchDB::Tags& _tags)
	{
		const auto& tags = _tags.getAdded();
		std::stringstream ss;

		size_t i = 0;
		for (const auto& tag : tags)
		{
			if (i)
				ss << ", ";
			ss << tag;
			++i;
		}
		return ss.str();
	}

	std::string Info::toText(const pluginLib::patchDB::DataSourceNodePtr& _source)
	{
		if (!_source)
			return {};

		switch (_source->type)
		{
		case pluginLib::patchDB::SourceType::Invalid:
		case pluginLib::patchDB::SourceType::Count:
			return {};
		case pluginLib::patchDB::SourceType::Rom:
		case pluginLib::patchDB::SourceType::Folder:
		case pluginLib::patchDB::SourceType::LocalStorage:
			return _source->name;
		case pluginLib::patchDB::SourceType::File:
			{
				auto t = _source->name;
				const auto pos = t.find_last_of("\\/");
				if (pos != std::string::npos)
					return t.substr(pos + 1);
				return t;
			}
		}
		return {};
	}

	void Info::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if(_dirty.searches.find(m_searchHandle) == _dirty.searches.end())
			return;

		setPatch(m_patch);
	}
}
