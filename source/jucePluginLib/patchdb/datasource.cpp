#include "datasource.h"

#include <sstream>
#include <memory>

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

	DataSourceNode::DataSourceNode(const DataSource& _ds) : DataSource(_ds)
	{
	}

	DataSourceNode::~DataSourceNode()
	{
		setParent(nullptr);
		removeAllChildren();
	}

	void DataSourceNode::setParent(const DataSourceNodePtr& _parent)
	{
		if (getParent() == _parent)
			return;

		if(m_parent)
		{
			// we MUST NOT create a new ptr to this here as we may be called from our destructor, in which case there shouldn't be a pointer in there anyway
			for(size_t i=0; i<m_parent->m_children.size(); ++i)
			{
				auto& child = m_parent->m_children[i];
				auto ptr = child.lock();
				if (ptr && ptr.get() == this)
				{
					m_parent->m_children.erase(m_parent->m_children.begin() + i);
					break;
				}
			}
		}

		m_parent = _parent;

		if(_parent)
			_parent->m_children.emplace_back(shared_from_this());
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

	void DataSourceNode::removeAllChildren()
	{
		while(!m_children.empty())
		{
			const auto& c = m_children.back().lock();
			if (c)
				c->setParent(nullptr);
			else
				m_children.pop_back();
		}
	}
}
