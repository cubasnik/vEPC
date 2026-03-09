#include "src/diameter_parser.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

void closeSocket(NativeSocket socketHandle) {
#ifdef _WIN32
    closesocket(socketHandle);
#else
    close(socketHandle);
#endif
}

bool sendAll(NativeSocket s, const std::vector<uint8_t>& data, std::string& error) {
#ifdef _WIN32
    const int sent = send(s, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#else
    const int sent = static_cast<int>(send(s, data.data(), data.size(), 0));
#endif
    if (sent != static_cast<int>(data.size())) {
        error = "send() failed";
        return false;
    }
    return true;
}

bool recvAll(NativeSocket s, std::vector<uint8_t>& response, std::string& error) {
    response.resize(4096);
#ifdef _WIN32
    const int received = recv(s, reinterpret_cast<char*>(response.data()), static_cast<int>(response.size()), 0);
#else
    const int received = static_cast<int>(recv(s, response.data(), response.size(), 0));
#endif
    if (received <= 0) {
        error = "recv() failed";
        return false;
    }
    response.resize(static_cast<std::size_t>(received));
    return true;
}

bool trySendMode(const std::string& mode, std::string& error) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#endif

    NativeSocket socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == kInvalidSocket) {
        error = "socket() failed";
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(3868);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        error = "inet_pton failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "connect() failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    const std::vector<uint8_t> cer = vepc::buildCapabilitiesExchangeRequest("mme.vepc.local",
                                                                             "epc.mnc001.mcc001.3gppnetwork.org");

    if (!sendAll(socketHandle, cer, error)) {
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    std::vector<uint8_t> response;
#ifdef _WIN32
    DWORD timeoutMs = 1500;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 500000;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    if (!recvAll(socketHandle, response, error)) {
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

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
    if (response != expectedCea) {
        error = "response bytes differ from expected CEA";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    vepc::DiameterHeader header;
    if (!vepc::parseDiameterHeader(response, header, error)) {
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    if (header.commandCode != 257 || header.request) {
        error = "unexpected Diameter response header";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    if (header.messageLength != expectedCea.size()) {
        error = "unexpected Diameter response length";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (mode == "watchdog") {
        const std::vector<uint8_t> dwr = vepc::buildWatchdogRequest("mme.vepc.local");
        if (!sendAll(socketHandle, dwr, error)) {
            closeSocket(socketHandle);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }

        std::vector<uint8_t> dwaResponse;
        if (!recvAll(socketHandle, dwaResponse, error)) {
            closeSocket(socketHandle);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }

        vepc::DiameterHeader dwaHeader;
        if (!vepc::parseDiameterHeader(dwaResponse, dwaHeader, error)) {
            closeSocket(socketHandle);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        if (dwaHeader.commandCode != 280 || dwaHeader.request) {
            error = "unexpected DWA response header";
            closeSocket(socketHandle);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
    }

    closeSocket(socketHandle);
#ifdef _WIN32
    WSACleanup();
#endif
    return true;
}

bool trySend(const std::string& mode, std::string& error) {
    return trySendMode(mode, error);
}

}  // namespace

int main(int argc, char* argv[]) {
    int retries = 15;
    std::string mode = "cer";
    if (argc >= 2) {
        retries = std::atoi(argv[1]);
        if (retries < 1) {
            retries = 1;
        }
    }
    if (argc >= 3) {
        mode = argv[2];
    }

    std::string error;
    for (int attempt = 0; attempt < retries; ++attempt) {
        if (trySend(mode, error)) {
            std::cout << "ok\n";
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::cerr << error << "\n";
    return 1;
}