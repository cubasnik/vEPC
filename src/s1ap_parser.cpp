#include "s1ap_parser.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace vepc {

namespace {

std::string sanitizeAscii(const uint8_t* data, std::size_t size) {
    std::string value;
    value.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const unsigned char ch = data[index];
        value.push_back(static_cast<char>(std::isprint(ch) ? ch : '.'));
    }
    return value;
}

template <typename MessageType>
bool parseDemoNasCarrier(const std::vector<uint8_t>& packet,
                         uint8_t expectedProcedureCode,
                         const char* label,
                         MessageType& message,
                         std::string& error) {
    message = {};
    error.clear();

    if (packet.size() < 3) {
        error = std::string("packet too short for ") + label;
        return false;
    }

    message.procedureCode = packet[0];
    if (message.procedureCode != expectedProcedureCode) {
        error = "unsupported S1AP procedure code: " + formatS1apProcedureCode(message.procedureCode);
        return false;
    }

    std::size_t offset = 1;
    while (offset < packet.size()) {
        if (offset + 2 > packet.size()) {
            error = "truncated demo S1AP TLV header";
            return false;
        }

        const uint8_t ieType = packet[offset++];
        const uint8_t ieLength = packet[offset++];
        if (offset + ieLength > packet.size()) {
            error = "truncated demo S1AP TLV payload";
            return false;
        }

        const uint8_t* value = &packet[offset];
        switch (ieType) {
        case 0x01:
            message.imsi = sanitizeAscii(value, ieLength);
            message.hasImsi = !message.imsi.empty();
            break;
        case 0x02:
            message.guti = sanitizeAscii(value, ieLength);
            message.hasGuti = !message.guti.empty();
            break;
        case 0x03:
            if (ieLength == 0) {
                error = "NAS PDU is empty";
                return false;
            }
            message.nasPdu.assign(value, value + ieLength);
            message.nasMessageType = message.nasPdu.front();
            message.hasNasPdu = true;
            break;
        default:
            break;
        }

        offset += ieLength;
    }

    if (!message.hasImsi) {
        error = std::string(label) + " does not contain IMSI";
        return false;
    }
    if (!message.hasNasPdu) {
        error = std::string(label) + " does not contain NAS PDU";
        return false;
    }

    return true;
}

}  // namespace

std::string formatS1apProcedureCode(uint8_t procedureCode) {
    switch (procedureCode) {
    case 0x0C:
        return "Initial UE Message (0x0C)";
    case 0x0D:
        return "Downlink NAS Transport (0x0D)";
    default: {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(procedureCode);
        return oss.str();
    }
    }
}

std::string formatNasMessageType(uint8_t messageType) {
    switch (messageType) {
    case 0x41:
        return "Attach Request (0x41)";
    case 0x52:
        return "Authentication Request (0x52)";
    case 0x53:
        return "Authentication Response (0x53)";
    case 0x5D:
        return "Security Mode Command (0x5D)";
    case 0x5E:
        return "Security Mode Complete (0x5E)";
    case 0x42:
        return "Attach Accept (0x42)";
    case 0x43:
        return "Attach Complete (0x43)";
    case 0x4C:
        return "Service Request (0x4C)";
    case 0x4D:
        return "Service Accept (0x4D)";
    case 0x4E:
        return "Service Release Request (0x4E)";
    case 0x4F:
        return "Service Release Complete (0x4F)";
    case 0x45:
        return "Detach Request (0x45)";
    case 0x46:
        return "Detach Accept (0x46)";
    case 0x48:
        return "Tracking Area Update Request (0x48)";
    case 0x49:
        return "Tracking Area Update Accept (0x49)";
    default: {
        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(messageType);
        return oss.str();
    }
    }
}

bool parseDemoInitialUeMessage(const std::vector<uint8_t>& packet,
                               DemoInitialUeMessage& message,
                               std::string& error) {
    return parseDemoNasCarrier(packet, 0x0C, "demo Initial UE Message", message, error);
}

bool parseDemoDownlinkNasTransport(const std::vector<uint8_t>& packet,
                                   DemoDownlinkNasTransport& message,
                                   std::string& error) {
    return parseDemoNasCarrier(packet, 0x0D, "demo Downlink NAS Transport", message, error);
}

bool parseNasAuthenticationRequest(const std::vector<uint8_t>& nasPdu,
                                   DemoNasAuthenticationRequest& request,
                                   std::string& error) {
    request = {};
    error.clear();

    if (nasPdu.size() < 2) {
        error = "demo Authentication Request is too short";
        return false;
    }
    if (nasPdu[0] != 0x52) {
        error = "unexpected NAS message type for Authentication Request: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    request.keySetIdentifier = nasPdu[1];
    request.hasKeySetIdentifier = true;
    return true;
}

bool parseNasAuthenticationResponse(const std::vector<uint8_t>& nasPdu,
                                    DemoNasAuthenticationResponse& response,
                                    std::string& error) {
    response = {};
    error.clear();

    if (nasPdu.size() < 2) {
        error = "demo Authentication Response is too short";
        return false;
    }
    if (nasPdu[0] != 0x53) {
        error = "unexpected NAS message type for Authentication Response: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    response.keySetIdentifier = nasPdu[1];
    response.hasKeySetIdentifier = true;
    return true;
}

bool parseNasSecurityModeCommand(const std::vector<uint8_t>& nasPdu,
                                 DemoNasSecurityModeCommand& command,
                                 std::string& error) {
    command = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Security Mode Command is too short";
        return false;
    }
    if (nasPdu[0] != 0x5D) {
        error = "unexpected NAS message type for Security Mode Command: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    command.keySetIdentifier = nasPdu[1];
    command.hasKeySetIdentifier = true;
    command.selectedAlgorithm = nasPdu[2];
    command.hasSelectedAlgorithm = true;
    return true;
}

bool parseNasSecurityModeComplete(const std::vector<uint8_t>& nasPdu,
                                  DemoNasSecurityModeComplete& complete,
                                  std::string& error) {
    complete = {};
    error.clear();

    if (nasPdu.size() < 2) {
        error = "demo Security Mode Complete is too short";
        return false;
    }
    if (nasPdu[0] != 0x5E) {
        error = "unexpected NAS message type for Security Mode Complete: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    complete.keySetIdentifier = nasPdu[1];
    complete.hasKeySetIdentifier = true;
    return true;
}

bool parseNasAttachAccept(const std::vector<uint8_t>& nasPdu,
                          DemoNasAttachAccept& accept,
                          std::string& error) {
    accept = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Attach Accept is too short";
        return false;
    }
    if (nasPdu[0] != 0x42) {
        error = "unexpected NAS message type for Attach Accept: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    accept.keySetIdentifier = nasPdu[1];
    accept.attachResult = nasPdu[2];
    accept.hasKeySetIdentifier = true;
    accept.hasAttachResult = true;
    return true;
}

bool parseNasAttachComplete(const std::vector<uint8_t>& nasPdu,
                            DemoNasAttachComplete& complete,
                            std::string& error) {
    complete = {};
    error.clear();

    if (nasPdu.size() < 2) {
        error = "demo Attach Complete is too short";
        return false;
    }
    if (nasPdu[0] != 0x43) {
        error = "unexpected NAS message type for Attach Complete: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    complete.keySetIdentifier = nasPdu[1];
    complete.hasKeySetIdentifier = true;
    return true;
}

bool parseNasServiceRequest(const std::vector<uint8_t>& nasPdu,
                            DemoNasServiceRequest& request,
                            std::string& error) {
    request = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Service Request is too short";
        return false;
    }
    if (nasPdu[0] != 0x4C) {
        error = "unexpected NAS message type for Service Request: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    request.keySetIdentifier = nasPdu[1];
    request.serviceType = nasPdu[2];
    request.hasKeySetIdentifier = true;
    request.hasServiceType = true;
    return true;
}

bool parseNasServiceAccept(const std::vector<uint8_t>& nasPdu,
                           DemoNasServiceAccept& accept,
                           std::string& error) {
    accept = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Service Accept is too short";
        return false;
    }
    if (nasPdu[0] != 0x4D) {
        error = "unexpected NAS message type for Service Accept: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    accept.keySetIdentifier = nasPdu[1];
    accept.bearerId = nasPdu[2];
    accept.hasKeySetIdentifier = true;
    accept.hasBearerId = true;
    return true;
}

bool parseNasServiceReleaseRequest(const std::vector<uint8_t>& nasPdu,
                                   DemoNasServiceReleaseRequest& request,
                                   std::string& error) {
    request = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Service Release Request is too short";
        return false;
    }
    if (nasPdu[0] != 0x4E) {
        error = "unexpected NAS message type for Service Release Request: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    request.keySetIdentifier = nasPdu[1];
    request.releaseCause = nasPdu[2];
    request.hasKeySetIdentifier = true;
    request.hasReleaseCause = true;
    return true;
}

bool parseNasServiceReleaseComplete(const std::vector<uint8_t>& nasPdu,
                                    DemoNasServiceReleaseComplete& complete,
                                    std::string& error) {
    complete = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Service Release Complete is too short";
        return false;
    }
    if (nasPdu[0] != 0x4F) {
        error = "unexpected NAS message type for Service Release Complete: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    complete.keySetIdentifier = nasPdu[1];
    complete.releaseResult = nasPdu[2];
    complete.hasKeySetIdentifier = true;
    complete.hasReleaseResult = true;
    return true;
}

bool parseNasDetachRequest(const std::vector<uint8_t>& nasPdu,
                           DemoNasDetachRequest& request,
                           std::string& error) {
    request = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Detach Request is too short";
        return false;
    }
    if (nasPdu[0] != 0x45) {
        error = "unexpected NAS message type for Detach Request: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    request.keySetIdentifier = nasPdu[1];
    request.detachType = nasPdu[2];
    request.hasKeySetIdentifier = true;
    request.hasDetachType = true;
    return true;
}

bool parseNasDetachAccept(const std::vector<uint8_t>& nasPdu,
                          DemoNasDetachAccept& accept,
                          std::string& error) {
    accept = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Detach Accept is too short";
        return false;
    }
    if (nasPdu[0] != 0x46) {
        error = "unexpected NAS message type for Detach Accept: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    accept.keySetIdentifier = nasPdu[1];
    accept.detachResult = nasPdu[2];
    accept.hasKeySetIdentifier = true;
    accept.hasDetachResult = true;
    return true;
}

bool parseNasTrackingAreaUpdateRequest(const std::vector<uint8_t>& nasPdu,
                                       DemoNasTrackingAreaUpdateRequest& request,
                                       std::string& error) {
    request = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Tracking Area Update Request is too short";
        return false;
    }
    if (nasPdu[0] != 0x48) {
        error = "unexpected NAS message type for Tracking Area Update Request: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    request.keySetIdentifier = nasPdu[1];
    request.trackingAreaCode = nasPdu[2];
    request.hasKeySetIdentifier = true;
    request.hasTrackingAreaCode = true;
    return true;
}

bool parseNasTrackingAreaUpdateAccept(const std::vector<uint8_t>& nasPdu,
                                      DemoNasTrackingAreaUpdateAccept& accept,
                                      std::string& error) {
    accept = {};
    error.clear();

    if (nasPdu.size() < 3) {
        error = "demo Tracking Area Update Accept is too short";
        return false;
    }
    if (nasPdu[0] != 0x49) {
        error = "unexpected NAS message type for Tracking Area Update Accept: " + formatNasMessageType(nasPdu[0]);
        return false;
    }

    accept.keySetIdentifier = nasPdu[1];
    accept.trackingAreaCode = nasPdu[2];
    accept.hasKeySetIdentifier = true;
    accept.hasTrackingAreaCode = true;
    return true;
}

std::vector<uint8_t> buildNasAuthenticationRequest(uint8_t keySetIdentifier) {
    return {
        0x52,
        keySetIdentifier,
    };
}

std::vector<uint8_t> buildNasSecurityModeCommand(uint8_t keySetIdentifier,
                                                 uint8_t selectedAlgorithm) {
    return {
        0x5D,
        keySetIdentifier,
        selectedAlgorithm,
    };
}

std::vector<uint8_t> buildNasAttachAccept(uint8_t keySetIdentifier,
                                          uint8_t attachResult) {
    return {
        0x42,
        keySetIdentifier,
        attachResult,
    };
}

std::vector<uint8_t> buildNasServiceAccept(uint8_t keySetIdentifier,
                                           uint8_t bearerId) {
    return {
        0x4D,
        keySetIdentifier,
        bearerId,
    };
}

std::vector<uint8_t> buildNasServiceReleaseComplete(uint8_t keySetIdentifier,
                                                    uint8_t releaseResult) {
    return {
        0x4F,
        keySetIdentifier,
        releaseResult,
    };
}

std::vector<uint8_t> buildNasDetachAccept(uint8_t keySetIdentifier,
                                          uint8_t detachResult) {
    return {
        0x46,
        keySetIdentifier,
        detachResult,
    };
}

std::vector<uint8_t> buildNasTrackingAreaUpdateAccept(uint8_t keySetIdentifier,
                                                      uint8_t trackingAreaCode) {
    return {
        0x49,
        keySetIdentifier,
        trackingAreaCode,
    };
}

std::vector<uint8_t> buildDemoDownlinkNasTransport(const std::string& imsi,
                                                   const std::string& guti,
                                                   const std::vector<uint8_t>& nasPdu) {
    std::vector<uint8_t> packet;
    packet.reserve(1 + 2 + imsi.size() + 2 + guti.size() + 2 + nasPdu.size());

    packet.push_back(0x0D);

    packet.push_back(0x01);
    packet.push_back(static_cast<uint8_t>(imsi.size()));
    packet.insert(packet.end(), imsi.begin(), imsi.end());

    if (!guti.empty()) {
        packet.push_back(0x02);
        packet.push_back(static_cast<uint8_t>(guti.size()));
        packet.insert(packet.end(), guti.begin(), guti.end());
    }

    packet.push_back(0x03);
    packet.push_back(static_cast<uint8_t>(nasPdu.size()));
    packet.insert(packet.end(), nasPdu.begin(), nasPdu.end());
    return packet;
}

}  // namespace vepc