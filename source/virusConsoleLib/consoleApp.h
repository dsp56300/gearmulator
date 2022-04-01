#pragma once
#include <string>

#include "esaiListener.h"
#include "esaiListenerToCallback.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/dsp.h"

#include "../virusLib/romfile.h"
#include "../virusLib/microcontroller.h"
#include "../virusLib/demoplayback.h"

class ConsoleApp
{
public:
	ConsoleApp(const std::string& _romFile, uint32_t _memorySize, uint32_t _extMemAddress);

	bool isValid() const;

	void loadSingle(int b, int p);
	bool loadSingle(const std::string& _preset);

	bool loadDemo(const std::string& _filename);

	std::string getSingleName() const;
	std::string getSingleNameAsFilename() const;

	static void waitReturn();

	void run(const std::string& _audioOutputFilename, uint32_t _maxSampleCount = 0);
	void run(EsaiListenerToCallback::TDataCallback _callback, uint32_t _maxSampleCount = 0);

	const virusLib::ROMFile& getRom() const { return v; }

private:
	void run(const std::string& _audioOutputFilename, EsaiListenerToCallback::TDataCallback _callback, uint32_t _maxSampleCount = 0);

	std::thread bootDSP();
	dsp56k::IPeripherals& getYPeripherals();
	void audioCallback(uint32_t audioCallbackCount);

	dsp56k::Memory memory;
	const std::string m_romName;
	virusLib::ROMFile v;
	dsp56k::Peripherals56367 periphY;
	dsp56k::Peripherals56362 periphX;
	dsp56k::PeripheralsNop periphNop;
	dsp56k::DSP dsp;
	virusLib::Microcontroller uc;
	std::unique_ptr<virusLib::DemoPlayback> m_demo;

	virusLib::Microcontroller::TPreset preset;
};
