#include "diameter_parser.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vepc {

namespace {

uint32_t readUint24Be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16)
        | (static_cast<uint32_t>(data[1]) << 8)
        | static_cast<uint32_t>(data[2]);
}

uint32_t readUint32Be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24)
        | (static_cast<uint32_t>(data[1]) << 16)
        | (static_cast<uint32_t>(data[2]) << 8)
        | static_cast<uint32_t>(data[3]);
}

std::size_t paddedAvpLength(std::size_t avpLength) {
    return (avpLength + 3u) & ~std::size_t(3u);
}

void appendUint24Be(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendUint32Be(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    output.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendPadding(std::vector<uint8_t>& output, std::size_t payloadSize) {
    const std::size_t paddedSize = paddedAvpLength(payloadSize);
    output.insert(output.end(), paddedSize - payloadSize, 0x00);
}

void appendUint32Avp(std::vector<uint8_t>& output, uint32_t code, uint32_t value, uint8_t flags = 0x40) {
    appendUint32Be(output, code);
    output.push_back(flags);
    appendUint24Be(output, 12);
    appendUint32Be(output, value);
}

void appendStringAvp(std::vector<uint8_t>& output, uint32_t code, const std::string& value, uint8_t flags = 0x40) {
    appendUint32Be(output, code);
    output.push_back(flags);
    appendUint24Be(output, static_cast<uint32_t>(8 + value.size()));
    output.insert(output.end(), value.begin(), value.end());
    appendPadding(output, value.size());
}

}  // namespace

bool parseDiameterHeader(const std::vector<uint8_t>& packet, DiameterHeader& header, std::string& error) {
    header = {};
    error.clear();

    if (packet.size() < 20) {
        error = "packet too short for Diameter header";
        return false;
    }

    header.version = packet[0];
    header.messageLength = readUint24Be(&packet[1]);
    header.flags = packet[4];
    header.commandCode = readUint24Be(&packet[5]);
    header.applicationId = readUint32Be(&packet[8]);
    header.hopByHopId = readUint32Be(&packet[12]);
    header.endToEndId = readUint32Be(&packet[16]);
    header.request = (header.flags & 0x80) != 0;
    header.proxiable = (header.flags & 0x40) != 0;
    header.error = (header.flags & 0x20) != 0;
    header.retransmitted = (header.flags & 0x10) != 0;

    if (header.version != 1) {
        error = "unsupported Diameter version: " + std::to_string(header.version);
        return false;
    }
    if (header.messageLength < header.headerLength) {
        error = "declared Diameter length is smaller than header length";
        return false;
    }
    if (packet.size() < header.messageLength) {
        std::ostringstream oss;
        oss << "declared Diameter length " << header.messageLength
            << " exceeds received size " << packet.size();
        error = oss.str();
        return false;
    }

    return true;
}

bool parseCapabilitiesExchangeRequest(const std::vector<uint8_t>& packet,
                                      DiameterCapabilitiesExchangeRequest& request,
                                      std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 257 || !request.header.request) {
        error = "packet is not a Diameter CER";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 296) {
            request.originRealm.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginRealm = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::string formatDiameterCommand(uint32_t commandCode, bool request) {
    if (commandCode == 257) {
        return request ? "Capabilities-Exchange-Request (CER)" : "Capabilities-Exchange-Answer (CEA)";
    }
    if (commandCode == 280) {
        return request ? "Device-Watchdog-Request (DWR)" : "Device-Watchdog-Answer (DWA)";
    }
    if (commandCode == 282) {
        return request ? "Disconnect-Peer-Request (DPR)" : "Disconnect-Peer-Answer (DPA)";
    }
    if (commandCode == 316) {
        return request ? "Update-Location-Request (ULR)" : "Update-Location-Answer (ULA)";
    }
    if (commandCode == 317) {
        return request ? "Cancel-Location-Request (CLR)" : "Cancel-Location-Answer (CLA)";
    }
    if (commandCode == 318) {
        return request ? "Authentication-Information-Request (AIR)" : "Authentication-Information-Answer (AIA)";
    }
    if (commandCode == 321) {
        return request ? "Purge-UE-Request (PUR)" : "Purge-UE-Answer (PUA)";
    }

    std::ostringstream oss;
    oss << (request ? "Request " : "Answer ")
        << "command=" << commandCode;
    return oss.str();
}

std::vector<uint8_t> buildCapabilitiesExchangeRequest(const std::string& originHost,
                                                      const std::string& originRealm,
                                                      uint32_t hopByHopId,
                                                      uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(128);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0x80);
    appendUint24Be(request, 257);
    appendUint32Be(request, 0);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendStringAvp(request, 296, originRealm);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildCapabilitiesExchangeAnswer(const DiameterHeader& requestHeader,
                                                     const std::string& originHost,
                                                     const std::string& originRealm,
                                                     uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(128);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 257);
    appendUint32Be(response, requestHeader.applicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);
    appendStringAvp(response, 296, originRealm);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parseWatchdogRequest(const std::vector<uint8_t>& packet,
                         DiameterWatchdogRequest& request,
                         std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 280 || !request.header.request) {
        error = "packet is not a Diameter DWR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildWatchdogRequest(const std::string& originHost,
                                          uint32_t hopByHopId,
                                          uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(64);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0x80);
    appendUint24Be(request, 280);
    appendUint32Be(request, 0);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildWatchdogAnswer(const DiameterHeader& requestHeader,
                                         const std::string& originHost,
                                         uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(64);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 280);
    appendUint32Be(response, requestHeader.applicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parseDisconnectPeerRequest(const std::vector<uint8_t>& packet,
                                DiameterDisconnectPeerRequest& request,
                                std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 282 || !request.header.request) {
        error = "packet is not a Diameter DPR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 273 && payloadLength == 4) {
            request.disconnectCause = readUint32Be(&packet[payloadOffset]);
            request.hasDisconnectCause = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildDisconnectPeerRequest(const std::string& originHost,
                                                uint32_t disconnectCause,
                                                uint32_t hopByHopId,
                                                uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(64);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0x80);
    appendUint24Be(request, 282);
    appendUint32Be(request, 0);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendUint32Avp(request, 273, disconnectCause);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildDisconnectPeerAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(64);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 282);
    appendUint32Be(response, requestHeader.applicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parseAuthInfoRequest(const std::vector<uint8_t>& packet,
                          DiameterAuthInfoRequest& request,
                          std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 318 || !request.header.request) {
        error = "packet is not a Diameter AIR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 296) {
            request.originRealm.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginRealm = true;
        } else if (avpCode == 1) {
            request.userName.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasUserName = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildAuthInfoRequest(const std::string& originHost,
                                          const std::string& originRealm,
                                          const std::string& userName,
                                          uint32_t hopByHopId,
                                          uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(128);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0xC0);
    appendUint24Be(request, 318);
    appendUint32Be(request, kS6aApplicationId);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendStringAvp(request, 296, originRealm);
    appendStringAvp(request, 1, userName, 0x40);
    appendUint32Avp(request, 277, 1, 0x40);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildAuthInfoAnswer(const DiameterHeader& requestHeader,
                                         const std::string& originHost,
                                         const std::string& originRealm,
                                         uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(128);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 318);
    appendUint32Be(response, kS6aApplicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);
    appendStringAvp(response, 296, originRealm);
    appendUint32Avp(response, 277, 1, 0x40);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parseUpdateLocationRequest(const std::vector<uint8_t>& packet,
                                DiameterUpdateLocationRequest& request,
                                std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 316 || !request.header.request) {
        error = "packet is not a Diameter ULR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 296) {
            request.originRealm.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginRealm = true;
        } else if (avpCode == 1) {
            request.userName.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasUserName = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildUpdateLocationRequest(const std::string& originHost,
                                                const std::string& originRealm,
                                                const std::string& userName,
                                                uint32_t hopByHopId,
                                                uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(128);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0xC0);
    appendUint24Be(request, 316);
    appendUint32Be(request, kS6aApplicationId);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendStringAvp(request, 296, originRealm);
    appendStringAvp(request, 1, userName, 0x40);
    appendUint32Avp(request, 277, 1, 0x40);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildUpdateLocationAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               const std::string& originRealm,
                                               uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(128);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 316);
    appendUint32Be(response, kS6aApplicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);
    appendStringAvp(response, 296, originRealm);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parsePurgeUeRequest(const std::vector<uint8_t>& packet,
                         DiameterPurgeUeRequest& request,
                         std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 321 || !request.header.request) {
        error = "packet is not a Diameter PUR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 296) {
            request.originRealm.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginRealm = true;
        } else if (avpCode == 1) {
            request.userName.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasUserName = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildPurgeUeRequest(const std::string& originHost,
                                         const std::string& originRealm,
                                         const std::string& userName,
                                         uint32_t hopByHopId,
                                         uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(128);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0xC0);
    appendUint24Be(request, 321);
    appendUint32Be(request, kS6aApplicationId);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendStringAvp(request, 296, originRealm);
    appendStringAvp(request, 1, userName, 0x40);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildPurgeUeAnswer(const DiameterHeader& requestHeader,
                                        const std::string& originHost,
                                        const std::string& originRealm,
                                        uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(128);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 321);
    appendUint32Be(response, kS6aApplicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);
    appendStringAvp(response, 296, originRealm);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

bool parseCancelLocationRequest(const std::vector<uint8_t>& packet,
                                DiameterCancelLocationRequest& request,
                                std::string& error) {
    request = {};
    if (!parseDiameterHeader(packet, request.header, error)) {
        return false;
    }
    if (request.header.commandCode != 317 || !request.header.request) {
        error = "packet is not a Diameter CLR";
        return false;
    }

    std::size_t offset = request.header.headerLength;
    while (offset < request.header.messageLength) {
        if (offset + 8 > request.header.messageLength) {
            error = "truncated Diameter AVP header";
            return false;
        }

        const uint32_t avpCode = readUint32Be(&packet[offset]);
        const uint8_t avpFlags = packet[offset + 4];
        const uint32_t avpLength = readUint24Be(&packet[offset + 5]);
        const std::size_t avpHeaderLength = (avpFlags & 0x80) != 0 ? 12u : 8u;
        if (avpLength < avpHeaderLength) {
            error = "invalid Diameter AVP length";
            return false;
        }
        if (offset + avpLength > request.header.messageLength) {
            error = "Diameter AVP exceeds declared message length";
            return false;
        }

        const std::size_t payloadOffset = offset + avpHeaderLength;
        const std::size_t payloadLength = avpLength - avpHeaderLength;
        if (avpCode == 264) {
            request.originHost.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginHost = true;
        } else if (avpCode == 296) {
            request.originRealm.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasOriginRealm = true;
        } else if (avpCode == 1) {
            request.userName.assign(reinterpret_cast<const char*>(packet.data() + payloadOffset), payloadLength);
            request.hasUserName = true;
        }

        offset += paddedAvpLength(avpLength);
    }

    return true;
}

std::vector<uint8_t> buildCancelLocationRequest(const std::string& originHost,
                                                const std::string& originRealm,
                                                const std::string& userName,
                                                uint32_t hopByHopId,
                                                uint32_t endToEndId) {
    std::vector<uint8_t> request;
    request.reserve(128);

    request.push_back(0x01);
    appendUint24Be(request, 0);
    request.push_back(0xC0);
    appendUint24Be(request, 317);
    appendUint32Be(request, kS6aApplicationId);
    appendUint32Be(request, hopByHopId);
    appendUint32Be(request, endToEndId);

    appendStringAvp(request, 264, originHost);
    appendStringAvp(request, 296, originRealm);
    appendStringAvp(request, 1, userName, 0x40);

    const uint32_t totalLength = static_cast<uint32_t>(request.size());
    request[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    request[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    request[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return request;
}

std::vector<uint8_t> buildCancelLocationAnswer(const DiameterHeader& requestHeader,
                                               const std::string& originHost,
                                               const std::string& originRealm,
                                               uint32_t resultCode) {
    std::vector<uint8_t> response;
    response.reserve(128);

    response.push_back(0x01);
    appendUint24Be(response, 0);

    uint8_t flags = static_cast<uint8_t>(requestHeader.flags & 0x40);
    response.push_back(flags);
    appendUint24Be(response, 317);
    appendUint32Be(response, kS6aApplicationId);
    appendUint32Be(response, requestHeader.hopByHopId);
    appendUint32Be(response, requestHeader.endToEndId);

    appendUint32Avp(response, 268, resultCode);
    appendStringAvp(response, 264, originHost);
    appendStringAvp(response, 296, originRealm);

    const uint32_t totalLength = static_cast<uint32_t>(response.size());
    response[1] = static_cast<uint8_t>((totalLength >> 16) & 0xFF);
    response[2] = static_cast<uint8_t>((totalLength >> 8) & 0xFF);
    response[3] = static_cast<uint8_t>(totalLength & 0xFF);

    return response;
}

}  // namespace vepc