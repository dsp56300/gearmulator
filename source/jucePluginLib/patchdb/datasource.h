#pragma once

#include <string>

#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct DataSource
	{
		SourceType type = SourceType::Invalid;
		std::string name;
		uint32_t bank = g_invalidBank;
//		uint32_t program = g_invalidProgram;

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

	struct DataSourceNode : DataSource
	{
		DataSourceNodePtr parent;

		DataSourceNode& operator = (const DataSource& _source)
		{
			static_cast<DataSource&>(*this) = _source;
			return *this;
		}

		bool isChildOf(const DataSourceNode* _ds) const
		{
			auto node = this;

			while(node)
			{
				if (_ds == node)
					return true;
				node = node->parent.get();
			}
			return false;
		}
	};
}
