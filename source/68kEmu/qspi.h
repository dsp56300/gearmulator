#pragma once

namespace mc68k
{
	class Qsm;

	class Qspi
	{
	public:
		explicit Qspi(Qsm& _qsm);

		void exec();

	private:
		Qsm& m_qsm;
	};
}
