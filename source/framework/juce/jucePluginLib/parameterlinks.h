#pragma once

#include <map>
#include <memory>

#include "parameterlink.h"
#include "types.h"

namespace pluginLib
{
	class ParameterRegion;
	class Controller;
	class Parameter;

	class ParameterLinks
	{
	public:
		ParameterLinks(Controller& _controller);

		bool add(Parameter* _source, Parameter* _dest, bool _applyCurrentSourceToTarget);
		bool remove(const Parameter* _source, Parameter* _dest);

		ParameterLinkType getRegionLinkType(const std::string& _regionId, uint8_t _part) const;

		bool linkRegion(const std::string& _regionId, uint8_t _partSource, uint8_t _partDest, bool _applyCurrentSourceToTarget);
		bool unlinkRegion(const std::string& _regionId, uint8_t _partSource, uint8_t _partDest);
		bool isRegionLinked(const std::string& _regionId, uint8_t _partSource, uint8_t _partDest) const;

		void saveChunkData(baseLib::BinaryStream& s) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

	private:
		struct RegionLink
		{
			std::string regionId;
			uint8_t sourcePart = 0;
			uint8_t destPart = 0;

			bool operator == (const RegionLink& _link) const
			{
				return sourcePart == _link.sourcePart && destPart == _link.destPart && regionId == _link.regionId;
			}

			bool operator < (const RegionLink& _link) const
			{
				if(sourcePart < _link.sourcePart)	return true;
				if(sourcePart > _link.sourcePart)	return false;
				if(destPart < _link.destPart)		return true;
				if(destPart > _link.destPart)		return false;
				if(regionId < _link.regionId)		return true;
				return false;
			}
		};

		bool updateRegionLinks(const ParameterRegion& _region, uint8_t _partSource, uint8_t _partDest, bool _enableLink, bool _applyCurrentSourceToTarget);

		Controller& m_controller;
		std::map<const Parameter*, std::unique_ptr<ParameterLink>> m_parameterLinks;
		std::map<const Parameter*, std::set<const Parameter*>> m_destToSource;
		std::set<RegionLink> m_linkedRegions;
	};
}
