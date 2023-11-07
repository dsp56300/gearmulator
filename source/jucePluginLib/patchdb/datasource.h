#pragma once

#include <string>

#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct DataSource
	{
		SourceType type = SourceType::Invalid;
		DataSourceOrigin origin = DataSourceOrigin::Invalid;
		std::string name;
		uint32_t bank = g_invalidBank;
//		uint32_t program = g_invalidProgram;

		virtual ~DataSource() = default;

		bool operator == (const DataSource& _ds) const
		{
			return type == _ds.type && name == _ds.name && bank == _ds.bank;//&& program == _ds.program;
		}

		bool operator != (const DataSource& _ds) const
		{
			return !(*this == _ds);
		}

		bool operator < (const DataSource& _ds) const
		{
//			if (parent < _ds.parent)	return true;
//			if (parent > _ds.parent)	return false;

			if (type < _ds.type)		return true;
			if (type > _ds.type)		return false;

			if (name < _ds.name)		return true;
			if (name > _ds.name)		return false;

			if (bank < _ds.bank)		return true;
			if (bank > _ds.bank)		return false;
			/*
			if (program < _ds.program)	return true;
			if (program > _ds.program)	return false;
			*/
			return false;
		}

		bool operator > (const DataSource& _ds) const
		{
			return _ds < *this;
		}

		std::string toString() const;
	};

	struct DataSourceNode final : DataSource
	{
		DataSourceNode() = default;
		DataSourceNode(const DataSourceNode&) = delete;

		DataSourceNode(const DataSource& _ds);

		DataSourceNode(DataSourceNode&& _other) noexcept;

		~DataSourceNode() override;

		DataSourceNode& operator = (const DataSourceNode&) = delete;

		DataSourceNode& operator = (DataSourceNode&& _other) noexcept;

		auto& getParent() const { return m_parent; }
		auto hasParent() const { return getParent() != nullptr; }
		const auto& getChildren() const { return m_children; }

		void setParent(const DataSourceNodePtr& _parent);

		DataSourceNode& operator = (const DataSource& _source);

		bool isChildOf(const DataSourceNode* _ds) const;

	private:
		DataSourceNodePtr m_parent;
		std::set<DataSourceNode*> m_children;
	};
}
