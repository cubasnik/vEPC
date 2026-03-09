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

    const std::vector<uint8_t> updatePdpRequest = {
        0x32, 0x12, 0x00, 0x21,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x23, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x57,
        0x83, 0x00, 0x05, 0x04, 'c', 'o', 'r', 'p',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x63,
    };

    ok &= expect(vepc::parseGtpV1Header(updatePdpRequest, header, error), "update pdp request header parses");
    ok &= expect(vepc::parseUpdatePdpContextRequest(updatePdpRequest, header, request, error), "update pdp request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "update pdp parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "corp", "update pdp parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x57, "update pdp parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.99", "update pdp parser extracts GGSN IP");

    const std::vector<uint8_t> expectedUpdatePdpResponse = {
        0x32, 0x13, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x23, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildUpdatePdpContextResponse(header, 0x10004321u) == expectedUpdatePdpResponse,
                 "update pdp response bytes are stable");

    const std::vector<uint8_t> activatePdpRequest = {
        0x32, 0x16, 0x00, 0x25,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x24, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x33,
        0x83, 0x00, 0x09, 0x08, 'a', 'c', 't', 'i', 'v', 'a', 't', 'e',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x4D,
    };

    ok &= expect(vepc::parseGtpV1Header(activatePdpRequest, header, error), "activate pdp request header parses");
    ok &= expect(vepc::parseInitiatePdpContextActivationRequest(activatePdpRequest, header, request, error), "activate pdp request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "activate pdp parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "activate", "activate pdp parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x33, "activate pdp parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.77", "activate pdp parser extracts GGSN IP");

    const std::vector<uint8_t> expectedActivatePdpResponse = {
        0x32, 0x17, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x24, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildInitiatePdpContextActivationResponse(header, 0x10004321u) == expectedActivatePdpResponse,
                 "activate pdp response bytes are stable");

    const std::vector<uint8_t> pduNotificationRequest = {
        0x32, 0x1B, 0x00, 0x23,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x25, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x44,
        0x83, 0x00, 0x07, 0x06, 'n', 'o', 't', 'i', 'f', 'y',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x58,
    };

    ok &= expect(vepc::parseGtpV1Header(pduNotificationRequest, header, error), "pdu notification request header parses");
    ok &= expect(vepc::parsePduNotificationRequest(pduNotificationRequest, header, request, error), "pdu notification request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "pdu notification parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "notify", "pdu notification parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x44, "pdu notification parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.88", "pdu notification parser extracts GGSN IP");

    const std::vector<uint8_t> expectedPduNotificationResponse = {
        0x32, 0x1C, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x25, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildPduNotificationResponse(header, 0x10004321u) == expectedPduNotificationResponse,
                 "pdu notification response bytes are stable");

    const std::vector<uint8_t> pduNotificationRejectRequest = {
        0x32, 0x1D, 0x00, 0x23,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x26, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x45,
        0x83, 0x00, 0x07, 0x06, 'r', 'e', 'j', 'e', 'c', 't',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x59,
    };

    ok &= expect(vepc::parseGtpV1Header(pduNotificationRejectRequest, header, error), "pdu notification reject request header parses");
    ok &= expect(vepc::parsePduNotificationRejectRequest(pduNotificationRejectRequest, header, request, error), "pdu notification reject request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "pdu notification reject parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "reject", "pdu notification reject parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x45, "pdu notification reject parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.89", "pdu notification reject parser extracts GGSN IP");

    const std::vector<uint8_t> expectedPduNotificationRejectResponse = {
        0x32, 0x1E, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x26, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildPduNotificationRejectResponse(header, 0x10004321u) == expectedPduNotificationRejectResponse,
                 "pdu notification reject response bytes are stable");

    const std::vector<uint8_t> failureReportRequest = {
        0x32, 0x22, 0x00, 0x22,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x27, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x46,
        0x83, 0x00, 0x06, 0x05, 'a', 'l', 'a', 'r', 'm',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x5A,
    };

    ok &= expect(vepc::parseGtpV1Header(failureReportRequest, header, error), "failure report request header parses");
    ok &= expect(vepc::parseFailureReportRequest(failureReportRequest, header, request, error), "failure report request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "failure report parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "alarm", "failure report parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x46, "failure report parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.90", "failure report parser extracts GGSN IP");

    const std::vector<uint8_t> expectedFailureReportResponse = {
        0x32, 0x23, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x27, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildFailureReportResponse(header, 0x10004321u) == expectedFailureReportResponse,
                 "failure report response bytes are stable");

    const std::vector<uint8_t> noteMsGprsPresentRequest = {
        0x32, 0x24, 0x00, 0x23,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x28, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x47,
        0x83, 0x00, 0x07, 0x06, 'p', 'r', 'e', 's', 'e', 'n',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x5B,
    };

    ok &= expect(vepc::parseGtpV1Header(noteMsGprsPresentRequest, header, error), "note ms gprs present request header parses");
    ok &= expect(vepc::parseNoteMsGprsPresentRequest(noteMsGprsPresentRequest, header, request, error), "note ms gprs present request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "note ms gprs present parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "presen", "note ms gprs present parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x47, "note ms gprs present parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.91", "note ms gprs present parser extracts GGSN IP");

    const std::vector<uint8_t> expectedNoteMsGprsPresentResponse = {
        0x32, 0x25, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x28, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildNoteMsGprsPresentResponse(header, 0x10004321u) == expectedNoteMsGprsPresentResponse,
                 "note ms gprs present response bytes are stable");

    const std::vector<uint8_t> identificationRequest = {
        0x32, 0x30, 0x00, 0x22,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x29, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x48,
        0x83, 0x00, 0x06, 0x05, 'i', 'd', 'e', 'n', 't',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x5C,
    };

    ok &= expect(vepc::parseGtpV1Header(identificationRequest, header, error), "identification request header parses");
    ok &= expect(vepc::parseIdentificationRequest(identificationRequest, header, request, error), "identification request body parses");
    ok &= expect(request.hasImsi && request.imsi == "123456789012345", "identification parser extracts IMSI");
    ok &= expect(request.hasApn && request.apn == "ident", "identification parser extracts APN");
    ok &= expect(request.hasPdpType && request.pdpType == 0x48, "identification parser extracts PDP type");
    ok &= expect(request.hasGgsnIp && request.ggsnIp == "10.23.42.92", "identification parser extracts GGSN IP");

    const std::vector<uint8_t> expectedIdentificationResponse = {
        0x32, 0x31, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x29, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildIdentificationResponse(header, 0x10004321u) == expectedIdentificationResponse,
                 "identification response bytes are stable");

    const std::vector<uint8_t> deletePdpRequest = {
        0x32, 0x14, 0x00, 0x04,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x22, 0x00, 0x00,
    };

    ok &= expect(vepc::parseGtpV1Header(deletePdpRequest, header, error), "delete pdp request header parses");
    ok &= expect(header.messageType == 0x14, "delete pdp parser extracts message type");
    ok &= expect(header.teid == 0x10004321u, "delete pdp parser extracts teid");
    ok &= expect(header.sequence == 0x4322, "delete pdp parser extracts sequence");

    const std::vector<uint8_t> expectedDeletePdpResponse = {
        0x32, 0x15, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x22, 0x00, 0x00,
        0x01, 0x80,
    };
    ok &= expect(vepc::buildDeletePdpContextResponse(header) == expectedDeletePdpResponse,
                 "delete pdp response bytes are stable");

    return ok ? 0 : 1;
}