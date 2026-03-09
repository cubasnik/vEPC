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
    message = {};
    error.clear();

    if (packet.size() < 3) {
        error = "packet too short for demo S1AP Initial UE Message";
        return false;
    }

    message.procedureCode = packet[0];
    if (message.procedureCode != 0x0C) {
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
        error = "demo Initial UE Message does not contain IMSI";
        return false;
    }
    if (!message.hasNasPdu) {
        error = "demo Initial UE Message does not contain NAS PDU";
        return false;
    }

    return true;
}

std::vector<uint8_t> buildNasAuthenticationRequest(uint8_t keySetIdentifier) {
    return {
        0x52,
        keySetIdentifier,
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