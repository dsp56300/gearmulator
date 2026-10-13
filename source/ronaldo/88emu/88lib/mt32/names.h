#pragma once

#include "88lib/deviceModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu88Lib::mt32
{
	// The names of the preset sounds, read from the boards' ROMs: the LA board's timbres of groups a, b and r from its
	// control ROM, the CM-32P's internal tones from its first wave ROM and a card's tones from the card. A part's
	// timbre temp has the name of what it plays, but for a preset only once a data request read it, and the CM-32P
	// answers none.
	class NameCatalog
	{
	public:
		// A preset timbre by the group and number a patch selects it with: 0 a, 1 b, 3 r. Group 2, i, is the board's
		// timbre memory, which the ROM does not have. Empty when there is no name.
		const std::string& timbre(uint8_t _group, uint8_t _number) const;
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
		std::array<std::vector<std::string>, 4> m_timbres;
		std::array<std::vector<std::string>, 2> m_pcmTones;
	};

	// The names for a model's boards from the ROMs the player found, and a PCM card image as the player loads it
	// (empty for none). Null when the model has no board of this dialect; _error says why names are missing.
	std::shared_ptr<const NameCatalog> loadNameCatalog(DeviceModel _model, const std::vector<uint8_t>& _pcmCard,
		std::string& _error);
}
