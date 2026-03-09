#include "src/gtp_parser.h"

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
    vepc::GtpV1Header header;
    std::string error;

    const std::vector<uint8_t> validEchoRequest = {
        0x32, 0x01, 0x00, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x00, 0x00,
    };

    ok &= expect(vepc::parseGtpV1Header(validEchoRequest, header, error), "valid echo request parses");
    ok &= expect(header.version == 1, "parser extracts GTP version");
    ok &= expect(header.messageType == 0x01, "parser extracts message type");
    ok &= expect(header.payloadLength == 4, "parser extracts payload length");
    ok &= expect(header.teid == 0, "parser extracts TEID");
    ok &= expect(header.sequence == 0x1234, "parser extracts sequence");
    ok &= expect(header.headerLength == 12, "parser accounts for optional fields");

    const std::vector<uint8_t> expectedEchoResponse = {
        0x32, 0x02, 0x00, 0x06,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x00, 0x00,
        0x0E, 0x00,
    };
    ok &= expect(vepc::buildEchoResponse(header) == expectedEchoResponse, "echo response bytes are stable");

    const std::vector<uint8_t> invalidVersion = {
        0x12, 0x01, 0x00, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x00, 0x00,
    };

    ok &= expect(!vepc::parseGtpV1Header(invalidVersion, header, error), "invalid version is rejected");
    ok &= expect(error.find("unsupported GTP version") != std::string::npos, "invalid version error is descriptive");

    const std::vector<uint8_t> truncatedPacket = {
        0x32, 0x10, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x01,
        0x22, 0x22, 0x00, 0x00,
    };

    ok &= expect(!vepc::parseGtpV1Header(truncatedPacket, header, error), "truncated packet is rejected");
    ok &= expect(error.find("declared GTP length") != std::string::npos, "truncated packet error is descriptive");

    const std::vector<uint8_t> shortPacket = {0x32, 0x01, 0x00, 0x00};
    ok &= expect(!vepc::parseGtpV1Header(shortPacket, header, error), "short packet is rejected");

    const std::vector<uint8_t> createPdpRequest = {
        0x32, 0x10, 0x00, 0x25,
        0x00, 0x00, 0x00, 0x00,
        0x43, 0x21, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x21,
        0x83, 0x00, 0x09, 0x08, 'i', 'n', 't', 'e', 'r', 'n', 'e', 't',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x05,
    };

    vepc::CreatePdpRequestInfo request;
    ok &= expect(vepc::parseGtpV1Header(createPdpRequest, header, error), "create pdp request header parses");
    ok &= expect(vepc::parseCreatePdpContextRequest(createPdpRequest, header, request, error), "create pdp request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "create pdp parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "internet", "create pdp parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x21, "create pdp parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.5", "create pdp parser extracts GGSN IP");

    const std::vector<uint8_t> expectedCreatePdpResponse = {
        0x32, 0x11, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x21, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildCreatePdpContextResponse(header, 0x10004321u) == expectedCreatePdpResponse,
                 "create pdp response bytes are stable");

    return ok ? 0 : 1;
}