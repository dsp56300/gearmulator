#include "Virus_Parts.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;

Parts::Parts(VirusParameterBinding & _parameterBinding, Virus::Controller& _controller) : m_parameterBinding(_parameterBinding), m_controller(_controller)
{

}