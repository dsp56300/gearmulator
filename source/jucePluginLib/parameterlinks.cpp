#include "parameterlinks.h"

#include "controller.h"

namespace pluginLib
{
	ParameterLinks::ParameterLinks(Controller& _controller) : m_controller(_controller)
	{
	}

	bool ParameterLinks::add(Parameter* _source, Parameter* _dest)
	{
		if(!_source || !_dest)
			return false;

		if(_source == _dest)
			return false;

		const auto it = m_parameterLinks.find(_source);

		if(it != m_parameterLinks.end())
			return it->second->add(_dest);

		m_parameterLinks.insert({_source, std::make_unique<ParameterLink>(_source, _dest)});
		m_destToSource[_dest].insert(_source);

		_dest->setLinkState(Target);

		return true;
	}

	bool ParameterLinks::remove(const Parameter* _source, Parameter* _dest)
	{
		const auto it = m_parameterLinks.find(_source);
		if(it == m_parameterLinks.end())
			return false;
		if(!it->second->remove(_dest))
			return false;
		if(it->second->empty())
			m_parameterLinks.erase(it);

		auto& sources = m_destToSource[_dest];

		sources.erase(_source);

		if(sources.empty())
		{
			m_destToSource.erase(_dest);
			_dest->clearLinkState(Target);
		}

		return true;
	}

	ParameterLinkType ParameterLinks::getRegionLinkType(const std::string& _regionId, const uint8_t _part) const
	{
		const auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto itRegion = regions.find(_regionId);

		if(itRegion == regions.end())
			return None;

		uint32_t totalCount = 0;
		uint32_t sourceCount = 0;
		uint32_t targetCount = 0;

		for (const auto& [name, desc] : itRegion->second.getParams())
		{
			const auto* parameter = m_controller.getParameter(name, _part);

			if(!parameter)
				continue;

			++totalCount;

			const auto state = parameter->getLinkState();

			if(state & ParameterLinkType::Source)	++sourceCount;
			if(state & ParameterLinkType::Target)	++targetCount;
		}

		ParameterLinkType result = None;

		if(sourceCount > (totalCount>>1))
			result = static_cast<ParameterLinkType>(result | ParameterLinkType::Source);
		if(targetCount > (totalCount>>1))
			result = static_cast<ParameterLinkType>(result | ParameterLinkType::Target);

		return result;
	}

	bool ParameterLinks::linkRegion(const std::string& _regionId, const uint8_t _partSource, const uint8_t _partDest)
	{
		const auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto itRegion = regions.find(_regionId);

		if(itRegion == regions.end())
			return false;

		const RegionLink link{_regionId, _partSource, _partDest};

		if(!m_linkedRegions.insert(link).second)
			return false;

		return updateRegionLinks(itRegion->second, _partSource, _partDest, true);
	}

	bool ParameterLinks::unlinkRegion(const std::string& _regionId, const uint8_t _partSource, const uint8_t _partDest)
	{
		const RegionLink link{_regionId, _partSource, _partDest};

		if(!m_linkedRegions.erase(link))
			return false;

		const auto& regions = m_controller.getParameterDescriptions().getRegions();

		const auto itRegion = regions.find(_regionId);

		if(itRegion == regions.end())
			return false;

		return updateRegionLinks(itRegion->second, _partSource, _partDest, false);
	}

	bool ParameterLinks::isRegionLinked(const std::string& _regionId, const uint8_t _partSource, const uint8_t _partDest) const
	{
		const RegionLink link{_regionId, _partSource, _partDest};
		return m_linkedRegions.find(link) != m_linkedRegions.end();
	}

	bool ParameterLinks::updateRegionLinks(const ParameterRegion& _region, const uint8_t _partSource, const uint8_t _partDest, const bool _enableLink)
	{
		bool res = false;

		for(const auto& [name, desc] : _region.getParams())
		{
			auto* paramSource = m_controller.getParameter(name, _partSource);
			auto* paramDest = m_controller.getParameter(name, _partDest);

			if(_enableLink)
				res |= add(paramSource, paramDest);
			else
				res |= remove(paramSource, paramDest);
		}

		return res;
	}
}
