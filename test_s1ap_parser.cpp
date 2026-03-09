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
    vepc::DemoDownlinkNasTransport downlinkTransport;
    vepc::DemoNasAuthenticationRequest authRequest;
    vepc::DemoNasAuthenticationResponse authResponse;
    vepc::DemoNasSecurityModeCommand securityModeCommand;
    vepc::DemoNasSecurityModeComplete securityModeComplete;
    vepc::DemoNasAttachAccept attachAccept;
    vepc::DemoNasAttachComplete attachComplete;
    vepc::DemoNasServiceRequest serviceRequest;
    vepc::DemoNasServiceAccept serviceAccept;
    vepc::DemoNasServiceReleaseRequest serviceReleaseRequest;
    vepc::DemoNasServiceReleaseComplete serviceReleaseComplete;
    vepc::DemoNasDetachRequest detachRequest;
    vepc::DemoNasDetachAccept detachAcceptResult;
    vepc::DemoNasTrackingAreaUpdateRequest trackingAreaUpdateRequest;
    vepc::DemoNasTrackingAreaUpdateAccept trackingAreaUpdateAccept;
    vepc::DemoNasTrackingAreaUpdateComplete trackingAreaUpdateComplete;
    vepc::DemoNasServiceResumeRequest serviceResumeRequest;
    vepc::DemoNasServiceResumeAccept serviceResumeAccept;

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
    ok &= expect(vepc::formatNasMessageType(0x5D) == "Security Mode Command (0x5D)", "security mode command formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x5E) == "Security Mode Complete (0x5E)", "security mode complete formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x42) == "Attach Accept (0x42)", "attach accept formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x43) == "Attach Complete (0x43)", "attach complete formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x4C) == "Service Request (0x4C)", "service request formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x4D) == "Service Accept (0x4D)", "service accept formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x4E) == "Service Release Request (0x4E)", "service release request formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x4F) == "Service Release Complete (0x4F)", "service release complete formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x45) == "Detach Request (0x45)", "detach request formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x46) == "Detach Accept (0x46)", "detach accept formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x48) == "Tracking Area Update Request (0x48)", "tau request formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x49) == "Tracking Area Update Accept (0x49)", "tau accept formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x4A) == "Tracking Area Update Complete (0x4A)", "tau complete formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x50) == "Service Resume Request (0x50)", "service resume request formatter returns stable label");
    ok &= expect(vepc::formatNasMessageType(0x51) == "Service Resume Accept (0x51)", "service resume accept formatter returns stable label");

    const std::vector<uint8_t> expectedAuthRequest = {0x52, 0x01};
    ok &= expect(vepc::buildNasAuthenticationRequest() == expectedAuthRequest,
                 "authentication request bytes are stable");
    ok &= expect(vepc::parseNasAuthenticationRequest(expectedAuthRequest, authRequest, error),
                 "authentication request parser accepts stable bytes");
    ok &= expect(authRequest.hasKeySetIdentifier && authRequest.keySetIdentifier == 0x01,
                 "authentication request parser extracts key set identifier");

    const std::vector<uint8_t> expectedDownlinkTransport = {
        0x0D,
        0x01, 0x0F, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
        0x02, 0x08, 'g', 'u', 't', 'i', '-', '0', '0', '1',
        0x03, 0x02, 0x52, 0x01,
    };
    ok &= expect(vepc::buildDemoDownlinkNasTransport("123456789012345", "guti-001", expectedAuthRequest)
                     == expectedDownlinkTransport,
                 "downlink NAS transport bytes are stable");
    ok &= expect(vepc::parseDemoDownlinkNasTransport(expectedDownlinkTransport, downlinkTransport, error),
                 "downlink NAS transport parses");
    ok &= expect(downlinkTransport.procedureCode == 0x0D,
                 "downlink NAS transport parser extracts procedure code");
    ok &= expect(downlinkTransport.imsi == "123456789012345",
                 "downlink NAS transport parser extracts IMSI");
    ok &= expect(downlinkTransport.guti == "guti-001",
                 "downlink NAS transport parser extracts GUTI");
    ok &= expect(downlinkTransport.nasMessageType == 0x52,
                 "downlink NAS transport parser extracts NAS message type");

    const std::vector<uint8_t> authResponseBytes = {0x53, 0x01};
    ok &= expect(vepc::parseNasAuthenticationResponse(authResponseBytes, authResponse, error),
                 "authentication response parser accepts stable bytes");
    ok &= expect(authResponse.hasKeySetIdentifier && authResponse.keySetIdentifier == 0x01,
                 "authentication response parser extracts key set identifier");

    const std::vector<uint8_t> expectedSecurityModeCommand = {0x5D, 0x01, 0x01};
    ok &= expect(vepc::buildNasSecurityModeCommand() == expectedSecurityModeCommand,
                 "security mode command bytes are stable");
    ok &= expect(vepc::parseNasSecurityModeCommand(expectedSecurityModeCommand, securityModeCommand, error),
                 "security mode command parser accepts stable bytes");
    ok &= expect(securityModeCommand.hasKeySetIdentifier && securityModeCommand.keySetIdentifier == 0x01,
                 "security mode command parser extracts key set identifier");
    ok &= expect(securityModeCommand.hasSelectedAlgorithm && securityModeCommand.selectedAlgorithm == 0x01,
                 "security mode command parser extracts selected algorithm");

    const std::vector<uint8_t> securityModeCompleteBytes = {0x5E, 0x01};
    ok &= expect(vepc::parseNasSecurityModeComplete(securityModeCompleteBytes, securityModeComplete, error),
                 "security mode complete parser accepts stable bytes");
    ok &= expect(securityModeComplete.hasKeySetIdentifier && securityModeComplete.keySetIdentifier == 0x01,
                 "security mode complete parser extracts key set identifier");

    const std::vector<uint8_t> expectedAttachAccept = {0x42, 0x01, 0x01};
    ok &= expect(vepc::buildNasAttachAccept() == expectedAttachAccept,
                 "attach accept bytes are stable");
    ok &= expect(vepc::parseNasAttachAccept(expectedAttachAccept, attachAccept, error),
                 "attach accept parser accepts stable bytes");
    ok &= expect(attachAccept.hasKeySetIdentifier && attachAccept.keySetIdentifier == 0x01,
                 "attach accept parser extracts key set identifier");
    ok &= expect(attachAccept.hasAttachResult && attachAccept.attachResult == 0x01,
                 "attach accept parser extracts attach result");

    const std::vector<uint8_t> attachCompleteBytes = {0x43, 0x01};
    ok &= expect(vepc::parseNasAttachComplete(attachCompleteBytes, attachComplete, error),
                 "attach complete parser accepts stable bytes");
    ok &= expect(attachComplete.hasKeySetIdentifier && attachComplete.keySetIdentifier == 0x01,
                 "attach complete parser extracts key set identifier");

    const std::vector<uint8_t> serviceRequestBytes = {0x4C, 0x01, 0x01};
    ok &= expect(vepc::parseNasServiceRequest(serviceRequestBytes, serviceRequest, error),
                 "service request parser accepts stable bytes");
    ok &= expect(serviceRequest.hasKeySetIdentifier && serviceRequest.keySetIdentifier == 0x01,
                 "service request parser extracts key set identifier");
    ok &= expect(serviceRequest.hasServiceType && serviceRequest.serviceType == 0x01,
                 "service request parser extracts service type");

    const std::vector<uint8_t> expectedServiceAccept = {0x4D, 0x01, 0x05};
    ok &= expect(vepc::buildNasServiceAccept() == expectedServiceAccept,
                 "service accept bytes are stable");
    ok &= expect(vepc::parseNasServiceAccept(expectedServiceAccept, serviceAccept, error),
                 "service accept parser accepts stable bytes");
    ok &= expect(serviceAccept.hasKeySetIdentifier && serviceAccept.keySetIdentifier == 0x01,
                 "service accept parser extracts key set identifier");
    ok &= expect(serviceAccept.hasBearerId && serviceAccept.bearerId == 0x05,
                 "service accept parser extracts bearer id");

    const std::vector<uint8_t> serviceReleaseRequestBytes = {0x4E, 0x01, 0x00};
    ok &= expect(vepc::parseNasServiceReleaseRequest(serviceReleaseRequestBytes, serviceReleaseRequest, error),
                 "service release request parser accepts stable bytes");
    ok &= expect(serviceReleaseRequest.hasKeySetIdentifier && serviceReleaseRequest.keySetIdentifier == 0x01,
                 "service release request parser extracts key set identifier");
    ok &= expect(serviceReleaseRequest.hasReleaseCause && serviceReleaseRequest.releaseCause == 0x00,
                 "service release request parser extracts release cause");

    const std::vector<uint8_t> expectedServiceReleaseComplete = {0x4F, 0x01, 0x00};
    ok &= expect(vepc::buildNasServiceReleaseComplete() == expectedServiceReleaseComplete,
                 "service release complete bytes are stable");
    ok &= expect(vepc::parseNasServiceReleaseComplete(expectedServiceReleaseComplete, serviceReleaseComplete, error),
                 "service release complete parser accepts stable bytes");
    ok &= expect(serviceReleaseComplete.hasKeySetIdentifier && serviceReleaseComplete.keySetIdentifier == 0x01,
                 "service release complete parser extracts key set identifier");
    ok &= expect(serviceReleaseComplete.hasReleaseResult && serviceReleaseComplete.releaseResult == 0x00,
                 "service release complete parser extracts release result");

    const std::vector<uint8_t> detachRequestBytes = {0x45, 0x01, 0x01};
    ok &= expect(vepc::parseNasDetachRequest(detachRequestBytes, detachRequest, error),
                 "detach request parser accepts stable bytes");
    ok &= expect(detachRequest.hasKeySetIdentifier && detachRequest.keySetIdentifier == 0x01,
                 "detach request parser extracts key set identifier");
    ok &= expect(detachRequest.hasDetachType && detachRequest.detachType == 0x01,
                 "detach request parser extracts detach type");

    const std::vector<uint8_t> expectedDetachAccept = {0x46, 0x01, 0x00};
    ok &= expect(vepc::buildNasDetachAccept() == expectedDetachAccept,
                 "detach accept bytes are stable");
    ok &= expect(vepc::parseNasDetachAccept(expectedDetachAccept, detachAcceptResult, error),
                 "detach accept parser accepts stable bytes");
    ok &= expect(detachAcceptResult.hasKeySetIdentifier && detachAcceptResult.keySetIdentifier == 0x01,
                 "detach accept parser extracts key set identifier");
    ok &= expect(detachAcceptResult.hasDetachResult && detachAcceptResult.detachResult == 0x00,
                 "detach accept parser extracts detach result");

    const std::vector<uint8_t> trackingAreaUpdateRequestBytes = {0x48, 0x01, 0x11};
    ok &= expect(vepc::parseNasTrackingAreaUpdateRequest(trackingAreaUpdateRequestBytes, trackingAreaUpdateRequest, error),
                 "tau request parser accepts stable bytes");
    ok &= expect(trackingAreaUpdateRequest.hasKeySetIdentifier && trackingAreaUpdateRequest.keySetIdentifier == 0x01,
                 "tau request parser extracts key set identifier");
    ok &= expect(trackingAreaUpdateRequest.hasTrackingAreaCode && trackingAreaUpdateRequest.trackingAreaCode == 0x11,
                 "tau request parser extracts tracking area code");

    const std::vector<uint8_t> expectedTrackingAreaUpdateAccept = {0x49, 0x01, 0x11};
    ok &= expect(vepc::buildNasTrackingAreaUpdateAccept() == expectedTrackingAreaUpdateAccept,
                 "tau accept bytes are stable");
    ok &= expect(vepc::parseNasTrackingAreaUpdateAccept(expectedTrackingAreaUpdateAccept, trackingAreaUpdateAccept, error),
                 "tau accept parser accepts stable bytes");
    ok &= expect(trackingAreaUpdateAccept.hasKeySetIdentifier && trackingAreaUpdateAccept.keySetIdentifier == 0x01,
                 "tau accept parser extracts key set identifier");
    ok &= expect(trackingAreaUpdateAccept.hasTrackingAreaCode && trackingAreaUpdateAccept.trackingAreaCode == 0x11,
                 "tau accept parser extracts tracking area code");

    const std::vector<uint8_t> trackingAreaUpdateCompleteBytes = {0x4A, 0x01};
    ok &= expect(vepc::parseNasTrackingAreaUpdateComplete(trackingAreaUpdateCompleteBytes, trackingAreaUpdateComplete, error),
                 "tau complete parser accepts stable bytes");
    ok &= expect(trackingAreaUpdateComplete.hasKeySetIdentifier && trackingAreaUpdateComplete.keySetIdentifier == 0x01,
                 "tau complete parser extracts key set identifier");

    const std::vector<uint8_t> serviceResumeRequestBytes = {0x50, 0x01, 0x01};
    ok &= expect(vepc::parseNasServiceResumeRequest(serviceResumeRequestBytes, serviceResumeRequest, error),
                 "service resume request parser accepts stable bytes");
    ok &= expect(serviceResumeRequest.hasKeySetIdentifier && serviceResumeRequest.keySetIdentifier == 0x01,
                 "service resume request parser extracts key set identifier");
    ok &= expect(serviceResumeRequest.hasResumeType && serviceResumeRequest.resumeType == 0x01,
                 "service resume request parser extracts resume type");

    const std::vector<uint8_t> expectedServiceResumeAccept = {0x51, 0x01, 0x05};
    ok &= expect(vepc::buildNasServiceResumeAccept() == expectedServiceResumeAccept,
                 "service resume accept bytes are stable");
    ok &= expect(vepc::parseNasServiceResumeAccept(expectedServiceResumeAccept, serviceResumeAccept, error),
                 "service resume accept parser accepts stable bytes");
    ok &= expect(serviceResumeAccept.hasKeySetIdentifier && serviceResumeAccept.keySetIdentifier == 0x01,
                 "service resume accept parser extracts key set identifier");
    ok &= expect(serviceResumeAccept.hasBearerId && serviceResumeAccept.bearerId == 0x05,
                 "service resume accept parser extracts bearer id");

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

    const std::vector<uint8_t> shortAuthResponse = {0x53};
    ok &= expect(!vepc::parseNasAuthenticationResponse(shortAuthResponse, authResponse, error),
                 "short authentication response is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short authentication response error is descriptive");

    const std::vector<uint8_t> wrongNasType = {0x41, 0x01};
    ok &= expect(!vepc::parseNasAuthenticationRequest(wrongNasType, authRequest, error),
                 "wrong NAS message type is rejected for auth request parser");
    ok &= expect(error.find("unexpected NAS message type") != std::string::npos,
                 "wrong NAS message type error is descriptive");

    const std::vector<uint8_t> shortSecurityModeCommand = {0x5D, 0x01};
    ok &= expect(!vepc::parseNasSecurityModeCommand(shortSecurityModeCommand, securityModeCommand, error),
                 "short security mode command is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short security mode command error is descriptive");

    const std::vector<uint8_t> shortAttachComplete = {0x43};
    ok &= expect(!vepc::parseNasAttachComplete(shortAttachComplete, attachComplete, error),
                 "short attach complete is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short attach complete error is descriptive");

    const std::vector<uint8_t> wrongServiceAccept = {0x42, 0x01, 0x05};
    ok &= expect(!vepc::parseNasServiceAccept(wrongServiceAccept, serviceAccept, error),
                 "wrong NAS message type is rejected for service accept parser");
    ok &= expect(error.find("unexpected NAS message type") != std::string::npos,
                 "wrong service accept NAS type error is descriptive");

    const std::vector<uint8_t> shortServiceReleaseComplete = {0x4F, 0x01};
    ok &= expect(!vepc::parseNasServiceReleaseComplete(shortServiceReleaseComplete, serviceReleaseComplete, error),
                 "short service release complete is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short service release complete error is descriptive");

    const std::vector<uint8_t> wrongDetachAccept = {0x4F, 0x01, 0x00};
    ok &= expect(!vepc::parseNasDetachAccept(wrongDetachAccept, detachAcceptResult, error),
                 "wrong NAS message type is rejected for detach accept parser");
    ok &= expect(error.find("unexpected NAS message type") != std::string::npos,
                 "wrong detach accept NAS type error is descriptive");

    const std::vector<uint8_t> shortTrackingAreaUpdateAccept = {0x49, 0x01};
    ok &= expect(!vepc::parseNasTrackingAreaUpdateAccept(shortTrackingAreaUpdateAccept, trackingAreaUpdateAccept, error),
                 "short tau accept is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short tau accept error is descriptive");

    const std::vector<uint8_t> wrongTrackingAreaUpdateComplete = {0x49, 0x01};
    ok &= expect(!vepc::parseNasTrackingAreaUpdateComplete(wrongTrackingAreaUpdateComplete, trackingAreaUpdateComplete, error),
                 "wrong NAS message type is rejected for tau complete parser");
    ok &= expect(error.find("unexpected NAS message type") != std::string::npos,
                 "wrong tau complete NAS type error is descriptive");

    const std::vector<uint8_t> shortServiceResumeAccept = {0x51, 0x01};
    ok &= expect(!vepc::parseNasServiceResumeAccept(shortServiceResumeAccept, serviceResumeAccept, error),
                 "short service resume accept is rejected");
    ok &= expect(error.find("too short") != std::string::npos,
                 "short service resume accept error is descriptive");

    return ok ? 0 : 1;
}