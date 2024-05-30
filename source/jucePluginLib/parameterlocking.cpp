#include "parameterlocking.h"

#include <cassert>

#include "controller.h"

namespace pluginLib
{
	ParameterLocking::ParameterLocking(Controller& _controller) : m_controller(_controller)
	{
	}

	bool ParameterLocking::lockRegion(const std::string& _id)
	{
		if(m_lockedRegions.find(_id) != m_lockedRegions.end())
			return true;

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto it = regions.find(_id);

		if(it == regions.end())
			return false;

		m_lockedRegions.insert(_id);

		setParametersLocked(it->second, true);

		return true;
	}

	bool ParameterLocking::unlockRegion(const std::string& _id)
	{
		if(!m_lockedRegions.erase(_id))
			return false;

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto it = regions.find(_id);

		if(it == regions.end())
			return false;

		m_lockedRegions.erase(_id);

		setParametersLocked(it->second, false);
		return true;
	}

	const std::set<std::string>& ParameterLocking::getLockedRegions() const
	{
		return m_lockedRegions;
	}

	bool ParameterLocking::isRegionLocked(const std::string& _id)
	{
		return m_lockedRegions.find(_id) != m_lockedRegions.end();
	}

	std::unordered_set<std::string> ParameterLocking::getLockedParameterNames() const
	{
		if(m_lockedRegions.empty())
			return {};

		std::unordered_set<std::string> result;

		auto& regions = m_controller.getParameterDescriptions().getRegions();

		for (const auto& name : m_lockedRegions)
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
		const auto paramNames = getLockedParameterNames();

		std::unordered_set<const Parameter*> results;

		for (const auto& paramName : paramNames)
		{
			const auto idx = m_controller.getParameterIndexByName(paramName);
			assert(idx != Controller::InvalidParameterIndex);
			const auto* p = m_controller.getParameter(idx, _part);
			assert(p != nullptr);
			results.insert(p);
		}

		return results;
	}

	bool ParameterLocking::isParameterLocked(const std::string& _name) const
	{
		auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto& lockedRegions = getLockedRegions();

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

	void ParameterLocking::setParametersLocked(const ParameterRegion& _parameterRegion, const bool _locked) const
	{
		for (const auto& param : _parameterRegion.getParams())
		{
			// if a region is unlocked but other regions still lock the same parameter, do nothing
			if(!_locked && isParameterLocked(param.first))
				continue;

			const auto idx = m_controller.getParameterIndexByName(param.first);

			for(uint8_t part=0; part<16; ++part)
			{
				if(auto* p = m_controller.getParameter(idx, part))
					p->setLocked(_locked);
			}
		}
	}
}
