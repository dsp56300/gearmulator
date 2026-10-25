#include "parameterlocking.h"

#include <cassert>

#include "controller.h"

#include "baseLib/binarystream.h"

namespace pluginLib
{
	ParameterLocking::ParameterLocking(Controller& _controller) : m_controller(_controller)
	{
	}

	bool ParameterLocking::lockRegion(const uint8_t _part, const std::string& _id)
	{
		auto& lockedRegions = getLockedRegions(_part);

		if(lockedRegions.find(_id) != lockedRegions.end())
			return true;

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto it = regions.find(_id);

		if(it == regions.end())
			return false;

		lockedRegions.insert(_id);
		m_lockedRegionCounts[_part] = static_cast<uint32_t>(lockedRegions.size());

		setParametersLocked(it->second, _part, true);

		evRegionLockChanged(_part, _id, true);

		return true;
	}

	bool ParameterLocking::unlockRegion(const uint8_t _part, const std::string& _id)
	{
		auto& lockedRegions = getLockedRegions(_part);

		if(!lockedRegions.erase(_id))
			return false;

		m_lockedRegionCounts[_part] = static_cast<uint32_t>(lockedRegions.size());

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto it = regions.find(_id);

		if(it == regions.end())
			return false;

		lockedRegions.erase(_id);

		setParametersLocked(it->second, _part, false);

		evRegionLockChanged(_part, _id, false);

		return true;
	}

	const std::set<std::string>& ParameterLocking::getLockedRegions(const uint8_t _part) const
	{
		return m_lockedRegions[_part];
	}

	bool ParameterLocking::isRegionLocked(const uint8_t _part, const std::string& _id)
	{
		return m_lockedRegions[_part].find(_id) != m_lockedRegions[_part].end();
	}

	std::unordered_set<std::string> ParameterLocking::getLockedParameterNames(const uint8_t _part) const
	{
		if(m_lockedRegions[_part].empty())
			return {};

		std::unordered_set<std::string> result;

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		for (const auto& name : m_lockedRegions[_part])
		{
			const auto& it = regions.find(name);
			if(it == regions.end())
				continue;

			const auto& region = it->second;
			for (const auto& itParam : region.getParams())
				result.insert(itParam.first);
		}

		return result;
	}

	std::unordered_set<const Parameter*> ParameterLocking::getLockedParameters(const uint8_t _part) const
	{
		const auto paramNames = getLockedParameterNames(_part);

		std::unordered_set<const Parameter*> results;

		for (const auto& paramName : paramNames)
		{
			const auto* p = m_controller.getParameter(paramName, _part);
			assert(p != nullptr);
			results.insert(p);
		}

		return results;
	}

	bool ParameterLocking::isParameterLocked(const uint8_t _part, const std::string& _name) const
	{
		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto& lockedRegions = getLockedRegions(_part);

		for (const auto& region : lockedRegions)
		{
			const auto& it = regions.find(region);
			if(it == regions.end())
				continue;

			if(it->second.containsParameter(_name))
				return true;
		}
		return false;
	}

	void ParameterLocking::saveChunkData(baseLib::BinaryStream& _s) const
	{
		// written without locks too: loading a state unlocks what it does not lock
		baseLib::ChunkWriter cw(_s, "PLCK", 1);

		uint32_t count = 0;
		for (const auto& regions : m_lockedRegions)
			count += static_cast<uint32_t>(regions.size());

		_s.write(count);

		for(uint8_t part=0; part<m_lockedRegions.size(); ++part)
		{
			for (const auto& id : m_lockedRegions[part])
			{
				_s.write(part);
				_s.write(id);
			}
		}
	}

	void ParameterLocking::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("PLCK", 1, [this](baseLib::BinaryStream& _s, uint32_t/* _version*/)
		{
			std::array<std::set<std::string>,16> regions;

			const auto count = _s.read<uint32_t>();

			for(uint32_t i=0; i<count; ++i)
			{
				const auto part = _s.read<uint8_t>();
				auto id = _s.readString();
				if(part < regions.size())
					regions[part].insert(std::move(id));
			}

			for(uint8_t part=0; part<regions.size(); ++part)
			{
				// a copy, unlocking erases from it
				const auto locked = m_lockedRegions[part];

				for (const auto& id : locked)
				{
					if(regions[part].find(id) == regions[part].end())
						unlockRegion(part, id);
				}

				for (const auto& id : regions[part])
					lockRegion(part, id);
			}
		});
	}

	void ParameterLocking::setParametersLocked(const ParameterRegion& _parameterRegion, const uint8_t _part, const bool _locked) const
	{
		for (const auto& param : _parameterRegion.getParams())
		{
			// if a region is unlocked but other regions still lock the same parameter, do nothing
			if(!_locked && isParameterLocked(_part, param.first))
				continue;

			if(auto* p = m_controller.getParameter(param.first, _part))
				p->setLocked(_locked);
		}
	}
}
