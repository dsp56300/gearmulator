#include "datasource.h"

#include <sstream>

namespace pluginLib::patchDB
{
	std::string DataSource::toString() const
	{
		std::stringstream ss;

		ss << "type|" << patchDB::toString(type);
		ss << "|name|" << name;
		if (bank != g_invalidBank)
			ss << "|bank|" << bank;
//		if (program != g_invalidProgram)
//			ss << "|prog|" << program;
		return ss.str();
	}

	DataSourceNode::DataSourceNode(const DataSource& _ds): DataSource(_ds)
	{
	}

	DataSourceNode::DataSourceNode(DataSourceNode&& _other) noexcept: DataSource(_other), m_parent(std::move(_other.m_parent)), m_children(std::move(_other.m_children))
	{
	}

	DataSourceNode::~DataSourceNode()
	{
		setParent(nullptr);

		while (!m_children.empty())
		{
			const auto child = *m_children.begin();
			child->setParent(nullptr);
		}
	}

	DataSourceNode& DataSourceNode::operator=(DataSourceNode&& _other) noexcept
	{
		static_cast<DataSource&>(*this) = static_cast<DataSource&>(_other);

		m_parent = std::move(_other.m_parent);
		m_children = std::move(_other.m_children);

		return *this;
	}

	void DataSourceNode::setParent(const DataSourceNodePtr& _parent)
	{
		if (getParent() == _parent)
			return;

		if(m_parent)
		{
			m_parent->m_children.erase(this);
		}

		m_parent = _parent;

		if(_parent)
		{
			_parent->m_children.insert(this);
		}
	}

	DataSourceNode& DataSourceNode::operator=(const DataSource& _source)
	{
		static_cast<DataSource&>(*this) = _source;
		return *this;
	}

	bool DataSourceNode::isChildOf(const DataSourceNode* _ds) const
	{
		auto node = this;

		while(node)
		{
			if (_ds == node)
				return true;
			node = node->m_parent.get();
		}
		return false;
	}
}
