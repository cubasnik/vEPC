#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vepc {

struct DemoInitialUeMessage {
    uint8_t procedureCode = 0;
    std::string imsi;
    std::string guti;
    std::vector<uint8_t> nasPdu;
    uint8_t nasMessageType = 0;
    bool hasImsi = false;
    bool hasGuti = false;
    bool hasNasPdu = false;
};

std::string formatS1apProcedureCode(uint8_t procedureCode);
std::string formatNasMessageType(uint8_t messageType);
bool parseDemoInitialUeMessage(const std::vector<uint8_t>& packet,
                               DemoInitialUeMessage& message,
                               std::string& error);
std::vector<uint8_t> buildNasAuthenticationRequest(uint8_t keySetIdentifier = 0x01);
std::vector<uint8_t> buildDemoDownlinkNasTransport(const std::string& imsi,
                                                   const std::string& guti,
                                                   const std::vector<uint8_t>& nasPdu);

}  // namespace vepc