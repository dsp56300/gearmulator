#pragma once

#include "88lib/deviceModel.h"
#include "88lib/mt32/memory.h"
#include "88lib/mt32/mt32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu88Lib::mt32
{
	// The preset sounds, read from the boards' ROMs: the LA board's timbres of groups a, b and r from its control ROM,
	// whole, the CM-32P's internal tones' names from its first wave ROM and a card's from the card. A part's timbre
	// temp has what it plays, but for a preset only once a data request read it, and the CM-32P answers none.
	class PresetCatalog
	{
	public:
		using Timbre = std::array<uint8_t, g_timbreSize>;

		// A preset timbre by the group and number a patch selects it with: 0 a, 1 b, 3 r. Group 2, i, is the board's
		// timbre memory, which the ROM does not have. Empty or null when there is none.
		const std::string& timbre(uint8_t _group, uint8_t _number) const;
		// Its 246 bytes as the board loads them into a part's timbre temp
		const Timbre* timbreData(uint8_t _group, uint8_t _number) const;
		// Every preset timbre with its group and number, for Memory::addPreset()
		std::vector<std::pair<TimbreSource, Timbre>> allTimbres() const;
		// A PCM tone by the media and number a patch selects it with: 0 internal, 1 the card. Empty when there is no
		// name: no card, or an unused place.
		const std::string& pcmTone(uint8_t _media, uint8_t _number) const;

		bool hasTimbres() const { return !m_timbres[0].empty(); }
		bool hasPcmTones() const { return !m_pcmTones[0].empty(); }

		// From the LA control ROM as the 8095 sees it: false when neither layout fits
		bool readLaControl(const std::vector<uint8_t>& _control);
		// From the tone table of a decoded wave image, the CM-32P's first wave ROM or a card
		void readPcmTones(uint8_t _media, const uint8_t* _decoded, size_t _size);

	private:
		std::array<std::vector<Timbre>, 4> m_timbres;
		std::array<std::vector<std::string>, 4> m_timbreNames;
		std::array<std::vector<std::string>, 2> m_pcmTones;
	};

	// The presets of a model's boards from the ROMs the player found, and a PCM card image as the player loads it
	// (empty for none). Null when the model has no board of this dialect; _error says why presets are missing.
	std::shared_ptr<const PresetCatalog> loadPresetCatalog(DeviceModel _model, const std::vector<uint8_t>& _pcmCard,
		std::string& _error);
}
