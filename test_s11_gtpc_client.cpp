#include "src/gtp_parser.h"

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

bool tryRoundTrip(std::string& error) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#endif

    NativeSocket socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == kInvalidSocket) {
        error = "socket() failed";
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

#ifdef _WIN32
    DWORD timeoutMs = 1500;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 500000;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(2123);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        error = "inet_pton failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    const std::vector<uint8_t> request = {
        0x32, 0x10, 0x00, 0x25,
        0x00, 0x00, 0x00, 0x00,
        0x43, 0x21, 0x00, 0x00,
        0x02, 0x21, 0x43, 0x65, 0x87, 0x09, 0x21, 0x43, 0xF5,
        0x80, 0x00, 0x02, 0xF1, 0x21,
        0x83, 0x00, 0x09, 0x08, 'i', 'n', 't', 'e', 'r', 'n', 'e', 't',
        0x85, 0x00, 0x04, 0x0A, 0x17, 0x2A, 0x05,
    };

#ifdef _WIN32
    const int sent = sendto(socketHandle,
                            reinterpret_cast<const char*>(request.data()),
                            static_cast<int>(request.size()),
                            0,
                            reinterpret_cast<const sockaddr*>(&address),
                            sizeof(address));
#else
    const int sent = static_cast<int>(sendto(socketHandle,
                                             request.data(),
                                             request.size(),
                                             0,
                                             reinterpret_cast<const sockaddr*>(&address),
                                             sizeof(address)));
#endif
    if (sent != static_cast<int>(request.size())) {
        error = "sendto() failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    std::vector<uint8_t> response(4096);
    sockaddr_in peerAddr{};
#ifdef _WIN32
    int peerAddrSize = sizeof(peerAddr);
    const int received = recvfrom(socketHandle,
                                  reinterpret_cast<char*>(response.data()),
                                  static_cast<int>(response.size()),
                                  0,
                                  reinterpret_cast<sockaddr*>(&peerAddr),
                                  &peerAddrSize);
#else
    socklen_t peerAddrSize = sizeof(peerAddr);
    const int received = static_cast<int>(recvfrom(socketHandle,
                                                   response.data(),
                                                   response.size(),
                                                   0,
                                                   reinterpret_cast<sockaddr*>(&peerAddr),
                                                   &peerAddrSize));
#endif
    if (received <= 0) {
        error = "recvfrom() failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    response.resize(static_cast<std::size_t>(received));

    vepc::GtpV1Header header;
    if (!vepc::parseGtpV1Header(response, header, error)) {
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (header.messageType != 0x11) {
        error = "unexpected response message type";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    if (header.teid != 0x10004321u) {
        error = "unexpected response teid";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    if (header.sequence != 0x4321) {
        error = "unexpected response sequence";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    const std::vector<uint8_t> expected = {
        0x32, 0x11, 0x00, 0x06,
        0x10, 0x00, 0x43, 0x21,
        0x43, 0x21, 0x00, 0x00,
        0x01, 0x80,
    };
    if (response != expected) {
        error = "response bytes differ from expected Create PDP response";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    closeSocket(socketHandle);
#ifdef _WIN32
    WSACleanup();
#endif
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    int retries = 15;
    if (argc >= 2) {
        retries = std::atoi(argv[1]);
        if (retries < 1) {
            retries = 1;
        }
    }

    std::string error;
    for (int attempt = 0; attempt < retries; ++attempt) {
        if (tryRoundTrip(error)) {
            std::cout << "ok\n";
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::cerr << error << "\n";
    return 1;
}
