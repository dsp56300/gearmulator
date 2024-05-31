#pragma once

#include <set>
#include <string>
#include <unordered_set>
#include <array>

#include "parameterregion.h"

namespace pluginLib
{
	class Parameter;
	class Controller;

	class ParameterLocking
	{
	public:
		explicit ParameterLocking(Controller& _controller);

		bool lockRegion(uint8_t _part, const std::string& _id);
		bool unlockRegion(uint8_t _part, const std::string& _id);
		const std::set<std::string>& getLockedRegions(uint8_t _part) const;
		bool isRegionLocked(uint8_t _part, const std::string& _id);
		std::unordered_set<std::string> getLockedParameterNames(uint8_t _part) const;
		std::unordered_set<const Parameter*> getLockedParameters(uint8_t _part) const;
		bool isParameterLocked(uint8_t _part, const std::string& _name) const;

	private:
		void setParametersLocked(const ParameterRegion& _parameterRegion, uint8_t _part, bool _locked) const;

		std::set<std::string>& getLockedRegions(const uint8_t _part) { return m_lockedRegions[_part]; }

		Controller& m_controller;

		std::array<std::set<std::string>,16> m_lockedRegions;
	};
}
