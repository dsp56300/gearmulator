#pragma once

#include <cstdint>

namespace jucePluginEditorLib::patchManager
{
	namespace defaultSkin::colors
	{
		constexpr uint32_t background		= 0xff222222;
		constexpr uint32_t selectedItem		= 0xff444444;
		constexpr uint32_t itemText			= 0xffeeeeee;
		constexpr uint32_t textEditOutline	= selectedItem;
		constexpr uint32_t infoLabel		= 0xff999999;
		constexpr uint32_t infoText			= itemText;
		constexpr uint32_t infoHeadline		= infoText;
		constexpr uint32_t statusText		= itemText;
		constexpr uint32_t scrollbar		= 0xff999999;
	}
}
