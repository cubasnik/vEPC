#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vepc {

struct GtpV1Header {
    uint8_t flags = 0;
    uint8_t version = 0;
    bool protocolType = false;
    bool hasExtensionHeader = false;
    bool hasSequenceNumber = false;
    bool hasNpduNumber = false;
    uint8_t messageType = 0;
    uint16_t payloadLength = 0;
    uint32_t teid = 0;
    uint16_t sequence = 0;
    uint8_t npduNumber = 0;
    uint8_t nextExtensionHeaderType = 0;
    std::size_t headerLength = 0;
    std::size_t totalLength = 0;
};

struct CreatePdpRequestInfo {
    std::string imsi;
    std::string apn;
    std::string ggsnIp;
    uint8_t pdpType = 0;
    bool hasImsi = false;
    bool hasApn = false;
    bool hasGgsnIp = false;
    bool hasPdpType = false;
};

bool parseGtpV1Header(const std::vector<uint8_t>& packet, GtpV1Header& header, std::string& error);
bool parseCreatePdpContextRequest(const std::vector<uint8_t>& packet,
                                  const GtpV1Header& header,
                                  CreatePdpRequestInfo& request,
                                  std::string& error);
bool parseUpdatePdpContextRequest(const std::vector<uint8_t>& packet,
                                  const GtpV1Header& header,
                                  CreatePdpRequestInfo& request,
                                  std::string& error);
bool parseInitiatePdpContextActivationRequest(const std::vector<uint8_t>& packet,
                                              const GtpV1Header& header,
                                              CreatePdpRequestInfo& request,
                                              std::string& error);
std::string formatGtpMessageType(uint8_t messageType);
std::vector<uint8_t> buildEchoResponse(const GtpV1Header& requestHeader, uint8_t recoveryRestartCounter = 0);
std::vector<uint8_t> buildCreatePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint32_t responseTeid,
                                                   uint8_t cause = 0x80);
std::vector<uint8_t> buildUpdatePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint32_t responseTeid,
                                                   uint8_t cause = 0x80);
std::vector<uint8_t> buildDeletePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint8_t cause = 0x80);
std::vector<uint8_t> buildInitiatePdpContextActivationResponse(const GtpV1Header& requestHeader,
                                                               uint32_t responseTeid,
                                                               uint8_t cause = 0x80);

}  // namespace vepc