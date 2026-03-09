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

struct DemoNasServiceRequest {
    uint8_t keySetIdentifier = 0;
    uint8_t serviceType = 0;
    bool hasKeySetIdentifier = false;
    bool hasServiceType = false;
};

struct DemoNasServiceAccept {
    uint8_t keySetIdentifier = 0;
    uint8_t bearerId = 0;
    bool hasKeySetIdentifier = false;
    bool hasBearerId = false;
};

struct DemoNasServiceReleaseRequest {
    uint8_t keySetIdentifier = 0;
    uint8_t releaseCause = 0;
    bool hasKeySetIdentifier = false;
    bool hasReleaseCause = false;
};

struct DemoNasServiceReleaseComplete {
    uint8_t keySetIdentifier = 0;
    uint8_t releaseResult = 0;
    bool hasKeySetIdentifier = false;
    bool hasReleaseResult = false;
};

struct DemoNasDetachRequest {
    uint8_t keySetIdentifier = 0;
    uint8_t detachType = 0;
    bool hasKeySetIdentifier = false;
    bool hasDetachType = false;
};

struct DemoNasDetachAccept {
    uint8_t keySetIdentifier = 0;
    uint8_t detachResult = 0;
    bool hasKeySetIdentifier = false;
    bool hasDetachResult = false;
};

struct DemoNasTrackingAreaUpdateRequest {
    uint8_t keySetIdentifier = 0;
    uint8_t trackingAreaCode = 0;
    bool hasKeySetIdentifier = false;
    bool hasTrackingAreaCode = false;
};

struct DemoNasTrackingAreaUpdateAccept {
    uint8_t keySetIdentifier = 0;
    uint8_t trackingAreaCode = 0;
    bool hasKeySetIdentifier = false;
    bool hasTrackingAreaCode = false;
};

struct DemoNasTrackingAreaUpdateComplete {
    uint8_t keySetIdentifier = 0;
    bool hasKeySetIdentifier = false;
};

struct DemoNasServiceResumeRequest {
    uint8_t keySetIdentifier = 0;
    uint8_t resumeType = 0;
    bool hasKeySetIdentifier = false;
    bool hasResumeType = false;
};

struct DemoNasServiceResumeAccept {
    uint8_t keySetIdentifier = 0;
    uint8_t bearerId = 0;
    bool hasKeySetIdentifier = false;
    bool hasBearerId = false;
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
bool parseNasServiceRequest(const std::vector<uint8_t>& nasPdu,
                            DemoNasServiceRequest& request,
                            std::string& error);
bool parseNasServiceAccept(const std::vector<uint8_t>& nasPdu,
                           DemoNasServiceAccept& accept,
                           std::string& error);
bool parseNasServiceReleaseRequest(const std::vector<uint8_t>& nasPdu,
                                   DemoNasServiceReleaseRequest& request,
                                   std::string& error);
bool parseNasServiceReleaseComplete(const std::vector<uint8_t>& nasPdu,
                                    DemoNasServiceReleaseComplete& complete,
                                    std::string& error);
bool parseNasDetachRequest(const std::vector<uint8_t>& nasPdu,
                           DemoNasDetachRequest& request,
                           std::string& error);
bool parseNasDetachAccept(const std::vector<uint8_t>& nasPdu,
                          DemoNasDetachAccept& accept,
                          std::string& error);
bool parseNasTrackingAreaUpdateRequest(const std::vector<uint8_t>& nasPdu,
                                       DemoNasTrackingAreaUpdateRequest& request,
                                       std::string& error);
bool parseNasTrackingAreaUpdateAccept(const std::vector<uint8_t>& nasPdu,
                                      DemoNasTrackingAreaUpdateAccept& accept,
                                      std::string& error);
bool parseNasTrackingAreaUpdateComplete(const std::vector<uint8_t>& nasPdu,
                                        DemoNasTrackingAreaUpdateComplete& complete,
                                        std::string& error);
bool parseNasServiceResumeRequest(const std::vector<uint8_t>& nasPdu,
                                  DemoNasServiceResumeRequest& request,
                                  std::string& error);
bool parseNasServiceResumeAccept(const std::vector<uint8_t>& nasPdu,
                                 DemoNasServiceResumeAccept& accept,
                                 std::string& error);
std::vector<uint8_t> buildNasAuthenticationRequest(uint8_t keySetIdentifier = 0x01);
std::vector<uint8_t> buildNasSecurityModeCommand(uint8_t keySetIdentifier = 0x01,
                                                 uint8_t selectedAlgorithm = 0x01);
std::vector<uint8_t> buildNasAttachAccept(uint8_t keySetIdentifier = 0x01,
                                          uint8_t attachResult = 0x01);
std::vector<uint8_t> buildNasServiceAccept(uint8_t keySetIdentifier = 0x01,
                                           uint8_t bearerId = 0x05);
std::vector<uint8_t> buildNasServiceReleaseComplete(uint8_t keySetIdentifier = 0x01,
                                                    uint8_t releaseResult = 0x00);
std::vector<uint8_t> buildNasDetachAccept(uint8_t keySetIdentifier = 0x01,
                                          uint8_t detachResult = 0x00);
std::vector<uint8_t> buildNasTrackingAreaUpdateAccept(uint8_t keySetIdentifier = 0x01,
                                                      uint8_t trackingAreaCode = 0x11);
std::vector<uint8_t> buildNasServiceResumeAccept(uint8_t keySetIdentifier = 0x01,
                                                 uint8_t bearerId = 0x05);
std::vector<uint8_t> buildDemoDownlinkNasTransport(const std::string& imsi,
                                                   const std::string& guti,
                                                   const std::vector<uint8_t>& nasPdu);

}  // namespace vepc