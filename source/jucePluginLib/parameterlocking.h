#pragma once

#include <set>
#include <string>
#include <unordered_set>

#include "parameterregion.h"

namespace pluginLib
{
	class Parameter;
	class Controller;

	class ParameterLocking
	{
	public:
		explicit ParameterLocking(Controller& _controller);

		bool lockRegion(const std::string& _id);
		bool unlockRegion(const std::string& _id);
		const std::set<std::string>& getLockedRegions() const;
		bool isRegionLocked(const std::string& _id);
		std::unordered_set<std::string> getLockedParameterNames() const;
		std::unordered_set<const Parameter*> getLockedParameters(uint8_t _part) const;
		bool isParameterLocked(const std::string& _name) const;

	private:
		void setParametersLocked(const ParameterRegion& _parameterRegion, bool _locked);

		Controller& m_controller;

		std::set<std::string> m_lockedRegions;
	};
}
