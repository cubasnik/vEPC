#include "src/diameter_parser.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << "FAIL: " << message << "\n";
    return false;
}

}  // namespace

int main() {
    bool ok = true;
    vepc::DiameterHeader header;
    vepc::DiameterCapabilitiesExchangeRequest cerRequest;
    std::string error;

    const std::vector<uint8_t> expectedCer = {
        0x01, 0x00, 0x00, 0x58,
        0x80, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0,
        0x00, 0x00, 0x01, 0x08,
        0x40, 0x00, 0x00, 0x16,
        'm', 'm', 'e', '.', 'v', 'e', 'p', 'c', '.', 'l', 'o', 'c', 'a', 'l',
        0x00, 0x00,
        0x00, 0x00, 0x01, 0x28,
        0x40, 0x00, 0x00, 0x29,
        'e', 'p', 'c', '.', 'm', 'n', 'c', '0', '0', '1', '.', 'm', 'c', 'c', '0', '0', '1', '.',
        '3', 'g', 'p', 'p', 'n', 'e', 't', 'w', 'o', 'r', 'k', '.', 'o', 'r', 'g',
        0x00, 0x00, 0x00,
    };
    const std::vector<uint8_t> cer = vepc::buildCapabilitiesExchangeRequest("mme.vepc.local",
                                                                             "epc.mnc001.mcc001.3gppnetwork.org");

    ok &= expect(cer == expectedCer, "diameter cer request bytes are stable");
    ok &= expect(vepc::parseDiameterHeader(cer, header, error), "diameter cer header parses");
    ok &= expect(header.version == 1, "diameter parser extracts version");
    ok &= expect(header.messageLength == 88, "diameter parser extracts message length");
    ok &= expect(header.request, "diameter parser extracts request flag");
    ok &= expect(header.commandCode == 257, "diameter parser extracts command code");
    ok &= expect(header.applicationId == 0, "diameter parser extracts application id");
    ok &= expect(header.hopByHopId == 0x12345678u, "diameter parser extracts hop-by-hop id");
    ok &= expect(header.endToEndId == 0x9ABCDEF0u, "diameter parser extracts end-to-end id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Capabilities-Exchange-Request (CER)",
                 "diameter formatter recognizes CER");
    ok &= expect(vepc::parseCapabilitiesExchangeRequest(cer, cerRequest, error), "diameter cer avps parse");
    ok &= expect(cerRequest.hasOriginHost && cerRequest.originHost == "mme.vepc.local",
                 "diameter cer origin-host parses");
    ok &= expect(cerRequest.hasOriginRealm && cerRequest.originRealm == "epc.mnc001.mcc001.3gppnetwork.org",
                 "diameter cer origin-realm parses");

    const std::vector<uint8_t> expectedCea = {
        0x01, 0x00, 0x00, 0x64,
        0x00, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0,
        0x00, 0x00, 0x01, 0x0C,
        0x40, 0x00, 0x00, 0x0C,
        0x00, 0x00, 0x07, 0xD1,
        0x00, 0x00, 0x01, 0x08,
        0x40, 0x00, 0x00, 0x16,
        'h', 's', 's', '.', 'v', 'e', 'p', 'c', '.', 'l', 'o', 'c', 'a', 'l',
        0x00, 0x00,
        0x00, 0x00, 0x01, 0x28,
        0x40, 0x00, 0x00, 0x29,
        'e', 'p', 'c', '.', 'm', 'n', 'c', '0', '0', '1', '.', 'm', 'c', 'c', '0', '0', '1', '.',
        '3', 'g', 'p', 'p', 'n', 'e', 't', 'w', 'o', 'r', 'k', '.', 'o', 'r', 'g',
        0x00, 0x00, 0x00,
    };
    ok &= expect(vepc::buildCapabilitiesExchangeAnswer(header,
                                                       "hss.vepc.local",
                                                       "epc.mnc001.mcc001.3gppnetwork.org") == expectedCea,
                 "diameter cea response bytes are stable");

    ok &= expect(vepc::parseDiameterHeader(expectedCea, header, error), "diameter cea header parses");
    ok &= expect(!header.request, "diameter parser extracts answer flag");
    ok &= expect(header.messageLength == 100, "diameter parser extracts cea message length");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Capabilities-Exchange-Answer (CEA)",
                 "diameter formatter recognizes CEA");

    const std::vector<uint8_t> invalidVersion = {
        0x02, 0x00, 0x00, 0x14,
        0x80, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0,
    };
    ok &= expect(!vepc::parseDiameterHeader(invalidVersion, header, error), "invalid diameter version is rejected");
    ok &= expect(error.find("unsupported Diameter version") != std::string::npos, "invalid diameter version error is descriptive");

    const std::vector<uint8_t> truncated = {
        0x01, 0x00, 0x00, 0x5C,
        0x80, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x12, 0x34, 0x56, 0x78,
        0x9A, 0xBC, 0xDE, 0xF0,
    };
    ok &= expect(!vepc::parseDiameterHeader(truncated, header, error), "truncated diameter packet is rejected");
    ok &= expect(error.find("declared Diameter length") != std::string::npos, "truncated diameter error is descriptive");

    std::vector<uint8_t> malformedCer = cer;
    malformedCer[25] = 0x00;
    malformedCer[26] = 0x00;
    malformedCer[27] = 0x04;
    ok &= expect(!vepc::parseCapabilitiesExchangeRequest(malformedCer, cerRequest, error), "malformed cer avp length is rejected");
    ok &= expect(error.find("invalid Diameter AVP length") != std::string::npos, "malformed cer avp error is descriptive");

    // DWR builder / parser tests
    const std::vector<uint8_t> dwr = vepc::buildWatchdogRequest("mme.vepc.local");
    ok &= expect(vepc::parseDiameterHeader(dwr, header, error), "diameter dwr header parses");
    ok &= expect(header.commandCode == 280, "diameter dwr has command 280");
    ok &= expect(header.request, "diameter dwr has request flag");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Device-Watchdog-Request (DWR)",
                 "diameter formatter recognizes DWR");

    vepc::DiameterWatchdogRequest dwrRequest;
    ok &= expect(vepc::parseWatchdogRequest(dwr, dwrRequest, error), "diameter dwr avps parse");
    ok &= expect(dwrRequest.hasOriginHost && dwrRequest.originHost == "mme.vepc.local",
                 "diameter dwr origin-host parses");

    // DWA builder test
    const std::vector<uint8_t> dwa = vepc::buildWatchdogAnswer(header, "hss.vepc.local");
    ok &= expect(vepc::parseDiameterHeader(dwa, header, error), "diameter dwa header parses");
    ok &= expect(header.commandCode == 280, "diameter dwa has command 280");
    ok &= expect(!header.request, "diameter dwa has answer flag");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Device-Watchdog-Answer (DWA)",
                 "diameter formatter recognizes DWA");

    // DPR builder / parser tests
    const std::vector<uint8_t> dpr = vepc::buildDisconnectPeerRequest("mme.vepc.local", 0);
    ok &= expect(vepc::parseDiameterHeader(dpr, header, error), "diameter dpr header parses");
    ok &= expect(header.commandCode == 282, "diameter dpr has command 282");
    ok &= expect(header.request, "diameter dpr has request flag");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Disconnect-Peer-Request (DPR)",
                 "diameter formatter recognizes DPR");

    vepc::DiameterDisconnectPeerRequest dprRequest;
    ok &= expect(vepc::parseDisconnectPeerRequest(dpr, dprRequest, error), "diameter dpr avps parse");
    ok &= expect(dprRequest.hasOriginHost && dprRequest.originHost == "mme.vepc.local",
                 "diameter dpr origin-host parses");
    ok &= expect(dprRequest.hasDisconnectCause && dprRequest.disconnectCause == 0,
                 "diameter dpr disconnect-cause parses");

    // DPA builder test
    const std::vector<uint8_t> dpa = vepc::buildDisconnectPeerAnswer(header, "hss.vepc.local");
    ok &= expect(vepc::parseDiameterHeader(dpa, header, error), "diameter dpa header parses");
    ok &= expect(header.commandCode == 282, "diameter dpa has command 282");
    ok &= expect(!header.request, "diameter dpa has answer flag");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Disconnect-Peer-Answer (DPA)",
                 "diameter formatter recognizes DPA");

    // AIR builder / parser tests
    const std::vector<uint8_t> air = vepc::buildAuthInfoRequest("mme.vepc.local",
                                                                 "epc.mnc001.mcc001.3gppnetwork.org",
                                                                 "001010123456789");
    ok &= expect(vepc::parseDiameterHeader(air, header, error), "diameter air header parses");
    ok &= expect(header.commandCode == 318, "diameter air has command 318");
    ok &= expect(header.request, "diameter air has request flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter air has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Authentication-Information-Request (AIR)",
                 "diameter formatter recognizes AIR");

    vepc::DiameterAuthInfoRequest airRequest;
    ok &= expect(vepc::parseAuthInfoRequest(air, airRequest, error), "diameter air avps parse");
    ok &= expect(airRequest.hasOriginHost && airRequest.originHost == "mme.vepc.local",
                 "diameter air origin-host parses");
    ok &= expect(airRequest.hasOriginRealm && airRequest.originRealm == "epc.mnc001.mcc001.3gppnetwork.org",
                 "diameter air origin-realm parses");
    ok &= expect(airRequest.hasUserName && airRequest.userName == "001010123456789",
                 "diameter air user-name parses");

    // AIA builder test
    const std::vector<uint8_t> aia = vepc::buildAuthInfoAnswer(header, "hss.vepc.local",
                                                                "epc.mnc001.mcc001.3gppnetwork.org");
    ok &= expect(vepc::parseDiameterHeader(aia, header, error), "diameter aia header parses");
    ok &= expect(header.commandCode == 318, "diameter aia has command 318");
    ok &= expect(!header.request, "diameter aia has answer flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter aia has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Authentication-Information-Answer (AIA)",
                 "diameter formatter recognizes AIA");

    // ULR builder / parser tests
    const std::vector<uint8_t> ulr = vepc::buildUpdateLocationRequest("mme.vepc.local",
                                                                       "epc.mnc001.mcc001.3gppnetwork.org",
                                                                       "001010123456789");
    ok &= expect(vepc::parseDiameterHeader(ulr, header, error), "diameter ulr header parses");
    ok &= expect(header.commandCode == 316, "diameter ulr has command 316");
    ok &= expect(header.request, "diameter ulr has request flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter ulr has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Update-Location-Request (ULR)",
                 "diameter formatter recognizes ULR");

    vepc::DiameterUpdateLocationRequest ulrRequest;
    ok &= expect(vepc::parseUpdateLocationRequest(ulr, ulrRequest, error), "diameter ulr avps parse");
    ok &= expect(ulrRequest.hasOriginHost && ulrRequest.originHost == "mme.vepc.local",
                 "diameter ulr origin-host parses");
    ok &= expect(ulrRequest.hasOriginRealm && ulrRequest.originRealm == "epc.mnc001.mcc001.3gppnetwork.org",
                 "diameter ulr origin-realm parses");
    ok &= expect(ulrRequest.hasUserName && ulrRequest.userName == "001010123456789",
                 "diameter ulr user-name parses");

    // ULA builder test
    const std::vector<uint8_t> ula = vepc::buildUpdateLocationAnswer(header, "hss.vepc.local",
                                                                      "epc.mnc001.mcc001.3gppnetwork.org");
    ok &= expect(vepc::parseDiameterHeader(ula, header, error), "diameter ula header parses");
    ok &= expect(header.commandCode == 316, "diameter ula has command 316");
    ok &= expect(!header.request, "diameter ula has answer flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter ula has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Update-Location-Answer (ULA)",
                 "diameter formatter recognizes ULA");

    // PUR builder / parser tests
    const std::vector<uint8_t> pur = vepc::buildPurgeUeRequest("mme.vepc.local",
                                                                "epc.mnc001.mcc001.3gppnetwork.org",
                                                                "001010123456789");
    ok &= expect(vepc::parseDiameterHeader(pur, header, error), "diameter pur header parses");
    ok &= expect(header.commandCode == 321, "diameter pur has command 321");
    ok &= expect(header.request, "diameter pur has request flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter pur has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Purge-UE-Request (PUR)",
                 "diameter formatter recognizes PUR");

    vepc::DiameterPurgeUeRequest purRequest;
    ok &= expect(vepc::parsePurgeUeRequest(pur, purRequest, error), "diameter pur avps parse");
    ok &= expect(purRequest.hasOriginHost && purRequest.originHost == "mme.vepc.local",
                 "diameter pur origin-host parses");
    ok &= expect(purRequest.hasOriginRealm && purRequest.originRealm == "epc.mnc001.mcc001.3gppnetwork.org",
                 "diameter pur origin-realm parses");
    ok &= expect(purRequest.hasUserName && purRequest.userName == "001010123456789",
                 "diameter pur user-name parses");

    // PUA builder test
    const std::vector<uint8_t> pua = vepc::buildPurgeUeAnswer(header, "hss.vepc.local",
                                                               "epc.mnc001.mcc001.3gppnetwork.org");
    ok &= expect(vepc::parseDiameterHeader(pua, header, error), "diameter pua header parses");
    ok &= expect(header.commandCode == 321, "diameter pua has command 321");
    ok &= expect(!header.request, "diameter pua has answer flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter pua has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Purge-UE-Answer (PUA)",
                 "diameter formatter recognizes PUA");

    // CLR builder / parser tests
    const std::vector<uint8_t> clr = vepc::buildCancelLocationRequest("mme.vepc.local",
                                                                       "epc.mnc001.mcc001.3gppnetwork.org",
                                                                       "001010123456789");
    ok &= expect(vepc::parseDiameterHeader(clr, header, error), "diameter clr header parses");
    ok &= expect(header.commandCode == 317, "diameter clr has command 317");
    ok &= expect(header.request, "diameter clr has request flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter clr has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Cancel-Location-Request (CLR)",
                 "diameter formatter recognizes CLR");

    vepc::DiameterCancelLocationRequest clrRequest;
    ok &= expect(vepc::parseCancelLocationRequest(clr, clrRequest, error), "diameter clr avps parse");
    ok &= expect(clrRequest.hasOriginHost && clrRequest.originHost == "mme.vepc.local",
                 "diameter clr origin-host parses");
    ok &= expect(clrRequest.hasOriginRealm && clrRequest.originRealm == "epc.mnc001.mcc001.3gppnetwork.org",
                 "diameter clr origin-realm parses");
    ok &= expect(clrRequest.hasUserName && clrRequest.userName == "001010123456789",
                 "diameter clr user-name parses");

    // CLA builder test
    const std::vector<uint8_t> cla = vepc::buildCancelLocationAnswer(header, "hss.vepc.local",
                                                                      "epc.mnc001.mcc001.3gppnetwork.org");
    ok &= expect(vepc::parseDiameterHeader(cla, header, error), "diameter cla header parses");
    ok &= expect(header.commandCode == 317, "diameter cla has command 317");
    ok &= expect(!header.request, "diameter cla has answer flag");
    ok &= expect(header.applicationId == vepc::kS6aApplicationId, "diameter cla has s6a app id");
    ok &= expect(vepc::formatDiameterCommand(header.commandCode, header.request) == "Cancel-Location-Answer (CLA)",
                 "diameter formatter recognizes CLA");

    return ok ? 0 : 1;
}