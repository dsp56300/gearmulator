#include "datasource.h"

#include <algorithm>
#include <sstream>
#include <memory>

#include "patch.h"

namespace pluginLib::patchDB
{
	bool DataSource::createConsecutiveProgramNumbers()
	{
		if(patches.empty())
			return false;

		// note that this does NOT sort the patches member, it is a set that cannot be sorted, we only generate consecutive program numbers here

		std::vector patchesVector(patches.begin(), patches.end());

		sortByProgram(patchesVector);

		return createConsecutiveProgramNumbers(patchesVector);
	}

	bool DataSource::createConsecutiveProgramNumbers(const std::vector<PatchPtr>& _patches)
	{
		bool dirty = false;
		uint32_t program = 0;

		for (const auto& patch : _patches)
		{
			const auto p = program++;

			if(patch->program == p)
				continue;

			patch->program = p;
			dirty = true;
		}

		return dirty;
	}

	bool DataSource::makeSpaceForNewPatches(const uint32_t _insertPosition, const uint32_t _count) const
	{
		bool dirty = true;

		for (const auto& patch : patches)
		{
			if(patch->program >= _insertPosition)
			{
				patch->program += _count;
				dirty = true;
			}
		}
		return dirty;
	}

	std::pair<uint32_t, uint32_t> DataSource::getProgramNumberRange() const
	{
		if(patches.empty())
			return {g_invalidProgram, g_invalidProgram};

		uint32_t min = std::numeric_limits<uint32_t>::max();
		uint32_t max = std::numeric_limits<uint32_t>::min();

		for (const auto& patch : patches)
		{
			min = std::min(patch->program, min);
			max = std::max(patch->program, max);
		}

		return {min, max};
	}

	uint32_t DataSource::getMaxProgramNumber() const
	{
		return getProgramNumberRange().second;
	}

	void DataSource::sortByProgram(std::vector<PatchPtr>& _patches)
	{
		std::sort(_patches.begin(), _patches.end(), [&](const PatchPtr& _a, const PatchPtr& _b)
		{
			return _a->program < _b->program;
		});
	}

	bool DataSource::contains(const PatchPtr& _patch) const
	{
		return patches.find(_patch) != patches.end();
	}

	bool DataSource::movePatchesTo(const uint32_t _position, const std::vector<PatchPtr>& _patches)
	{
		std::vector patchesVec(patches.begin(), patches.end());
		sortByProgram(patchesVec);

		createConsecutiveProgramNumbers(patchesVec);

		uint32_t targetPosition = _position;

		// insert position has to be decremented by 1 for each patch that is reinserted that has a position less than the target position
		for (const auto& patch : _patches)
		{
			if(patch->program < _position)
				--targetPosition;
		}

		if(!remove(_patches))
			return false;

		patchesVec.assign(patches.begin(), patches.end());
		sortByProgram(patchesVec);

		if(targetPosition >= patchesVec.size())
			patchesVec.insert(patchesVec.end(), _patches.begin(), _patches.end());
		else
			patchesVec.insert(patchesVec.begin() + targetPosition, _patches.begin(), _patches.end());

		createConsecutiveProgramNumbers(patchesVec);
		
		for (const auto& patch : _patches)
			patches.insert(patch);

		return true;
	}

	bool DataSource::remove(const PatchPtr& _patch)
	{
		return patches.erase(_patch);
	}

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
			for(uint32_t i=0; i<static_cast<uint32_t>(m_parent->m_children.size()); ++i)
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
