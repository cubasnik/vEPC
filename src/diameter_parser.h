#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vepc {

struct DiameterHeader {
    uint8_t version = 0;
    uint32_t messageLength = 0;
    uint8_t flags = 0;
    uint32_t commandCode = 0;
    uint32_t applicationId = 0;
    uint32_t hopByHopId = 0;
    uint32_t endToEndId = 0;
    bool request = false;
    bool proxiable = false;
    bool error = false;
    bool retransmitted = false;
    std::size_t headerLength = 20;
};

struct DiameterCapabilitiesExchangeRequest {
    DiameterHeader header;
    std::string originHost;
    std::string originRealm;
    bool hasOriginHost = false;
    bool hasOriginRealm = false;
};

struct DiameterWatchdogRequest {
    DiameterHeader header;
    std::string originHost;
    bool hasOriginHost = false;
};

bool parseDiameterHeader(const std::vector<uint8_t>& packet, DiameterHeader& header, std::string& error);
bool parseCapabilitiesExchangeRequest(const std::vector<uint8_t>& packet,
                                      DiameterCapabilitiesExchangeRequest& request,
                                      std::string& error);
bool parseWatchdogRequest(const std::vector<uint8_t>& packet,
                         DiameterWatchdogRequest& request,
                         std::string& error);
std::string formatDiameterCommand(uint32_t commandCode, bool request);
std::vector<uint8_t> buildWatchdogRequest(const std::string& originHost,
                                          uint32_t hopByHopId = 0xAAAABBBB,
                                          uint32_t endToEndId = 0xCCCCDDDD);
std::vector<uint8_t> buildWatchdogAnswer(const DiameterHeader& requestHeader,
                                         const std::string& originHost,
                                         uint32_t resultCode = 2001);
std::vector<uint8_t> buildCapabilitiesExchangeRequest(const std::string& originHost,
                                                      const std::string& originRealm,
                                                      uint32_t hopByHopId = 0x12345678,
                                                      uint32_t endToEndId = 0x9ABCDEF0);
std::vector<uint8_t> buildCapabilitiesExchangeAnswer(const DiameterHeader& requestHeader,
                                                    const std::string& originHost,
                                                    const std::string& originRealm,
                                                    uint32_t resultCode = 2001);

}  // namespace vepc