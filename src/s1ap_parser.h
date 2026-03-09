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

struct DemoDownlinkNasTransport {
    uint8_t procedureCode = 0;
    std::string imsi;
    std::string guti;
    std::vector<uint8_t> nasPdu;
    uint8_t nasMessageType = 0;
    bool hasImsi = false;
    bool hasGuti = false;
    bool hasNasPdu = false;
};

struct DemoNasAuthenticationRequest {
    uint8_t keySetIdentifier = 0;
    bool hasKeySetIdentifier = false;
};

struct DemoNasAuthenticationResponse {
    uint8_t keySetIdentifier = 0;
    bool hasKeySetIdentifier = false;
};

struct DemoNasSecurityModeCommand {
    uint8_t keySetIdentifier = 0;
    uint8_t selectedAlgorithm = 0;
    bool hasKeySetIdentifier = false;
    bool hasSelectedAlgorithm = false;
};

struct DemoNasSecurityModeComplete {
    uint8_t keySetIdentifier = 0;
    bool hasKeySetIdentifier = false;
};

struct DemoNasAttachAccept {
    uint8_t keySetIdentifier = 0;
    uint8_t attachResult = 0;
    bool hasKeySetIdentifier = false;
    bool hasAttachResult = false;
};

struct DemoNasAttachComplete {
    uint8_t keySetIdentifier = 0;
    bool hasKeySetIdentifier = false;
};

std::string formatS1apProcedureCode(uint8_t procedureCode);
std::string formatNasMessageType(uint8_t messageType);
bool parseDemoInitialUeMessage(const std::vector<uint8_t>& packet,
                               DemoInitialUeMessage& message,
                               std::string& error);
bool parseDemoDownlinkNasTransport(const std::vector<uint8_t>& packet,
                                   DemoDownlinkNasTransport& message,
                                   std::string& error);
bool parseNasAuthenticationRequest(const std::vector<uint8_t>& nasPdu,
                                   DemoNasAuthenticationRequest& request,
                                   std::string& error);
bool parseNasAuthenticationResponse(const std::vector<uint8_t>& nasPdu,
                                    DemoNasAuthenticationResponse& response,
                                    std::string& error);
bool parseNasSecurityModeCommand(const std::vector<uint8_t>& nasPdu,
                                 DemoNasSecurityModeCommand& command,
                                 std::string& error);
bool parseNasSecurityModeComplete(const std::vector<uint8_t>& nasPdu,
                                  DemoNasSecurityModeComplete& complete,
                                  std::string& error);
bool parseNasAttachAccept(const std::vector<uint8_t>& nasPdu,
                          DemoNasAttachAccept& accept,
                          std::string& error);
bool parseNasAttachComplete(const std::vector<uint8_t>& nasPdu,
                            DemoNasAttachComplete& complete,
                            std::string& error);
std::vector<uint8_t> buildNasAuthenticationRequest(uint8_t keySetIdentifier = 0x01);
std::vector<uint8_t> buildNasSecurityModeCommand(uint8_t keySetIdentifier = 0x01,
                                                 uint8_t selectedAlgorithm = 0x01);
std::vector<uint8_t> buildNasAttachAccept(uint8_t keySetIdentifier = 0x01,
                                          uint8_t attachResult = 0x01);
std::vector<uint8_t> buildDemoDownlinkNasTransport(const std::string& imsi,
                                                   const std::string& guti,
                                                   const std::vector<uint8_t>& nasPdu);

}  // namespace vepc