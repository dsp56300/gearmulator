#pragma once

#include <cstdint>

#include "haltDSP.h"

namespace hwLib
{
	template<uint32_t UcFreqHz, uint32_t Samplerate, uint32_t SamplesThreshold>
	class SyncUCtoDSP
	{
	public:
		static constexpr double SamplerateInv = 1.0 / static_cast<double>(Samplerate);
		static constexpr double UcCyclesPerSample = static_cast<double>(UcFreqHz) / Samplerate;
		static constexpr double UcCyclesThreshold = UcCyclesPerSample * SamplesThreshold;

		explicit SyncUCtoDSP(dsp56k::DSP& _dsp) : m_haltDSP(_dsp)
		{
		}

		virtual ~SyncUCtoDSP() = default;

		void advanceDspSample()
		{
			m_targetUcCycles += UcCyclesPerSample;
			++m_totalSampleCount;

			if((m_totalSampleCount & (SamplesThreshold-1)) == 0)
				m_cvSampleAdded.notify_one();
		}

		void advanceUcCycles(const uint32_t _cycles)
		{
			m_totalUcCycles += static_cast<double>(_cycles);

			evaluate();
		}

		auto& getHaltDSP() { return m_haltDSP; }

	protected:
		void evaluate()
		{
			if(!m_totalSampleCount)
				return;

			const auto diff = m_totalUcCycles - m_targetUcCycles;

			if(diff > UcCyclesThreshold)
			{
				// UC is too fast, slow down
				resumeDSP();
				haltUC();
			}
			else if(diff < -UcCyclesThreshold)
			{
				haltDSP();
				resumeUC();
			}
			else
			{
				resumeDSP();
				resumeUC();
			}
		}

		void haltUC()
		{
			const auto lastCount = m_totalSampleCount;
			std::unique_lock lock(m_lockSampleAdded);
			m_cvSampleAdded.wait(lock, [&]
			{
				return m_totalSampleCount > lastCount;
			});
		}

		virtual void resumeUC()
		{
		}

		void haltDSP()
		{
			m_haltDSP.haltDSP();
		}

		void resumeDSP()
		{
			m_haltDSP.resumeDSP();
		}

	private:
		HaltDSP m_haltDSP;
		double m_targetUcCycles = 0.0;
		double m_totalUcCycles = 0.0;
		uint32_t m_totalSampleCount = 0;
		std::condition_variable m_cvSampleAdded;
		std::mutex m_lockSampleAdded;
	};
}
