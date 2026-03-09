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

bool tryQuery(const std::string& command, std::string& response, std::string& error) {
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
    address.sin_port = htons(5555);
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

    const std::string payload = command + "\n";
#ifdef _WIN32
    const int sent = send(socketHandle, payload.c_str(), static_cast<int>(payload.size()), 0);
#else
    const int sent = static_cast<int>(send(socketHandle, payload.c_str(), payload.size(), 0));
#endif
    if (sent <= 0) {
        error = "send() failed";
        closeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    response.clear();
    std::vector<char> buffer(8192);
    while (true) {
#ifdef _WIN32
        const int received = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int received = static_cast<int>(recv(socketHandle, buffer.data(), buffer.size(), 0));
#endif
        if (received == 0) {
            break;
        }
        if (received < 0) {
            error = "recv() failed";
            closeSocket(socketHandle);
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
    }

    closeSocket(socketHandle);
#ifdef _WIN32
    WSACleanup();
#endif
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: test-runtime-cli <command> [retries]\n";
        return 1;
    }

    const std::string command = argv[1];
    int retries = 20;
    if (argc >= 3) {
        retries = std::atoi(argv[2]);
        if (retries < 1) {
            retries = 1;
        }
    }

    std::string response;
    std::string error;
    for (int attempt = 0; attempt < retries; ++attempt) {
        if (tryQuery(command, response, error)) {
            std::cout << response;
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::cerr << error << "\n";
    return 1;
}
