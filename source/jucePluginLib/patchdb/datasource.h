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
		Timestamp timestamp;
		std::set<PatchPtr> patches;

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

			if (bank < _ds.bank)		return true;
			if (bank > _ds.bank)		return false;

			/*
			if (program < _ds.program)	return true;
			if (program > _ds.program)	return false;
			*/

			if (name < _ds.name)		return true;
			if (name > _ds.name)		return false;

			return false;
		}

		bool operator > (const DataSource& _ds) const
		{
			return _ds < *this;
		}

		std::string toString() const;
	};

	struct DataSourceNode final : DataSource, std::enable_shared_from_this<DataSourceNode>
	{
		DataSourceNode() = default;
		DataSourceNode(const DataSourceNode&) = delete;

		explicit DataSourceNode(const DataSource& _ds);
		explicit DataSourceNode(DataSourceNode&&) = delete;

		~DataSourceNode() override;

		DataSourceNode& operator = (const DataSourceNode&) = delete;
		DataSourceNode& operator = (DataSourceNode&& _other) noexcept = delete;

		auto& getParent() const { return m_parent; }
		auto hasParent() const { return getParent() != nullptr; }
		const auto& getChildren() const { return m_children; }

		void setParent(const DataSourceNodePtr& _parent);

		bool isChildOf(const DataSourceNode* _ds) const;
		void removeAllChildren();

	private:
		DataSourceNodePtr m_parent;

		std::vector<std::weak_ptr<DataSourceNode>> m_children;
	};
}
