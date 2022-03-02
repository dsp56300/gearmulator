#pragma once
#include <string>

#include "esaiListener.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/dsp.h"

#include "../virusLib/romfile.h"
#include "../virusLib/microcontroller.h"

class ConsoleApp
{
public:
	ConsoleApp(const std::string& _romFile, uint32_t _memorySize, uint32_t _extMemAddress);

	void loadSingle(int b, int p);
	bool loadSingle(const std::string& _preset);

	std::string getSingleName() const;

	static void waitReturn();

	std::thread bootDSP();

	void run(const std::string& _audioOutputFilename);

private:
	dsp56k::IPeripherals& getYPeripherals();
	void audioCallback(uint32_t audioCallbackCount);

	dsp56k::Memory memory;
	virusLib::ROMFile v;
	dsp56k::Peripherals56367 periphY;
	dsp56k::Peripherals56362 periphX;
	dsp56k::PeripheralsNop periphNop;
	dsp56k::DSP dsp;
	virusLib::Microcontroller uc;

	virusLib::Microcontroller::TPreset preset;
};
