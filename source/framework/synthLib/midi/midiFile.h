#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace synthLib::midi
{
    struct Event
    {
        double seconds = 0;
        std::vector<uint8_t> bytes;
        uint8_t port = 0;
    };

    bool readSmf(const std::vector<uint8_t>& data, std::vector<Event>& events, std::string& error);
    bool readRcp(const std::vector<uint8_t>& data, std::vector<Event>& events, std::string& error);
    bool isRcpV2(const std::vector<uint8_t>& data) noexcept;
} // namespace synthLib::midi
