#include "gtp_parser.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace vepc {

namespace {

uint16_t readUint16Be(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
}

uint32_t readUint32Be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24)
        | (static_cast<uint32_t>(data[1]) << 16)
        | (static_cast<uint32_t>(data[2]) << 8)
        | static_cast<uint32_t>(data[3]);
}

std::string decodeTbcdDigits(const uint8_t* data, std::size_t size) {
    std::string digits;
    digits.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        const uint8_t octet = data[index];
        const uint8_t low = static_cast<uint8_t>(octet & 0x0F);
        const uint8_t high = static_cast<uint8_t>((octet >> 4) & 0x0F);

        if (low <= 9) {
            digits.push_back(static_cast<char>('0' + low));
        }
        if (high <= 9) {
            digits.push_back(static_cast<char>('0' + high));
        }
    }
    return digits;
}

std::string decodeAccessPointName(const uint8_t* data, std::size_t size, std::string& error) {
    std::string apn;
    std::size_t offset = 0;
    while (offset < size) {
        const uint8_t labelLength = data[offset++];
        if (labelLength == 0) {
            break;
        }
        if (offset + labelLength > size) {
            error = "APN label length exceeds IE size";
            return {};
        }
        if (!apn.empty()) {
            apn.push_back('.');
        }
        for (std::size_t index = 0; index < labelLength; ++index) {
            const unsigned char ch = data[offset + index];
            apn.push_back(static_cast<char>(std::isprint(ch) ? ch : '.'));
        }
        offset += labelLength;
    }
    return apn;
}

std::string decodeIpv4Address(const uint8_t* data, std::size_t size) {
    if (size != 4) {
        return {};
    }

    std::ostringstream oss;
    oss << static_cast<int>(data[0]) << '.'
        << static_cast<int>(data[1]) << '.'
        << static_cast<int>(data[2]) << '.'
        << static_cast<int>(data[3]);
    return oss.str();
}

uint8_t buildResponseFlags(const GtpV1Header& requestHeader) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    uint8_t responseFlags = static_cast<uint8_t>((1u << 5) | 0x10u);
    if (hasOptionalFields) {
        if (requestHeader.hasExtensionHeader) {
            responseFlags |= 0x04u;
        }
        if (requestHeader.hasSequenceNumber) {
            responseFlags |= 0x02u;
        }
        if (requestHeader.hasNpduNumber) {
            responseFlags |= 0x01u;
        }
    }
    return responseFlags;
}

void appendOptionalFields(std::vector<uint8_t>& response, const GtpV1Header& requestHeader) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    if (!hasOptionalFields) {
        return;
    }

    response.push_back(static_cast<uint8_t>((requestHeader.sequence >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(requestHeader.sequence & 0xFF));
    response.push_back(requestHeader.hasNpduNumber ? requestHeader.npduNumber : 0x00);
    response.push_back(requestHeader.hasExtensionHeader ? requestHeader.nextExtensionHeaderType : 0x00);
}

bool parseDemoPdpContextRequest(const std::vector<uint8_t>& packet,
                               const GtpV1Header& header,
                               uint8_t expectedMessageType,
                               const char* label,
                               CreatePdpRequestInfo& request,
                               std::string& error) {
    request = {};
    error.clear();

    if (header.messageType != expectedMessageType) {
        error = std::string("GTP message is not ") + label;
        return false;
    }
    if (header.totalLength > packet.size() || header.headerLength > packet.size()) {
        error = "GTP header boundaries exceed packet size";
        return false;
    }

    std::size_t offset = header.headerLength;
    while (offset < header.totalLength) {
        const uint8_t ieType = packet[offset];

        if (ieType == 0x02) {
            if (offset + 9 > header.totalLength) {
                error = "truncated IMSI IE";
                return false;
            }
            request.imsi = decodeTbcdDigits(&packet[offset + 1], 8);
            request.hasImsi = !request.imsi.empty();
            offset += 9;
            continue;
        }

        if (offset + 3 > header.totalLength) {
            error = "truncated TLV IE header";
            return false;
        }

        const uint16_t ieLength = readUint16Be(&packet[offset + 1]);
        const std::size_t valueOffset = offset + 3;
        const std::size_t nextOffset = valueOffset + ieLength;
        if (nextOffset > header.totalLength) {
            error = "truncated TLV IE payload";
            return false;
        }

        const uint8_t* value = &packet[valueOffset];
        switch (ieType) {
        case 0x80:
            if (ieLength >= 2) {
                request.pdpType = value[1];
                request.hasPdpType = true;
            }
            break;
        case 0x83: {
            std::string apnError;
            request.apn = decodeAccessPointName(value, ieLength, apnError);
            if (!apnError.empty()) {
                error = apnError;
                return false;
            }
            request.hasApn = !request.apn.empty();
            break;
        }
        case 0x85: {
            const std::string ip = decodeIpv4Address(value, ieLength);
            if (!ip.empty() && !request.hasGgsnIp) {
                request.ggsnIp = ip;
                request.hasGgsnIp = true;
            }
            break;
        }
        default:
            break;
        }

        offset = nextOffset;
    }

    if (!request.hasImsi && !request.hasApn && !request.hasGgsnIp && !request.hasPdpType) {
        error = std::string(label) + " does not contain any supported demo IEs";
        return false;
    }

    return true;
}

}  // namespace

std::string formatGtpMessageType(uint8_t messageType) {
    switch (messageType) {
    case 0x01:
        return "Echo Request (0x01)";
    case 0x02:
        return "Echo Response (0x02)";
    case 0x10:
        return "Create PDP Context Request (0x10)";
    case 0x11:
        return "Create PDP Context Response (0x11)";
    case 0x12:
        return "Update PDP Context Request (0x12)";
    case 0x13:
        return "Update PDP Context Response (0x13)";
    case 0x14:
        return "Delete PDP Context Request (0x14)";
    case 0x15:
        return "Delete PDP Context Response (0x15)";
    case 0x16:
        return "Initiate PDP Context Activation Request (0x16)";
    case 0x17:
        return "Initiate PDP Context Activation Response (0x17)";
    case 0x1B:
        return "PDU Notification Request (0x1B)";
    case 0x1C:
        return "PDU Notification Response (0x1C)";
    case 0x1D:
        return "PDU Notification Reject Request (0x1D)";
    case 0x1E:
        return "PDU Notification Reject Response (0x1E)";
    case 0x22:
        return "Failure Report Request (0x22)";
    case 0x23:
        return "Failure Report Response (0x23)";
    case 0x24:
        return "Note MS GPRS Present Request (0x24)";
    case 0x25:
        return "Note MS GPRS Present Response (0x25)";
    case 0x30:
        return "Identification Request (0x30)";
    case 0x31:
        return "Identification Response (0x31)";
    default: {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(messageType);
        return oss.str();
    }
    }
}

bool parseGtpV1Header(const std::vector<uint8_t>& packet, GtpV1Header& header, std::string& error) {
    header = {};
    error.clear();

    if (packet.size() < 8) {
        error = "packet too short for GTPv1-C base header";
        return false;
    }

    header.flags = packet[0];
    header.version = static_cast<uint8_t>((header.flags >> 5) & 0x07);
    header.protocolType = (header.flags & 0x10) != 0;
    header.hasExtensionHeader = (header.flags & 0x04) != 0;
    header.hasSequenceNumber = (header.flags & 0x02) != 0;
    header.hasNpduNumber = (header.flags & 0x01) != 0;
    header.messageType = packet[1];
    header.payloadLength = readUint16Be(&packet[2]);
    header.teid = readUint32Be(&packet[4]);
    header.headerLength = 8;
    header.totalLength = 8 + static_cast<std::size_t>(header.payloadLength);

    if (header.version != 1) {
        error = "unsupported GTP version: " + std::to_string(header.version);
        return false;
    }

    if (!header.protocolType) {
        error = "packet is not marked as GTP protocol type";
        return false;
    }

    if (header.hasExtensionHeader || header.hasSequenceNumber || header.hasNpduNumber) {
        if (packet.size() < 12) {
            error = "packet too short for optional GTP fields";
            return false;
        }

        header.sequence = readUint16Be(&packet[8]);
        header.npduNumber = packet[10];
        header.nextExtensionHeaderType = packet[11];
        header.headerLength = 12;
    }

    if (packet.size() < header.totalLength) {
        std::ostringstream oss;
        oss << "declared GTP length " << header.payloadLength
            << " exceeds received payload size " << (packet.size() - 8);
        error = oss.str();
        return false;
    }

    if (header.totalLength < header.headerLength) {
        error = "declared GTP length is smaller than parsed header length";
        return false;
    }

    return true;
}

bool parseCreatePdpContextRequest(const std::vector<uint8_t>& packet,
                                  const GtpV1Header& header,
                                  CreatePdpRequestInfo& request,
                                  std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x10,
                                      "Create PDP Context Request",
                                      request,
                                      error);
}

bool parseUpdatePdpContextRequest(const std::vector<uint8_t>& packet,
                                  const GtpV1Header& header,
                                  CreatePdpRequestInfo& request,
                                  std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x12,
                                      "Update PDP Context Request",
                                      request,
                                      error);
}

bool parseInitiatePdpContextActivationRequest(const std::vector<uint8_t>& packet,
                                              const GtpV1Header& header,
                                              CreatePdpRequestInfo& request,
                                              std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x16,
                                      "Initiate PDP Context Activation Request",
                                      request,
                                      error);
}

bool parsePduNotificationRequest(const std::vector<uint8_t>& packet,
                                 const GtpV1Header& header,
                                 CreatePdpRequestInfo& request,
                                 std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x1B,
                                      "PDU Notification Request",
                                      request,
                                      error);
}

bool parsePduNotificationRejectRequest(const std::vector<uint8_t>& packet,
                                       const GtpV1Header& header,
                                       CreatePdpRequestInfo& request,
                                       std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x1D,
                                      "PDU Notification Reject Request",
                                      request,
                                      error);
}

bool parseFailureReportRequest(const std::vector<uint8_t>& packet,
                               const GtpV1Header& header,
                               CreatePdpRequestInfo& request,
                               std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x22,
                                      "Failure Report Request",
                                      request,
                                      error);
}

bool parseNoteMsGprsPresentRequest(const std::vector<uint8_t>& packet,
                                   const GtpV1Header& header,
                                   CreatePdpRequestInfo& request,
                                   std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x24,
                                      "Note MS GPRS Present Request",
                                      request,
                                      error);
}

bool parseIdentificationRequest(const std::vector<uint8_t>& packet,
                                const GtpV1Header& header,
                                CreatePdpRequestInfo& request,
                                std::string& error) {
    return parseDemoPdpContextRequest(packet,
                                      header,
                                      0x30,
                                      "Identification Request",
                                      request,
                                      error);
}

std::vector<uint8_t> buildEchoResponse(const GtpV1Header& requestHeader, uint8_t recoveryRestartCounter) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x02);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(requestHeader.teid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x0E);
    response.push_back(recoveryRestartCounter);
    return response;
}

std::vector<uint8_t> buildCreatePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint32_t responseTeid,
                                                   uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x11);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildUpdatePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint32_t responseTeid,
                                                   uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x13);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildDeletePdpContextResponse(const GtpV1Header& requestHeader,
                                                   uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x15);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((requestHeader.teid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(requestHeader.teid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildInitiatePdpContextActivationResponse(const GtpV1Header& requestHeader,
                                                               uint32_t responseTeid,
                                                               uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x17);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildPduNotificationResponse(const GtpV1Header& requestHeader,
                                                  uint32_t responseTeid,
                                                  uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x1C);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildPduNotificationRejectResponse(const GtpV1Header& requestHeader,
                                                        uint32_t responseTeid,
                                                        uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x1E);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildFailureReportResponse(const GtpV1Header& requestHeader,
                                                uint32_t responseTeid,
                                                uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x23);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildNoteMsGprsPresentResponse(const GtpV1Header& requestHeader,
                                                    uint32_t responseTeid,
                                                    uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x25);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

std::vector<uint8_t> buildIdentificationResponse(const GtpV1Header& requestHeader,
                                                 uint32_t responseTeid,
                                                 uint8_t cause) {
    const bool hasOptionalFields = requestHeader.hasExtensionHeader
        || requestHeader.hasSequenceNumber
        || requestHeader.hasNpduNumber;
    const uint16_t payloadLength = static_cast<uint16_t>((hasOptionalFields ? 4 : 0) + 2);

    std::vector<uint8_t> response;
    response.reserve(8 + (hasOptionalFields ? 4 : 0) + 2);

    response.push_back(buildResponseFlags(requestHeader));
    response.push_back(0x31);
    response.push_back(static_cast<uint8_t>((payloadLength >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(payloadLength & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((responseTeid >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(responseTeid & 0xFF));

    appendOptionalFields(response, requestHeader);

    response.push_back(0x01);
    response.push_back(cause);
    return response;
}

}  // namespace vepc