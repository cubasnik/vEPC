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

struct DiameterDisconnectPeerRequest {
    DiameterHeader header;
    std::string originHost;
    uint32_t disconnectCause = 0;
    bool hasOriginHost = false;
    bool hasDisconnectCause = false;
};

struct DiameterAuthInfoRequest {
    DiameterHeader header;
    std::string originHost;
    std::string originRealm;
    std::string userName;
    bool hasOriginHost = false;
    bool hasOriginRealm = false;
    bool hasUserName = false;
};

struct DiameterUpdateLocationRequest {
    DiameterHeader header;
    std::string originHost;
    std::string originRealm;
    std::string userName;
    bool hasOriginHost = false;
    bool hasOriginRealm = false;
    bool hasUserName = false;
};

struct DiameterPurgeUeRequest {
    DiameterHeader header;
    std::string originHost;
    std::string originRealm;
    std::string userName;
    bool hasOriginHost = false;
    bool hasOriginRealm = false;
    bool hasUserName = false;
};

struct DiameterCancelLocationRequest {
    DiameterHeader header;
    std::string originHost;
    std::string originRealm;
    std::string userName;
    bool hasOriginHost = false;
    bool hasOriginRealm = false;
    bool hasUserName = false;
};

constexpr uint32_t kS6aApplicationId = 16777251;

bool parseDiameterHeader(const std::vector<uint8_t>& packet, DiameterHeader& header, std::string& error);
bool parseCapabilitiesExchangeRequest(const std::vector<uint8_t>& packet,
                                      DiameterCapabilitiesExchangeRequest& request,
                                      std::string& error);
bool parseWatchdogRequest(const std::vector<uint8_t>& packet,
                         DiameterWatchdogRequest& request,
                         std::string& error);
bool parseDisconnectPeerRequest(const std::vector<uint8_t>& packet,
                                DiameterDisconnectPeerRequest& request,
                                std::string& error);
std::string formatDiameterCommand(uint32_t commandCode, bool request);
std::vector<uint8_t> buildWatchdogRequest(const std::string& originHost,
                                          uint32_t hopByHopId = 0xAAAABBBB,
                                          uint32_t endToEndId = 0xCCCCDDDD);
std::vector<uint8_t> buildWatchdogAnswer(const DiameterHeader& requestHeader,
                                         const std::string& originHost,
                                         uint32_t resultCode = 2001);
std::vector<uint8_t> buildDisconnectPeerRequest(const std::string& originHost,
                                                uint32_t disconnectCause = 0,
                                                uint32_t hopByHopId = 0x11112222,
                                                uint32_t endToEndId = 0x33334444);
std::vector<uint8_t> buildDisconnectPeerAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               uint32_t resultCode = 2001);
bool parseAuthInfoRequest(const std::vector<uint8_t>& packet,
                          DiameterAuthInfoRequest& request,
                          std::string& error);
std::vector<uint8_t> buildAuthInfoRequest(const std::string& originHost,
                                          const std::string& originRealm,
                                          const std::string& userName,
                                          uint32_t hopByHopId = 0x44445555,
                                          uint32_t endToEndId = 0x66667777);
std::vector<uint8_t> buildAuthInfoAnswer(const DiameterHeader& requestHeader,
                                         const std::string& originHost,
                                         const std::string& originRealm,
                                         uint32_t resultCode = 2001);
bool parseUpdateLocationRequest(const std::vector<uint8_t>& packet,
                                DiameterUpdateLocationRequest& request,
                                std::string& error);
std::vector<uint8_t> buildUpdateLocationRequest(const std::string& originHost,
                                                const std::string& originRealm,
                                                const std::string& userName,
                                                uint32_t hopByHopId = 0x55556666,
                                                uint32_t endToEndId = 0x77778888);
std::vector<uint8_t> buildUpdateLocationAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               const std::string& originRealm,
                                               uint32_t resultCode = 2001);
bool parsePurgeUeRequest(const std::vector<uint8_t>& packet,
                         DiameterPurgeUeRequest& request,
                         std::string& error);
std::vector<uint8_t> buildPurgeUeRequest(const std::string& originHost,
                                         const std::string& originRealm,
                                         const std::string& userName,
                                         uint32_t hopByHopId = 0x88889999,
                                         uint32_t endToEndId = 0xAAAABBBB);
std::vector<uint8_t> buildPurgeUeAnswer(const DiameterHeader& requestHeader,
                                        const std::string& originHost,
                                        const std::string& originRealm,
                                        uint32_t resultCode = 2001);
bool parseCancelLocationRequest(const std::vector<uint8_t>& packet,
                                DiameterCancelLocationRequest& request,
                                std::string& error);
std::vector<uint8_t> buildCancelLocationRequest(const std::string& originHost,
                                                const std::string& originRealm,
                                                const std::string& userName,
                                                uint32_t hopByHopId = 0xBBBBCCCC,
                                                uint32_t endToEndId = 0xDDDDEEEE);
std::vector<uint8_t> buildCancelLocationAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               const std::string& originRealm,
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