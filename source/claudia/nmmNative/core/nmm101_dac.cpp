#include "nmm101_dac.h"

namespace nmm::native
{
DacStereo runResidentDac(const Word24* inputFourWords,
                         Word24 masterCoefficient,
                         Word24 dcOffset) noexcept
{
    // P:$1d1 loads Y:$5f into Y0. P:$1d2 loads X:$5f into A and the first
    // graph word into Y1. P:$1d3 reads X:$5f into B while MACing A. The two
    // alternating MACs therefore start from the same DC offset and consume
    // four consecutive words from the selected resident buffer.
    auto left = mac(from24(dcOffset), inputFourWords[0], masterCoefficient);
    auto right = mac(from24(dcOffset), inputFourWords[1], masterCoefficient);
    left = mac(left, inputFourWords[2], masterCoefficient);
    right = mac(right, inputFourWords[3], masterCoefficient);

    // MOVE A/Y applies the DSP's saturation mode at the 24-bit bus transfer;
    // the resident firmware leaves the default saturation setting enabled.
    const auto leftWord = limit24(left);
    const auto rightWord = limit24(right);
    return {leftWord, rightWord, decodeDacSigned18(leftWord),
            decodeDacSigned18(rightWord)};
}
}
