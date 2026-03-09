#include "src/s1ap_parser.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        return false;
    }

    std::cout << "PASS: " << label << "\n";
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    std::string error;
    vepc::DemoInitialUeMessage message;

    const std::vector<uint8_t> initialUeMessage = {
        0x0C,
        0x01, 0x0F, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
        0x02, 0x08, 'g', 'u', 't', 'i', '-', '0', '0', '1',
        0x03, 0x03, 0x41, 0x01, 0x02,
    };

    ok &= expect(vepc::parseDemoInitialUeMessage(initialUeMessage, message, error), "demo Initial UE Message parses");
    ok &= expect(message.procedureCode == 0x0C, "parser extracts Initial UE Message procedure code");
    ok &= expect(message.hasImsi && message.imsi == "123456789012345", "parser extracts IMSI");
    ok &= expect(message.hasGuti && message.guti == "guti-001", "parser extracts GUTI");
    ok &= expect(message.hasNasPdu && message.nasMessageType == 0x41, "parser extracts NAS message type");
    ok &= expect(vepc::formatNasMessageType(message.nasMessageType) == "Attach Request (0x41)", "NAS formatter returns stable label");
    ok &= expect(vepc::formatS1apProcedureCode(message.procedureCode) == "Initial UE Message (0x0C)", "S1AP formatter returns stable label");
    ok &= expect(vepc::formatS1apProcedureCode(0x0D) == "Downlink NAS Transport (0x0D)", "downlink NAS transport formatter returns stable label");

    const std::vector<uint8_t> expectedAuthRequest = {0x52, 0x01};
    ok &= expect(vepc::buildNasAuthenticationRequest() == expectedAuthRequest,
                 "authentication request bytes are stable");

    const std::vector<uint8_t> expectedDownlinkTransport = {
        0x0D,
        0x01, 0x0F, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
        0x02, 0x08, 'g', 'u', 't', 'i', '-', '0', '0', '1',
        0x03, 0x02, 0x52, 0x01,
    };
    ok &= expect(vepc::buildDemoDownlinkNasTransport("123456789012345", "guti-001", expectedAuthRequest)
                     == expectedDownlinkTransport,
                 "downlink NAS transport bytes are stable");

    const std::vector<uint8_t> invalidProcedure = {
        0x0B,
        0x01, 0x03, '1', '2', '3',
        0x03, 0x01, 0x41,
    };
    ok &= expect(!vepc::parseDemoInitialUeMessage(invalidProcedure, message, error),
                 "unsupported S1AP procedure is rejected");
    ok &= expect(error.find("unsupported S1AP procedure code") != std::string::npos,
                 "unsupported procedure error is descriptive");

    const std::vector<uint8_t> truncatedNas = {
        0x0C,
        0x01, 0x03, '1', '2', '3',
        0x03, 0x02, 0x41,
    };
    ok &= expect(!vepc::parseDemoInitialUeMessage(truncatedNas, message, error),
                 "truncated demo S1AP TLV is rejected");
    ok &= expect(error.find("truncated demo S1AP TLV payload") != std::string::npos,
                 "truncated S1AP error is descriptive");

    return ok ? 0 : 1;
}