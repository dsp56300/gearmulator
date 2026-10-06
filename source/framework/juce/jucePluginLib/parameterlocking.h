#pragma once

#include <set>
#include <string>
#include <unordered_set>
#include <array>
#include <atomic>

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

		// safe to call from any thread, for example the audio thread
		bool hasLockedRegions(const uint8_t _part) const { return _part < m_lockedRegionCounts.size() && m_lockedRegionCounts[_part] > 0; }

	private:
		void setParametersLocked(const ParameterRegion& _parameterRegion, uint8_t _part, bool _locked) const;

		std::set<std::string>& getLockedRegions(const uint8_t _part) { return m_lockedRegions[_part]; }

		Controller& m_controller;

		std::array<std::set<std::string>,16> m_lockedRegions;
		std::array<std::atomic<uint32_t>,16> m_lockedRegionCounts{};
	};
}
