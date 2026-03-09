#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include <csignal>
#include <clocale>
#include <filesystem>

#include "src/diameter_parser.h"
#include "src/gtp_parser.h"
#include "src/s1ap_parser.h"

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#endif

// Пути относительно рабочей директории (папки проекта)
#define LOG_FILE    "build/logs/vepc.log"
#define RUNTIME_STATE_FILE "build/state/runtime_state.json"
#define RUNTIME_STATE_SCHEMA_VERSION 2
#define RUNTIME_STATE_CORRUPT_RETENTION 5
#define CONFIG_FILE "config/vepc.config"
#define MME_CONFIG_FILE "config/vmme.conf"
#define SGSN_CONFIG_FILE "config/vsgsn.conf"
#define INTERFACE_ADMIN_STATE_FILE "config/interface_admin_state.conf"

#ifdef _WIN32
// Для Windows используем TCP CLI на localhost:5555
#define CLI_TCP_HOST "127.0.0.1"
#define CLI_TCP_PORT 5555
#define CLI_ENDPOINT "127.0.0.1:5555"
#else
// Для Linux оставляем старый UNIX-сокет
#define CLI_SOCKET   "/tmp/vepc.sock"
#define CLI_ENDPOINT CLI_SOCKET
#endif

// Цвета ANSI
#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_BLUE   "\033[34m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED    "\033[31m"
#define COLOR_WHITE  "\033[37m"
#define COLOR_BOLD   "\033[1m"

struct LogEntry {
    std::time_t time;
    std::string node;
    std::string msg;
};

struct PDPContext {
    uint32_t    teid = 0;
    uint16_t    sequence = 0;
    uint8_t     pdp_type = 0;
    bool        has_pdp_type = false;
    uint8_t     last_message_type = 0;
    std::string peer_ip;
    std::string ggsn_ip;
    std::string imsi;
    std::string apn;
    std::time_t updated_at = 0;
};

struct UEContext {
    std::string imsi;
    std::string guti;
    std::string peer_id;
    bool        authenticated = false;
    bool        auth_request_sent = false;
    bool        auth_response_received = false;
    bool        security_mode_command_sent = false;
    bool        security_mode_complete = false;
    bool        attach_accept_sent = false;
    bool        attach_complete_received = false;
    bool        attached = false;
    bool        service_request_received = false;
    bool        service_accept_sent = false;
    bool        service_active = false;
    bool        service_resume_request_received = false;
    bool        service_resume_accept_sent = false;
    bool        service_release_request_received = false;
    bool        service_release_complete_sent = false;
    bool        detach_request_received = false;
    bool        detach_accept_sent = false;
    bool        detached = false;
    bool        tracking_area_update_request_received = false;
    bool        tracking_area_update_accept_sent = false;
    bool        tracking_area_update_complete_received = false;
    uint8_t     last_nas_message_type = 0;
    bool        has_last_nas_message_type = false;
    uint8_t     last_s1ap_procedure = 0;
    uint8_t     security_context_id = 0;
    bool        has_security_context_id = false;
    uint8_t     selected_nas_algorithm = 0;
    bool        has_selected_nas_algorithm = false;
    uint8_t     default_bearer_id = 0;
    bool        has_default_bearer_id = false;
    uint8_t     tracking_area_code = 0;
    bool        has_tracking_area_code = false;
    std::time_t updated_at = 0;
};

struct InterfaceConfigEntry {
    std::string name;
    std::string proto;
    std::string address;
    std::string ip;
    std::string port;
    std::string peer;
};

struct InterfaceEndpointRuntime {
    bool        handlerImplemented = false;
    bool        listenerActive = false;
    uint64_t    receivedPackets = 0;
    uint64_t    receivedBytes = 0;
    std::string lastPeer;
    std::time_t lastActivityAt = 0;
    std::string lastMessage;
    std::string lastDetail;
    std::string reason;
};

struct InterfaceDiagnostics {
    bool        adminUp = true;
    bool        localBind = false;
    std::string operState = "UNKNOWN";
    std::string implementation = "UNKNOWN";
    std::string reason;
    std::string bindState = "N/A";
    std::string bindReason = "not evaluated";
    std::string listenState = "N/A";
    std::string listenReason = "not evaluated";
};

#ifdef _WIN32
using NativeSocket = SOCKET;
static constexpr NativeSocket INVALID_NATIVE_SOCKET = INVALID_SOCKET;
#else
using NativeSocket = int;
static constexpr NativeSocket INVALID_NATIVE_SOCKET = -1;
#endif

static std::string trimCopy(const std::string& value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static std::filesystem::path resolveProjectRelativePath(const std::string& filePath, bool requireExistingTarget) {
    const auto relativePath = std::filesystem::path(filePath);
    if (relativePath.is_absolute()) {
        if (!requireExistingTarget || std::filesystem::exists(relativePath)) {
            return relativePath;
        }
        return relativePath;
    }

    std::error_code error;
    auto searchRoot = std::filesystem::current_path(error);
    if (error) {
        searchRoot = std::filesystem::path(".");
    }

    for (auto current = searchRoot;; current = current.parent_path()) {
        const auto candidate = (current / relativePath).lexically_normal();
        const bool candidateMatches = requireExistingTarget
            ? std::filesystem::exists(candidate)
            : (!candidate.has_parent_path() || std::filesystem::exists(candidate.parent_path()));
        if (candidateMatches) {
            return candidate;
        }

        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
    }

    return relativePath;
}

static std::string resolveConfigPath(const std::string& filePath) {
    return resolveProjectRelativePath(filePath, true).string();
}

static std::string resolveWritableConfigPath(const std::string& filePath) {
    return resolveProjectRelativePath(filePath, false).string();
}

static void setConfigAlias(std::map<std::string, std::string>& config,
                           const std::string& sourceKey,
                           const std::string& targetKey) {
    const auto it = config.find(sourceKey);
    if (it != config.end() && !it->second.empty()) {
        config[targetKey] = it->second;
    }
}

static bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

#ifdef _WIN32
static void configureWindowsConsoleUtf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
}
#endif

static bool isSharedConfigKey(const std::string& key) {
    return key == "mcc" || key == "mnc";
}

static bool isMmeSpecificConfigKey(const std::string& key) {
    return isSharedConfigKey(key)
        || key == "mme-name"
        || key == "mme-code"
        || key == "mme-group-id"
        || startsWith(key, "s1ap-bind-")
        || startsWith(key, "s6a-")
        || startsWith(key, "s11-");
}

static bool isSgsnSpecificConfigKey(const std::string& key) {
    return isSharedConfigKey(key)
        || key == "sgsn-code"
        || startsWith(key, "gb-")
        || startsWith(key, "gn-")
        || startsWith(key, "iups-");
}

static bool isRuntimeConfigKey(const std::string& key) {
    return !isMmeSpecificConfigKey(key) && !isSgsnSpecificConfigKey(key);
}

static void writeConfigFile(const std::string& filename,
                            const std::map<std::string, std::string>& config,
                            const std::function<bool(const std::string&)>& predicate,
                            const std::string& header = "") {
    const std::string resolvedPath = resolveWritableConfigPath(filename);
    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to save config: " + resolvedPath);
    }

    if (!header.empty()) {
        file << header << "\n";
    }

    for (const auto& [key, value] : config) {
        if (predicate(key)) {
            file << key << " = " << value << "\n";
        }
    }
}

static bool tryParseInterfaceAdminState(const std::string& value, bool& adminUp) {
    std::string normalized = trimCopy(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "up" || normalized == "true" || normalized == "1" || normalized == "enabled") {
        adminUp = true;
        return true;
    }

    if (normalized == "down" || normalized == "false" || normalized == "0" || normalized == "disabled") {
        adminUp = false;
        return true;
    }

    return false;
}

static std::vector<InterfaceConfigEntry> loadInterfaceConfigEntries() {
    std::ifstream file(resolveConfigPath("config/interfaces.conf"));

    std::vector<InterfaceConfigEntry> entries;
    if (!file.is_open()) {
        return entries;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trimCopy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        InterfaceConfigEntry entry;
        std::istringstream ss(line);
        std::getline(ss, entry.name, '|');
        std::getline(ss, entry.proto, '|');
        std::getline(ss, entry.address, '|');
        std::getline(ss, entry.peer, '|');

        entry.name = trimCopy(entry.name);
        entry.proto = trimCopy(entry.proto);
        entry.address = trimCopy(entry.address);
        entry.peer = trimCopy(entry.peer);
        if (entry.name.empty()) {
            continue;
        }

        const size_t colon = entry.address.find(':');
        if (colon == std::string::npos) {
            entry.ip = entry.address;
        } else {
            entry.ip = trimCopy(entry.address.substr(0, colon));
            entry.port = trimCopy(entry.address.substr(colon + 1));
        }

        entries.push_back(entry);
    }

    return entries;
}

static bool isLocalInterfaceAddress(const std::string& ip, const std::map<std::string, std::string>& config) {
    auto localAddresses = std::set<std::string>{};
#ifdef _WIN32
    ULONG bufferSize = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &bufferSize)
        == ERROR_BUFFER_OVERFLOW) {
        std::vector<unsigned char> buffer(bufferSize);
        auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, addresses, &bufferSize)
            == NO_ERROR) {
            for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
                for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                    if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                        continue;
                    }
                    char addrBuffer[INET_ADDRSTRLEN]{};
                    const auto* addrIn = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                    if (inet_ntop(AF_INET, &addrIn->sin_addr, addrBuffer, sizeof(addrBuffer)) != nullptr) {
                        localAddresses.insert(addrBuffer);
                    }
                }
            }
        }
    }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
            if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            char addrBuffer[INET_ADDRSTRLEN]{};
            const auto* addrIn = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
            if (inet_ntop(AF_INET, &addrIn->sin_addr, addrBuffer, sizeof(addrBuffer)) != nullptr) {
                localAddresses.insert(addrBuffer);
            }
        }
        freeifaddrs(interfaces);
    }
#endif

    if (ip.empty() || ip == "0.0.0.0" || ip == "127.0.0.1" || ip == "::1") {
        return true;
    }

    if (localAddresses.find(ip) != localAddresses.end()) {
        return true;
    }

    const auto mmeIt = config.find("mme-ip");
    if (mmeIt != config.end() && ip == mmeIt->second) {
        return true;
    }

    const auto sgsnIt = config.find("sgsn-ip");
    if (sgsnIt != config.end() && ip == sgsnIt->second) {
        return true;
    }

    return false;
}

static std::string makeEndpointKey(const InterfaceConfigEntry& entry) {
    return entry.proto + "|" + entry.ip + "|" + entry.port;
}

static bool isConnectionOrientedProtocol(const InterfaceConfigEntry& entry) {
    return entry.proto == "TCP" || entry.proto == "DIAMETER";
}

static bool isGenericEndpointProtocol(const InterfaceConfigEntry& entry, const std::map<std::string, std::string>& config) {
    if (entry.proto == "TCP" || entry.proto == "DIAMETER" || entry.proto == "NS") {
        return true;
    }

    const auto gtpUserPortIt = config.find("gtp-u-port");
    if (entry.proto == "UDP" && gtpUserPortIt != config.end() && entry.port == gtpUserPortIt->second) {
        return true;
    }
    const auto s11PortIt = config.find("s11-port");
    if (entry.name == "S11" && entry.proto == "UDP" && s11PortIt != config.end() && entry.port == s11PortIt->second) {
        return true;
    }
    return false;
}

static std::string resolveInterfaceBindIp(const InterfaceConfigEntry& entry, const std::map<std::string, std::string>& config) {
    const auto gtpUserPortIt = config.find("gtp-u-port");
    if (gtpUserPortIt != config.end() && entry.proto == "UDP" && entry.port == gtpUserPortIt->second) {
        const auto bindIpIt = config.find("gn-gtp-u-bind-ip");
        if (bindIpIt != config.end() && !bindIpIt->second.empty()) {
            return bindIpIt->second;
        }
        const auto sgsnIpIt = config.find("sgsn-ip");
        if (sgsnIpIt != config.end() && !sgsnIpIt->second.empty()) {
            return sgsnIpIt->second;
        }
    }
    const auto s11PortIt = config.find("s11-port");
    if (entry.name == "S11" && s11PortIt != config.end() && entry.proto == "UDP" && entry.port == s11PortIt->second) {
        const auto bindIpIt = config.find("s11-bind-ip");
        if (bindIpIt != config.end() && !bindIpIt->second.empty()) {
            return bindIpIt->second;
        }
        const auto mmeIpIt = config.find("mme-ip");
        if (mmeIpIt != config.end() && !mmeIpIt->second.empty()) {
            return mmeIpIt->second;
        }
    }
    return entry.ip;
}

static std::string getSocketErrorText() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

static void closeNativeSocket(NativeSocket socketHandle) {
    if (socketHandle == INVALID_NATIVE_SOCKET) {
        return;
    }
#ifdef _WIN32
    closesocket(socketHandle);
#else
    close(socketHandle);
#endif
}

static bool tryGetLocalTime(std::time_t value, std::tm& result) {
#ifdef _WIN32
    return localtime_s(&result, &value) == 0;
#else
    return localtime_r(&value, &result) != nullptr;
#endif
}

static std::string formatTimestamp(std::time_t value) {
    if (value == 0) {
        return "n/a";
    }

    char buffer[64]{};
    std::tm localTime{};
    if (!tryGetLocalTime(value, localTime)) {
        return "n/a";
    }

    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

static std::string formatTimestampForFilename(std::time_t value) {
    if (value == 0) {
        value = std::time(nullptr);
    }

    char buffer[64]{};
    std::tm localTime{};
    if (!tryGetLocalTime(value, localTime)) {
        return "unknown-time";
    }

    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &localTime);
    return buffer;
}

static bool parseTimestamp(const std::string& value, std::time_t& result) {
    result = 0;
    if (value.empty() || value == "n/a") {
        return true;
    }

    std::tm localTime{};
    std::istringstream iss(value);
    iss >> std::get_time(&localTime, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return false;
    }

    result = std::mktime(&localTime);
    return result != static_cast<std::time_t>(-1);
}

static std::string escapeJsonString(const std::string& value) {
    std::ostringstream oss;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (ch < 0x20) {
                oss << "\\u"
                    << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(ch)
                    << std::nouppercase << std::dec << std::setfill(' ');
            } else {
                oss << static_cast<char>(ch);
            }
            break;
        }
    }
    return oss.str();
}

static std::string unescapeJsonString(const std::string& value) {
    std::ostringstream oss;
    for (size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch != '\\' || index + 1 >= value.size()) {
            oss << ch;
            continue;
        }

        const char escaped = value[++index];
        switch (escaped) {
        case '\\':
            oss << '\\';
            break;
        case '"':
            oss << '"';
            break;
        case 'b':
            oss << '\b';
            break;
        case 'f':
            oss << '\f';
            break;
        case 'n':
            oss << '\n';
            break;
        case 'r':
            oss << '\r';
            break;
        case 't':
            oss << '\t';
            break;
        default:
            oss << escaped;
            break;
        }
    }
    return oss.str();
}

static bool findJsonArrayBounds(const std::string& json,
                                const std::string& key,
                                size_t& arrayStart,
                                size_t& arrayEnd) {
    const std::string token = "\"" + key + "\"";
    const size_t keyPos = json.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t bracketPos = json.find('[', keyPos + token.size());
    if (bracketPos == std::string::npos) {
        return false;
    }

    bool inString = false;
    bool escaping = false;
    int depth = 0;
    for (size_t pos = bracketPos; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (inString) {
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '[') {
            ++depth;
            continue;
        }
        if (ch == ']') {
            --depth;
            if (depth == 0) {
                arrayStart = bracketPos + 1;
                arrayEnd = pos;
                return true;
            }
        }
    }

    return false;
}

static std::vector<std::string> splitTopLevelJsonObjects(const std::string& body) {
    std::vector<std::string> objects;
    bool inString = false;
    bool escaping = false;
    int depth = 0;
    size_t objectStart = std::string::npos;

    for (size_t pos = 0; pos < body.size(); ++pos) {
        const char ch = body[pos];
        if (inString) {
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                objectStart = pos;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(body.substr(objectStart, pos - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }

    return objects;
}

static bool findJsonFieldValue(const std::string& object,
                               const std::string& key,
                               size_t& valueStart,
                               size_t& valueEnd) {
    const std::string token = "\"" + key + "\":";
    const size_t keyPos = object.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }

    valueStart = keyPos + token.size();
    while (valueStart < object.size() && std::isspace(static_cast<unsigned char>(object[valueStart]))) {
        ++valueStart;
    }
    if (valueStart >= object.size()) {
        return false;
    }

    if (object[valueStart] == '"') {
        ++valueStart;
        valueEnd = valueStart;
        bool escaping = false;
        while (valueEnd < object.size()) {
            const char ch = object[valueEnd];
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                return true;
            }
            ++valueEnd;
        }
        return false;
    }

    valueEnd = valueStart;
    while (valueEnd < object.size() && object[valueEnd] != ',' && object[valueEnd] != '\n' && object[valueEnd] != '\r' && object[valueEnd] != '}') {
        ++valueEnd;
    }
    return valueEnd > valueStart;
}

static bool extractJsonStringField(const std::string& object,
                                   const std::string& key,
                                   std::string& value) {
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!findJsonFieldValue(object, key, valueStart, valueEnd)) {
        return false;
    }
    value = unescapeJsonString(object.substr(valueStart, valueEnd - valueStart));
    return true;
}

static bool extractJsonBoolField(const std::string& object,
                                 const std::string& key,
                                 bool& value) {
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!findJsonFieldValue(object, key, valueStart, valueEnd)) {
        return false;
    }
    const std::string token = trimCopy(object.substr(valueStart, valueEnd - valueStart));
    if (token == "true") {
        value = true;
        return true;
    }
    if (token == "false") {
        value = false;
        return true;
    }
    return false;
}

static bool extractJsonUintField(const std::string& object,
                                 const std::string& key,
                                 uint64_t& value) {
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!findJsonFieldValue(object, key, valueStart, valueEnd)) {
        return false;
    }
    const std::string token = trimCopy(object.substr(valueStart, valueEnd - valueStart));
    if (token.empty()) {
        return false;
    }
    try {
        value = std::stoull(token);
        return true;
    } catch (...) {
        return false;
    }
}

static bool validateLoadedPdpContext(const PDPContext& context, std::string& error) {
    error.clear();
    if (context.teid == 0) {
        error = "missing non-zero teid";
        return false;
    }
    if (context.last_message_type == 0) {
        error = "missing last_message_type";
        return false;
    }
    if (context.peer_ip.empty()) {
        error = "missing peer_ip";
        return false;
    }
    if (context.imsi.empty()) {
        error = "missing imsi";
        return false;
    }
    if (context.apn.empty()) {
        error = "missing apn";
        return false;
    }
    if (context.has_pdp_type && context.pdp_type == 0) {
        error = "has_pdp_type is true but pdp_type is zero";
        return false;
    }
    if (context.updated_at == 0) {
        error = "missing updated_at";
        return false;
    }
    return true;
}

static bool validateLoadedUeContext(const UEContext& context, std::string& error) {
    error.clear();
    if (context.imsi.empty()) {
        error = "missing imsi";
        return false;
    }
    if (context.updated_at == 0) {
        error = "missing updated_at";
        return false;
    }
    if (context.has_last_nas_message_type && context.last_nas_message_type == 0) {
        error = "has_last_nas_message_type is true but last_nas_message_type is zero";
        return false;
    }
    if (context.has_selected_nas_algorithm && context.selected_nas_algorithm == 0) {
        error = "has_selected_nas_algorithm is true but selected_nas_algorithm is zero";
        return false;
    }
    if (context.has_default_bearer_id && context.default_bearer_id == 0) {
        error = "has_default_bearer_id is true but default_bearer_id is zero";
        return false;
    }
    if (context.has_tracking_area_code && context.tracking_area_code == 0) {
        error = "has_tracking_area_code is true but tracking_area_code is zero";
        return false;
    }
    if (context.auth_response_received && !context.auth_request_sent) {
        error = "auth_response_received requires auth_request_sent";
        return false;
    }
    if (context.security_mode_complete && !context.security_mode_command_sent) {
        error = "security_mode_complete requires security_mode_command_sent";
        return false;
    }
    if (context.attach_complete_received && !context.attach_accept_sent) {
        error = "attach_complete_received requires attach_accept_sent";
        return false;
    }
    if (context.attached && !context.attach_complete_received) {
        error = "attached requires attach_complete_received";
        return false;
    }
    if (context.service_active && (!context.attached || !context.has_default_bearer_id)) {
        error = "service_active requires attached state and default bearer id";
        return false;
    }
    if (context.service_resume_accept_sent && !context.service_active) {
        error = "service_resume_accept_sent requires service_active";
        return false;
    }
    if (context.tracking_area_update_complete_received && (!context.attached || !context.has_tracking_area_code)) {
        error = "TAU complete requires attached state and tracking area code";
        return false;
    }
    if (context.detached) {
        if (context.attached || context.authenticated || context.service_active) {
            error = "detached context cannot remain attached/authenticated/service-active";
            return false;
        }
        if (context.has_security_context_id || context.has_default_bearer_id || context.has_tracking_area_code) {
            error = "detached context cannot retain security, bearer, or tracking area data";
            return false;
        }
    }

    const bool requiresSecurityContext = context.auth_request_sent
        || context.auth_response_received
        || context.security_mode_command_sent
        || context.security_mode_complete
        || context.attach_accept_sent
        || context.attach_complete_received
        || context.attached
        || context.service_request_received
        || context.service_accept_sent
        || context.service_active
        || context.service_resume_request_received
        || context.service_resume_accept_sent
        || context.service_release_request_received
        || context.service_release_complete_sent
        || context.tracking_area_update_request_received
        || context.tracking_area_update_accept_sent
        || context.tracking_area_update_complete_received;
    if (requiresSecurityContext && !context.detached && !context.has_security_context_id) {
        error = "active UE flow requires security_context_id";
        return false;
    }

    return true;
}

static std::string quarantineRuntimeStateFile(const std::string& inputPath) {
    const std::filesystem::path sourcePath(inputPath);
    if (!std::filesystem::exists(sourcePath)) {
        return "";
    }

    const std::string suffix = ".corrupt-" + formatTimestampForFilename(std::time(nullptr));
    const std::filesystem::path destinationPath = sourcePath.parent_path() /
        (sourcePath.stem().string() + suffix + sourcePath.extension().string());

    std::error_code error;
    std::filesystem::rename(sourcePath, destinationPath, error);
    if (!error) {
        return destinationPath.string();
    }

    std::filesystem::copy_file(sourcePath,
                               destinationPath,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) {
        throw std::runtime_error("Failed to quarantine runtime state file: " + inputPath);
    }

    std::filesystem::remove(sourcePath, error);
    if (error) {
        throw std::runtime_error("Failed to remove original corrupt runtime state file: " + inputPath);
    }

    return destinationPath.string();
}

static std::size_t pruneCorruptRuntimeStateFiles(const std::filesystem::path& stateDirectory,
                                                 std::size_t maxFiles,
                                                 const std::filesystem::path& preservePath = {}) {
    if (maxFiles == 0 || stateDirectory.empty()) {
        return 0;
    }

    std::error_code error;
    if (!std::filesystem::exists(stateDirectory, error) || error) {
        return 0;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(stateDirectory, error)) {
        if (error) {
            return 0;
        }

        if (!entry.is_regular_file()) {
            continue;
        }

        const auto fileName = entry.path().filename().string();
        if (startsWith(fileName, "runtime_state.corrupt-") && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    if (files.size() <= maxFiles) {
        return 0;
    }

    std::sort(files.begin(), files.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return left.filename().string() < right.filename().string();
    });

    const std::string preserved = preservePath.empty() ? "" : preservePath.lexically_normal().string();
    std::size_t removedCount = 0;
    for (const auto& file : files) {
        if (files.size() - removedCount <= maxFiles) {
            break;
        }
        if (!preserved.empty() && file.lexically_normal().string() == preserved) {
            continue;
        }

        std::filesystem::remove(file, error);
        if (!error) {
            ++removedCount;
        }
        error.clear();
    }

    return removedCount;
}

static void appendJsonStringField(std::ostringstream& oss,
                                  const std::string& name,
                                  const std::string& value,
                                  bool trailingComma = true) {
    oss << "      \"" << name << "\": \"" << escapeJsonString(value) << "\"";
    if (trailingComma) {
        oss << ",";
    }
    oss << "\n";
}

static void appendJsonBoolField(std::ostringstream& oss,
                                const std::string& name,
                                bool value,
                                bool trailingComma = true) {
    oss << "      \"" << name << "\": " << (value ? "true" : "false");
    if (trailingComma) {
        oss << ",";
    }
    oss << "\n";
}

static void appendJsonIntField(std::ostringstream& oss,
                               const std::string& name,
                               uint64_t value,
                               bool trailingComma = true) {
    oss << "      \"" << name << "\": " << value;
    if (trailingComma) {
        oss << ",";
    }
    oss << "\n";
}

static std::string formatPdpTypeValue(const PDPContext& context) {
    if (!context.has_pdp_type) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(context.pdp_type);
    return oss.str();
}

static std::string formatNasMessageTypeValue(const UEContext& context) {
    if (!context.has_last_nas_message_type) {
        return "n/a";
    }

    return vepc::formatNasMessageType(context.last_nas_message_type);
}

static std::string formatS1apProcedureValue(const UEContext& context) {
    if (context.last_s1ap_procedure == 0) {
        return "n/a";
    }

    return vepc::formatS1apProcedureCode(context.last_s1ap_procedure);
}

static std::string formatSecurityContextIdValue(const UEContext& context) {
    if (!context.has_security_context_id) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(context.security_context_id);
    return oss.str();
}

static std::string formatAuthFlowState(const UEContext& context) {
    if (context.detached || context.detach_accept_sent) {
        return "detached";
    }
    if (context.service_resume_accept_sent) {
        return "service-resume-accept-sent";
    }
    if (context.tracking_area_update_complete_received) {
        return "tau-updated";
    }
    if (context.tracking_area_update_accept_sent) {
        return "tau-accept-sent";
    }
    if (context.service_active) {
        return "service-active";
    }
    if (context.service_release_complete_sent) {
        return "attached-idle";
    }
    if (context.service_accept_sent) {
        return "service-accept-sent";
    }
    if (context.attached || context.attach_complete_received) {
        return "attach-complete";
    }
    if (context.attach_accept_sent) {
        return "attach-accept-sent";
    }
    if (context.security_mode_complete) {
        return "security-mode-complete";
    }
    if (context.security_mode_command_sent) {
        return "security-mode-command-sent";
    }
    if (context.authenticated) {
        return "authentication-complete";
    }
    if (context.auth_response_received) {
        return "authentication-response-received";
    }
    if (context.auth_request_sent) {
        return "authentication-request-sent";
    }
    if (context.has_last_nas_message_type && context.last_nas_message_type == 0x41) {
        return "attach-request-received";
    }
    return "idle";
}

static std::string formatBearerIdValue(const UEContext& context) {
    if (!context.has_default_bearer_id) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(context.default_bearer_id);
    return oss.str();
}

static std::string formatNasAlgorithmValue(const UEContext& context) {
    if (!context.has_selected_nas_algorithm) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(context.selected_nas_algorithm);
    return oss.str();
}

static std::string formatTrackingAreaCodeValue(const UEContext& context) {
    if (!context.has_tracking_area_code) {
        return "n/a";
    }

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(context.tracking_area_code);
    return oss.str();
}

static uint32_t allocateCreatePdpTeid(const vepc::GtpV1Header& header) {
    if (header.teid != 0) {
        return header.teid;
    }
    return 0x10000000u | static_cast<uint32_t>(header.sequence);
}

static std::string socketAddressToString(const sockaddr_in& address) {
    char buffer[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr) {
        return "unknown";
    }
    return buffer;
}

static std::string socketEndpointToString(const sockaddr_in& address) {
    std::ostringstream oss;
    oss << socketAddressToString(address) << ':' << ntohs(address.sin_port);
    return oss.str();
}

// Глобальный указатель для обработки сигналов
static std::atomic<bool> g_running{true};

class VNodeController {
public:
    VNodeController() : running(true) {
        loadConfigFromFile();
    }

    ~VNodeController() { stop(); }

    void start();
    void stop();
    void restart();
    bool consumeRestartRequest();

    void        log(const std::string& node, const std::string& msg);
    std::string getStatus() const;
    void        printLogs()   const;
    void        printState()  const;
    void        printConfig() const;
    bool        loadConfig(const std::string& filename);

    void syncConfigAliasesForKey(const std::string& key, const std::string& value) {
        if (key == "s1ap-port") {
            config["s1ap-bind-port"] = value;
        } else if (key == "s1ap-bind-port") {
            config["s1ap-port"] = value;
        } else if (key == "gtp-c-port") {
            config["gn-gtp-c-port"] = value;
        } else if (key == "gn-gtp-c-port") {
            config["gtp-c-port"] = value;
        } else if (key == "gtp-u-port") {
            config["gn-gtp-u-port"] = value;
        } else if (key == "gn-gtp-u-port") {
            config["gtp-u-port"] = value;
        } else if (key == "s11-port") {
            config["s11-bind-port"] = value;
        } else if (key == "s11-bind-port") {
            config["s11-port"] = value;
        } else if (key == "s11-ip") {
            config["s11-bind-ip"] = value;
        } else if (key == "s11-bind-ip") {
            config["s11-ip"] = value;
        }
    }

    void setValue(const std::string& key, const std::string& value) {
        config[key] = value;
        syncConfigAliasesForKey(key, value);
        applyRuntimeConfigAliases();
        log("MAIN", "Установлено " + key + " = " + value);
        saveConfigToFile();
    }

    void cliServerThread();
    std::string handleCliCommand(const std::string& cmd, bool& shouldStop);

private:
    void mmeThreadFunc();
    void sgsnThreadFunc();
    void gtpServerThreadFunc();
    void s1apServerThreadFunc();
    bool handleDemoS1apMessage(NativeSocket clientSocket,
                               const std::string& peerId,
                               const std::vector<uint8_t>& packet);
    bool handleRealGtpMessage(NativeSocket socketHandle,
                              const sockaddr_in& peerAddr,
                              const std::string& peerIp,
                              const std::vector<uint8_t>& packet,
                              const vepc::GtpV1Header& header);
    bool sendGtpResponse(NativeSocket socketHandle,
                         const sockaddr_in& peerAddr,
                         const std::vector<uint8_t>& response,
                         const std::string& peerIp,
                         const std::string& responseLabel,
                         uint32_t teid,
                         uint16_t sequence);
    void startConfiguredInterfaceEndpoints();
    void interfaceEndpointThread(InterfaceConfigEntry entry);
    void closeCliListenSocket();
    InterfaceDiagnostics buildInterfaceDiagnostics(const InterfaceConfigEntry& entry) const;
    std::string formatInterfaceStatus(const std::string& name) const;
    std::string formatInterfaceOverview() const;
    std::string formatStateSnapshotLocked() const;
    std::string applyInterfaceAction(const std::string& action, const std::string& name);
    InterfaceEndpointRuntime getInterfaceEndpointState(const InterfaceConfigEntry& entry) const;
    void setInterfaceEndpointState(const std::string& endpointKey, InterfaceEndpointRuntime runtimeState);
    void upsertPdpContext(const PDPContext& context);
    bool tryGetPdpContext(uint32_t teid, PDPContext& context) const;
    bool removePdpContext(uint32_t teid, PDPContext* removedContext = nullptr);
    void upsertUeContext(const UEContext& context);
    bool tryGetUeContext(const std::string& imsi, UEContext& context) const;
    void clearRuntimeState();
    void saveRuntimeStateToFile();
    void loadRuntimeStateFromFile();

    std::atomic<bool> running;
    std::atomic<bool> restartRequested{false};
    std::thread mmeThread;
    std::thread sgsnThread;
    std::thread gtpThread;
    std::thread s1apThread;
    std::thread cliThread;
    std::vector<std::thread> endpointThreads;

    mutable std::mutex          logMutex;
    std::vector<LogEntry>       logs;
    std::ofstream               logFile;

    std::string        mmeStatus{"Stopped"};
    std::string        sgsnStatus{"Stopped"};
    mutable std::mutex statusMutex;

    std::map<std::string, std::string> config;
    std::map<uint32_t, PDPContext>     pdpContexts;
    std::map<std::string, UEContext>   ueContexts;
    mutable std::mutex stateMutex;
    mutable std::mutex ifaceMutex;
    std::map<std::string, bool> interfaceAdminState;
    mutable std::mutex endpointMutex;
    std::map<std::string, InterfaceEndpointRuntime> endpointStates;
    mutable std::mutex cliSocketMutex;
    NativeSocket cliListenSocket{INVALID_NATIVE_SOCKET};

    void colorLog(const std::string& node, const std::string& msg);
    void saveConfigToFile() const;
    void saveInterfaceAdminStateToFile() const;
    void resetConfigToDefaults();
    void applyRuntimeConfigAliases();
    void loadInterfaceAdminStateFromFile();
    void loadConfigFromFile();
};

// ----------------------------------------------------------------
//  Логирование
// ----------------------------------------------------------------

void VNodeController::colorLog(const std::string& node, const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    logs.push_back({std::time(nullptr), node, msg});

    std::string color = COLOR_WHITE;
    std::string style;
    if      (node == "MME")  { color = COLOR_GREEN;  style = COLOR_BOLD; }
    else if (node == "SGSN") { color = COLOR_BLUE; }
    else if (node == "GTP")  { color = COLOR_YELLOW; }
    else if (node == "S1AP") { color = COLOR_RED; }
    else if (node == "MAIN") { color = COLOR_WHITE;  style = COLOR_BOLD; }

    std::cout << style << color << "[" << node << "] " << msg << COLOR_RESET << "\n";

    if (logFile.is_open()) {
        char buf[64];
        std::tm localTime{};
        if (tryGetLocalTime(logs.back().time, localTime)) {
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
            logFile << "[" << buf << "] [" << node << "] " << msg << "\n";
        } else {
            logFile << "[timestamp-unavailable] [" << node << "] " << msg << "\n";
        }
        logFile.flush();
    }
}

void VNodeController::log(const std::string& node, const std::string& msg) {
    colorLog(node, msg);
}

// ----------------------------------------------------------------
//  Конфиг
// ----------------------------------------------------------------

bool VNodeController::loadConfig(const std::string& filename) {
    const std::string resolvedPath = resolveConfigPath(filename);
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        log("MAIN", "Failed to open config: " + filename);
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trimCopy(line);
        if (line.empty() || line[0] == '#') continue;
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = trimCopy(line.substr(0, eqPos));
        std::string value = trimCopy(line.substr(eqPos + 1));
        if (key.empty()) {
            continue;
        }
        config[key] = value;
    }
    log("MAIN", "Конфиг загружен: " + resolvedPath);
    return true;
}

void VNodeController::saveConfigToFile() const {
    try {
        writeConfigFile(CONFIG_FILE, config, [](const std::string& key) {
            return isRuntimeConfigKey(key) || isSharedConfigKey(key);
        });
        writeConfigFile(MME_CONFIG_FILE, config, [](const std::string& key) {
            return isMmeSpecificConfigKey(key);
        }, "# vMME configuration");
        writeConfigFile(SGSN_CONFIG_FILE, config, [](const std::string& key) {
            return isSgsnSpecificConfigKey(key);
        }, "# vSGSN configuration");
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
    }
}

void VNodeController::saveInterfaceAdminStateToFile() const {
    std::map<std::string, bool> stateCopy;
    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        stateCopy = interfaceAdminState;
    }

    const std::string resolvedPath = resolveWritableConfigPath(INTERFACE_ADMIN_STATE_FILE);
    std::error_code error;

    if (stateCopy.empty()) {
        std::filesystem::remove(resolvedPath, error);
        return;
    }

    const auto outputPath = std::filesystem::path(resolvedPath);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), error);
    }

    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to save interface admin-state: " + resolvedPath);
    }

    file << "# Persisted administrative interface state\n";
    for (const auto& [name, adminUp] : stateCopy) {
        file << name << " = " << (adminUp ? "up" : "down") << "\n";
    }
}

void VNodeController::resetConfigToDefaults() {
    config.clear();
    config["mcc"] = "250";
    config["mnc"] = "20";
    config["gtp-c-port"] = "2123";
    config["gtp-u-port"] = "2152";
    config["s1ap-port"] = "36412";
    config["sgsn-ip"] = "127.0.0.1";
    config["mme-ip"] = "127.0.0.1";
}

void VNodeController::applyRuntimeConfigAliases() {
    setConfigAlias(config, "s1ap-bind-port", "s1ap-port");
    setConfigAlias(config, "s1ap-bind-ip", "mme-ip");
    setConfigAlias(config, "gn-gtp-c-port", "gtp-c-port");
    setConfigAlias(config, "gn-gtp-u-port", "gtp-u-port");
    setConfigAlias(config, "s11-bind-port", "s11-port");
    setConfigAlias(config, "s11-bind-ip", "mme-ip");

    if (config.find("gn-gtp-c-bind-ip") != config.end() && !config["gn-gtp-c-bind-ip"].empty()) {
        config["sgsn-ip"] = config["gn-gtp-c-bind-ip"];
    } else {
        setConfigAlias(config, "gb-bind-ip", "sgsn-ip");
    }
}

void VNodeController::loadInterfaceAdminStateFromFile() {
    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        interfaceAdminState.clear();
    }

    const std::string resolvedPath = resolveConfigPath(INTERFACE_ADMIN_STATE_FILE);
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        return;
    }

    std::set<std::string> validInterfaces;
    for (const auto& entry : loadInterfaceConfigEntries()) {
        validInterfaces.insert(entry.name);
    }

    std::map<std::string, bool> loadedState;
    std::string line;
    while (std::getline(file, line)) {
        line = trimCopy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }

        const std::string name = trimCopy(line.substr(0, eqPos));
        const std::string value = trimCopy(line.substr(eqPos + 1));
        if (name.empty()) {
            continue;
        }

        if (!validInterfaces.empty() && validInterfaces.find(name) == validInterfaces.end()) {
            log("MAIN", "Ignoring admin-state for unknown interface: " + name);
            continue;
        }

        bool adminUp = true;
        if (!tryParseInterfaceAdminState(value, adminUp)) {
            log("MAIN", "Ignoring invalid admin-state value for interface " + name + ": " + value);
            continue;
        }

        loadedState[name] = adminUp;
    }

    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        interfaceAdminState = std::move(loadedState);
    }
}

void VNodeController::loadConfigFromFile() {
    resetConfigToDefaults();
    loadConfig(CONFIG_FILE);
    loadConfig(MME_CONFIG_FILE);
    loadConfig(SGSN_CONFIG_FILE);
    applyRuntimeConfigAliases();
    loadInterfaceAdminStateFromFile();
}

void VNodeController::closeCliListenSocket() {
    std::lock_guard<std::mutex> lock(cliSocketMutex);
    if (cliListenSocket != INVALID_NATIVE_SOCKET) {
        closeNativeSocket(cliListenSocket);
        cliListenSocket = INVALID_NATIVE_SOCKET;
    }
}

bool VNodeController::consumeRestartRequest() {
    return restartRequested.exchange(false);
}

void VNodeController::setInterfaceEndpointState(const std::string& endpointKey, InterfaceEndpointRuntime runtimeState) {
    std::lock_guard<std::mutex> lock(endpointMutex);
    endpointStates[endpointKey] = std::move(runtimeState);
}

InterfaceEndpointRuntime VNodeController::getInterfaceEndpointState(const InterfaceConfigEntry& entry) const {
    std::lock_guard<std::mutex> lock(endpointMutex);
    const auto it = endpointStates.find(makeEndpointKey(entry));
    if (it != endpointStates.end()) {
        return it->second;
    }
    return {};
}

void VNodeController::interfaceEndpointThread(InterfaceConfigEntry entry) {
    const std::string endpointKey = makeEndpointKey(entry);
    const std::string bindIp = resolveInterfaceBindIp(entry, config);
    InterfaceEndpointRuntime runtimeState;
    runtimeState.handlerImplemented = true;

    if (!isLocalInterfaceAddress(bindIp, config)) {
        runtimeState.reason = "configured on non-local address";
        setInterfaceEndpointState(endpointKey, runtimeState);
        return;
    }

    int port = 0;
    try {
        port = entry.port.empty() ? 0 : std::stoi(entry.port);
    } catch (...) {
        port = 0;
    }
    if (port <= 0) {
        runtimeState.reason = "invalid port in interfaces.conf";
        setInterfaceEndpointState(endpointKey, runtimeState);
        return;
    }

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        runtimeState.reason = "WSAStartup failed";
        setInterfaceEndpointState(endpointKey, runtimeState);
        return;
    }
#endif

    const int socketType = isConnectionOrientedProtocol(entry) ? SOCK_STREAM : SOCK_DGRAM;
    NativeSocket socketHandle = socket(AF_INET, socketType, 0);
    if (socketHandle == INVALID_NATIVE_SOCKET) {
        runtimeState.reason = "socket() failed: " + getSocketErrorText();
        setInterfaceEndpointState(endpointKey, runtimeState);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int reuseAddr = 1;
#ifdef _WIN32
    const char* reuseValue = reinterpret_cast<const char*>(&reuseAddr);
#else
    const void* reuseValue = &reuseAddr;
#endif
    setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, reuseValue, sizeof(reuseAddr));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bindIp.c_str(), &addr.sin_addr) != 1) {
        runtimeState.reason = "inet_pton failed for address " + bindIp;
        setInterfaceEndpointState(endpointKey, runtimeState);
        closeNativeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        runtimeState.reason = "bind() failed: " + getSocketErrorText();
        setInterfaceEndpointState(endpointKey, runtimeState);
        closeNativeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (isConnectionOrientedProtocol(entry) && listen(socketHandle, SOMAXCONN) != 0) {
        runtimeState.reason = "listen() failed: " + getSocketErrorText();
        setInterfaceEndpointState(endpointKey, runtimeState);
        closeNativeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    runtimeState.listenerActive = true;
    runtimeState.reason = isConnectionOrientedProtocol(entry)
        ? "listener bound and accepting connections"
        : "socket bound and ready";
    setInterfaceEndpointState(endpointKey, runtimeState);
    log("MAIN", "Interface endpoint " + entry.name + " ready on " + bindIp + ":" + entry.port + " (" + entry.proto + ")");

    if (!isConnectionOrientedProtocol(entry)) {
        std::vector<char> buffer(2048);
        while (running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socketHandle, &readSet);

            timeval timeout{};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            const int selectResult = select(static_cast<int>(socketHandle) + 1, &readSet, nullptr, nullptr, &timeout);
            if (selectResult == 0) {
                continue;
            }
            if (selectResult < 0) {
                if (running) {
                    log("MAIN", "Interface endpoint " + entry.name + " select() failed: " + getSocketErrorText());
                }
                break;
            }

            sockaddr_in peerAddr{};
#ifdef _WIN32
            int peerAddrSize = sizeof(peerAddr);
            const int received = recvfrom(socketHandle,
                                          buffer.data(),
                                          static_cast<int>(buffer.size()),
                                          0,
                                          reinterpret_cast<sockaddr*>(&peerAddr),
                                          &peerAddrSize);
            if (received == SOCKET_ERROR) {
                if (running) {
                    log("MAIN", "Interface endpoint " + entry.name + " recvfrom() failed: socket error " + std::to_string(WSAGetLastError()));
                }
                continue;
            }
#else
            socklen_t peerAddrSize = sizeof(peerAddr);
            const int received = static_cast<int>(recvfrom(socketHandle,
                                                           buffer.data(),
                                                           buffer.size(),
                                                           0,
                                                           reinterpret_cast<sockaddr*>(&peerAddr),
                                                           &peerAddrSize));
            if (received < 0) {
                if (running) {
                    log("MAIN", "Interface endpoint " + entry.name + " recvfrom() failed: " + getSocketErrorText());
                }
                continue;
            }
#endif

            char peerBuffer[INET_ADDRSTRLEN]{};
            std::string peerIp = "unknown";
            if (inet_ntop(AF_INET, &peerAddr.sin_addr, peerBuffer, sizeof(peerBuffer)) != nullptr) {
                peerIp = peerBuffer;
            }

            runtimeState.receivedPackets += 1;
            runtimeState.receivedBytes += static_cast<uint64_t>(received);
            runtimeState.lastPeer = peerIp;
            runtimeState.lastActivityAt = std::time(nullptr);
            runtimeState.reason = "socket bound and receiving datagrams";
            setInterfaceEndpointState(endpointKey, runtimeState);

            std::ostringstream message;
            message << "Interface endpoint " << entry.name
                    << " received " << received << " bytes from " << peerIp;
            log("MAIN", message.str());

            if (entry.name == "S11") {
                const std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + received);
                vepc::GtpV1Header header;
                std::string parseError;
                if (!vepc::parseGtpV1Header(packet, header, parseError)) {
                    log("GTP", "Rejected packet from " + peerIp + ": " + parseError);
                    continue;
                }

                std::ostringstream gtpLog;
                gtpLog << "Parsed GTPv1-C header from " << peerIp
                       << ": type=" << vepc::formatGtpMessageType(header.messageType)
                       << ", teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                       << std::dec << std::setfill(' ')
                       << ", sequence=" << header.sequence
                       << ", payload_length=" << header.payloadLength;
                log("GTP", gtpLog.str());

                if (!handleRealGtpMessage(socketHandle, peerAddr, peerIp, packet, header)) {
                    log("GTP", "No real handler for parsed message type " + vepc::formatGtpMessageType(header.messageType)
                        + "; packet stays in parser-only demo path");
                }
            }
        }
    } else {
        std::vector<char> buffer(4096);
        while (running) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socketHandle, &readSet);

            timeval timeout{};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            const int selectResult = select(static_cast<int>(socketHandle) + 1, &readSet, nullptr, nullptr, &timeout);
            if (selectResult == 0) {
                continue;
            }
            if (selectResult < 0) {
                if (running) {
                    log("MAIN", "Interface endpoint " + entry.name + " select() failed: " + getSocketErrorText());
                }
                break;
            }

            sockaddr_in peerAddr{};
#ifdef _WIN32
            int peerAddrSize = sizeof(peerAddr);
#else
            socklen_t peerAddrSize = sizeof(peerAddr);
#endif
            NativeSocket clientSocket = accept(socketHandle,
                                               reinterpret_cast<sockaddr*>(&peerAddr),
                                               &peerAddrSize);
            if (clientSocket == INVALID_NATIVE_SOCKET) {
                if (running) {
                    log("MAIN", "Interface endpoint " + entry.name + " accept() failed: " + getSocketErrorText());
                }
                continue;
            }

            char peerBuffer[INET_ADDRSTRLEN]{};
            std::string peerIp = "unknown";
            if (inet_ntop(AF_INET, &peerAddr.sin_addr, peerBuffer, sizeof(peerBuffer)) != nullptr) {
                peerIp = peerBuffer;
            }
            log("MAIN", "Interface endpoint " + entry.name + " accepted connection from " + peerIp);

#ifdef _WIN32
            DWORD timeoutMs = 1500;
            setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
            timeval receiveTimeout{};
            receiveTimeout.tv_sec = 1;
            receiveTimeout.tv_usec = 500000;
            setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
#endif

            bool keepConnection = true;
            bool disconnectPeer = false;
            while (keepConnection && running) {
#ifdef _WIN32
            const int received = recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
            const int received = static_cast<int>(recv(clientSocket, buffer.data(), buffer.size(), 0));
#endif
            keepConnection = false;

            if (received > 0) {
                runtimeState.receivedPackets += 1;
                runtimeState.receivedBytes += static_cast<uint64_t>(received);
                runtimeState.lastPeer = peerIp;
                runtimeState.lastActivityAt = std::time(nullptr);
                runtimeState.reason = "listener accepting connections and receiving payloads";
                setInterfaceEndpointState(endpointKey, runtimeState);

                std::ostringstream message;
                message << "Interface endpoint " << entry.name
                        << " received " << received << " bytes from " << peerIp;
                log("MAIN", message.str());

                if (entry.name == "S6a") {
                    const std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + received);
                    vepc::DiameterHeader header;
                    std::string parseError;
                    if (!vepc::parseDiameterHeader(packet, header, parseError)) {
                        runtimeState.lastMessage = "Rejected Diameter";
                        runtimeState.lastDetail = parseError;
                        setInterfaceEndpointState(endpointKey, runtimeState);
                        log("S6A", "Rejected Diameter packet from " + peerIp + ": " + parseError);
                    } else {
                        runtimeState.lastMessage = vepc::formatDiameterCommand(header.commandCode, header.request);
                        runtimeState.lastDetail = "app_id=" + std::to_string(header.applicationId)
                            + ", message_length=" + std::to_string(header.messageLength);

                        std::string diameterDetailForLog;
                        if (header.commandCode == 257 && header.request) {
                            vepc::DiameterCapabilitiesExchangeRequest cer;
                            if (!vepc::parseCapabilitiesExchangeRequest(packet, cer, parseError)) {
                                runtimeState.lastMessage = "Rejected Diameter";
                                runtimeState.lastDetail = parseError;
                                setInterfaceEndpointState(endpointKey, runtimeState);
                                log("S6A", "Rejected Diameter CER from " + peerIp + ": " + parseError);
                                closeNativeSocket(clientSocket);
                                continue;
                            }

                            std::ostringstream cerDetail;
                            if (cer.hasOriginHost) {
                                cerDetail << "origin_host=" << cer.originHost;
                            }
                            if (cer.hasOriginRealm) {
                                if (cerDetail.tellp() > 0) {
                                    cerDetail << ", ";
                                }
                                cerDetail << "origin_realm=" << cer.originRealm;
                            }
                            if (cerDetail.tellp() <= 0) {
                                cerDetail << "no Origin-Host/Origin-Realm AVPs";
                            }

                            runtimeState.lastDetail = cerDetail.str();
                            diameterDetailForLog = runtimeState.lastDetail;
                        } else if (header.commandCode == 280 && header.request) {
                            vepc::DiameterWatchdogRequest dwr;
                            if (!vepc::parseWatchdogRequest(packet, dwr, parseError)) {
                                runtimeState.lastMessage = "Rejected Diameter";
                                runtimeState.lastDetail = parseError;
                                setInterfaceEndpointState(endpointKey, runtimeState);
                                log("S6A", "Rejected Diameter DWR from " + peerIp + ": " + parseError);
                                closeNativeSocket(clientSocket);
                                continue;
                            }

                            std::string dwrDetail;
                            if (dwr.hasOriginHost) {
                                dwrDetail = "origin_host=" + dwr.originHost;
                            } else {
                                dwrDetail = "no Origin-Host AVP";
                            }
                            runtimeState.lastDetail = dwrDetail;
                            diameterDetailForLog = dwrDetail;
                        } else if (header.commandCode == 282 && header.request) {
                            vepc::DiameterDisconnectPeerRequest dpr;
                            if (!vepc::parseDisconnectPeerRequest(packet, dpr, parseError)) {
                                runtimeState.lastMessage = "Rejected Diameter";
                                runtimeState.lastDetail = parseError;
                                setInterfaceEndpointState(endpointKey, runtimeState);
                                log("S6A", "Rejected Diameter DPR from " + peerIp + ": " + parseError);
                                closeNativeSocket(clientSocket);
                                continue;
                            }

                            std::ostringstream dprDetail;
                            if (dpr.hasOriginHost) {
                                dprDetail << "origin_host=" << dpr.originHost;
                            }
                            if (dpr.hasDisconnectCause) {
                                if (dprDetail.tellp() > 0) {
                                    dprDetail << ", ";
                                }
                                dprDetail << "disconnect_cause=" << dpr.disconnectCause;
                            }
                            if (dprDetail.tellp() <= 0) {
                                dprDetail << "no Origin-Host/Disconnect-Cause AVPs";
                            }
                            runtimeState.lastDetail = dprDetail.str();
                            diameterDetailForLog = dprDetail.str();
                        } else if (header.commandCode == 318 && header.request) {
                            vepc::DiameterAuthInfoRequest air;
                            if (!vepc::parseAuthInfoRequest(packet, air, parseError)) {
                                runtimeState.lastMessage = "Rejected Diameter";
                                runtimeState.lastDetail = parseError;
                                setInterfaceEndpointState(endpointKey, runtimeState);
                                log("S6A", "Rejected Diameter AIR from " + peerIp + ": " + parseError);
                                closeNativeSocket(clientSocket);
                                continue;
                            }

                            std::ostringstream airDetail;
                            if (air.hasUserName) {
                                airDetail << "imsi=" << air.userName;
                            }
                            if (air.hasOriginHost) {
                                if (airDetail.tellp() > 0) {
                                    airDetail << ", ";
                                }
                                airDetail << "origin_host=" << air.originHost;
                            }
                            if (airDetail.tellp() <= 0) {
                                airDetail << "no User-Name/Origin-Host AVPs";
                            }
                            runtimeState.lastDetail = airDetail.str();
                            diameterDetailForLog = airDetail.str();
                        } else if (header.commandCode == 316 && header.request) {
                            vepc::DiameterUpdateLocationRequest ulr;
                            if (!vepc::parseUpdateLocationRequest(packet, ulr, parseError)) {
                                runtimeState.lastMessage = "Rejected Diameter";
                                runtimeState.lastDetail = parseError;
                                setInterfaceEndpointState(endpointKey, runtimeState);
                                log("S6A", "Rejected Diameter ULR from " + peerIp + ": " + parseError);
                                closeNativeSocket(clientSocket);
                                continue;
                            }

                            std::ostringstream ulrDetail;
                            if (ulr.hasUserName) {
                                ulrDetail << "imsi=" << ulr.userName;
                            }
                            if (ulr.hasOriginHost) {
                                if (ulrDetail.tellp() > 0) {
                                    ulrDetail << ", ";
                                }
                                ulrDetail << "origin_host=" << ulr.originHost;
                            }
                            if (ulrDetail.tellp() <= 0) {
                                ulrDetail << "no User-Name/Origin-Host AVPs";
                            }
                            runtimeState.lastDetail = ulrDetail.str();
                            diameterDetailForLog = ulrDetail.str();
                        }

                        setInterfaceEndpointState(endpointKey, runtimeState);

                        std::ostringstream diameterLog;
                        diameterLog << "Parsed Diameter header from " << peerIp
                                    << ": command=" << vepc::formatDiameterCommand(header.commandCode, header.request)
                                    << ", app_id=" << header.applicationId
                                    << ", hop_by_hop=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.hopByHopId
                                    << ", end_to_end=0x" << std::setw(8) << header.endToEndId
                                    << std::dec << std::setfill(' ')
                                    << ", message_length=" << header.messageLength;
                        if (!diameterDetailForLog.empty()) {
                            diameterLog << ", " << diameterDetailForLog;
                        }
                        log("S6A", diameterLog.str());

                        if (header.commandCode == 257 && header.request) {
                            const std::string originHost = config.count("s6a-hss-host") != 0 && !config.at("s6a-hss-host").empty()
                                ? config.at("s6a-hss-host")
                                : "hss.vepc.local";
                            const std::string originRealm = config.count("s6a-hss-realm") != 0 && !config.at("s6a-hss-realm").empty()
                                ? config.at("s6a-hss-realm")
                                : "epc.mnc001.mcc001.3gppnetwork.org";
                            const std::vector<uint8_t> response = vepc::buildCapabilitiesExchangeAnswer(header, originHost, originRealm);
#ifdef _WIN32
                            const int sent = send(clientSocket,
                                                  reinterpret_cast<const char*>(response.data()),
                                                  static_cast<int>(response.size()),
                                                  0);
#else
                            const int sent = static_cast<int>(send(clientSocket,
                                                                   response.data(),
                                                                   response.size(),
                                                                   0));
#endif
                            if (sent == static_cast<int>(response.size())) {
                                std::ostringstream responseLog;
                                responseLog << "Sent Diameter response to " << peerIp
                                            << ": command=" << vepc::formatDiameterCommand(257, false)
                                            << ", bytes=" << response.size();
                                log("S6A", responseLog.str());
                            } else {
                                log("S6A", "Failed to send Diameter response to " + peerIp + ": " + getSocketErrorText());
                            }
                        } else if (header.commandCode == 280 && header.request) {
                            const std::string hssHost = config.count("s6a-hss-host") != 0 && !config.at("s6a-hss-host").empty()
                                ? config.at("s6a-hss-host")
                                : "hss.vepc.local";
                            const std::vector<uint8_t> response = vepc::buildWatchdogAnswer(header, hssHost);
#ifdef _WIN32
                            const int sent = send(clientSocket,
                                                  reinterpret_cast<const char*>(response.data()),
                                                  static_cast<int>(response.size()),
                                                  0);
#else
                            const int sent = static_cast<int>(send(clientSocket,
                                                                   response.data(),
                                                                   response.size(),
                                                                   0));
#endif
                            if (sent == static_cast<int>(response.size())) {
                                std::ostringstream responseLog;
                                responseLog << "Sent Diameter response to " << peerIp
                                            << ": command=" << vepc::formatDiameterCommand(280, false)
                                            << ", bytes=" << response.size();
                                log("S6A", responseLog.str());
                            } else {
                                log("S6A", "Failed to send Diameter response to " + peerIp + ": " + getSocketErrorText());
                            }
                        } else if (header.commandCode == 282 && header.request) {
                            const std::string hssHost = config.count("s6a-hss-host") != 0 && !config.at("s6a-hss-host").empty()
                                ? config.at("s6a-hss-host")
                                : "hss.vepc.local";
                            const std::vector<uint8_t> response = vepc::buildDisconnectPeerAnswer(header, hssHost);
#ifdef _WIN32
                            const int sent = send(clientSocket,
                                                  reinterpret_cast<const char*>(response.data()),
                                                  static_cast<int>(response.size()),
                                                  0);
#else
                            const int sent = static_cast<int>(send(clientSocket,
                                                                   response.data(),
                                                                   response.size(),
                                                                   0));
#endif
                            if (sent == static_cast<int>(response.size())) {
                                std::ostringstream responseLog;
                                responseLog << "Sent Diameter response to " << peerIp
                                            << ": command=" << vepc::formatDiameterCommand(282, false)
                                            << ", bytes=" << response.size();
                                log("S6A", responseLog.str());
                            } else {
                                log("S6A", "Failed to send Diameter response to " + peerIp + ": " + getSocketErrorText());
                            }
                            disconnectPeer = true;
                        } else if (header.commandCode == 318 && header.request) {
                            const std::string hssHost = config.count("s6a-hss-host") != 0 && !config.at("s6a-hss-host").empty()
                                ? config.at("s6a-hss-host")
                                : "hss.vepc.local";
                            const std::string hssRealm = config.count("s6a-hss-realm") != 0 && !config.at("s6a-hss-realm").empty()
                                ? config.at("s6a-hss-realm")
                                : "epc.mnc001.mcc001.3gppnetwork.org";
                            const std::vector<uint8_t> response = vepc::buildAuthInfoAnswer(header, hssHost, hssRealm);
#ifdef _WIN32
                            const int sent = send(clientSocket,
                                                  reinterpret_cast<const char*>(response.data()),
                                                  static_cast<int>(response.size()),
                                                  0);
#else
                            const int sent = static_cast<int>(send(clientSocket,
                                                                   response.data(),
                                                                   response.size(),
                                                                   0));
#endif
                            if (sent == static_cast<int>(response.size())) {
                                std::ostringstream responseLog;
                                responseLog << "Sent Diameter response to " << peerIp
                                            << ": command=" << vepc::formatDiameterCommand(318, false)
                                            << ", bytes=" << response.size();
                                log("S6A", responseLog.str());
                            } else {
                                log("S6A", "Failed to send Diameter response to " + peerIp + ": " + getSocketErrorText());
                            }
                        } else if (header.commandCode == 316 && header.request) {
                            const std::string hssHost = config.count("s6a-hss-host") != 0 && !config.at("s6a-hss-host").empty()
                                ? config.at("s6a-hss-host")
                                : "hss.vepc.local";
                            const std::string hssRealm = config.count("s6a-hss-realm") != 0 && !config.at("s6a-hss-realm").empty()
                                ? config.at("s6a-hss-realm")
                                : "epc.mnc001.mcc001.3gppnetwork.org";
                            const std::vector<uint8_t> response = vepc::buildUpdateLocationAnswer(header, hssHost, hssRealm);
#ifdef _WIN32
                            const int sent = send(clientSocket,
                                                  reinterpret_cast<const char*>(response.data()),
                                                  static_cast<int>(response.size()),
                                                  0);
#else
                            const int sent = static_cast<int>(send(clientSocket,
                                                                   response.data(),
                                                                   response.size(),
                                                                   0));
#endif
                            if (sent == static_cast<int>(response.size())) {
                                std::ostringstream responseLog;
                                responseLog << "Sent Diameter response to " << peerIp
                                            << ": command=" << vepc::formatDiameterCommand(316, false)
                                            << ", bytes=" << response.size();
                                log("S6A", responseLog.str());
                            } else {
                                log("S6A", "Failed to send Diameter response to " + peerIp + ": " + getSocketErrorText());
                            }
                        }
                    }
                    keepConnection = (entry.name == "S6a") && !disconnectPeer;
                }
            } else if (received < 0 && running) {
                log("MAIN", "Interface endpoint " + entry.name + " recv() failed: " + getSocketErrorText());
            }
            }  // end while keepConnection

            closeNativeSocket(clientSocket);
        }
    }

    runtimeState.listenerActive = false;
    runtimeState.reason = "endpoint stopped";
    setInterfaceEndpointState(endpointKey, runtimeState);
    closeNativeSocket(socketHandle);
#ifdef _WIN32
    WSACleanup();
#endif
}

void VNodeController::startConfiguredInterfaceEndpoints() {
    const auto entries = loadInterfaceConfigEntries();
    std::set<std::string> startedEndpoints;
    for (const auto& entry : entries) {
        if (!isGenericEndpointProtocol(entry, config)) {
            continue;
        }

        const std::string endpointKey = makeEndpointKey(entry);
        if (!startedEndpoints.insert(endpointKey).second) {
            continue;
        }

        InterfaceEndpointRuntime runtimeState;
        runtimeState.handlerImplemented = true;
        runtimeState.reason = "starting endpoint thread";
        setInterfaceEndpointState(endpointKey, runtimeState);
        endpointThreads.emplace_back(&VNodeController::interfaceEndpointThread, this, entry);
    }
}

InterfaceDiagnostics VNodeController::buildInterfaceDiagnostics(const InterfaceConfigEntry& entry) const {
    InterfaceDiagnostics diagnostics;

    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        const auto adminIt = interfaceAdminState.find(entry.name);
        if (adminIt != interfaceAdminState.end()) {
            diagnostics.adminUp = adminIt->second;
        }
    }

    std::string mme;
    std::string sgsn;
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        mme = mmeStatus;
        sgsn = sgsnStatus;
    }

    diagnostics.localBind = isLocalInterfaceAddress(resolveInterfaceBindIp(entry, config), config);

    const auto s1apPortIt = config.find("s1ap-port");
    const auto gtpControlPortIt = config.find("gtp-c-port");
    const auto gtpUserPortIt = config.find("gtp-u-port");
    const bool connectionOriented = isConnectionOrientedProtocol(entry);
    const bool isS1ap = s1apPortIt != config.end() && entry.proto == "SCTP" && entry.port == s1apPortIt->second;
    const bool isGenericProtocol = isGenericEndpointProtocol(entry, config);
    const bool genericOwnsGtpControl = entry.name == "S11" && isGenericProtocol;
    const bool isGtpControl = gtpControlPortIt != config.end()
        && entry.proto == "UDP"
        && entry.port == gtpControlPortIt->second
        && !genericOwnsGtpControl;
    const bool isGtpUser = gtpUserPortIt != config.end() && entry.proto == "UDP" && entry.port == gtpUserPortIt->second;
    const InterfaceEndpointRuntime endpointRuntime = getInterfaceEndpointState(entry);

    if (!diagnostics.adminUp) {
        diagnostics.operState = "DOWN";
        diagnostics.implementation = (isS1ap || isGtpControl || endpointRuntime.handlerImplemented) ? "IMPLEMENTED" : "PLANNED";
        diagnostics.reason = "administratively disabled";
        diagnostics.bindState = "DISABLED";
        diagnostics.bindReason = "administratively disabled";
        diagnostics.listenState = connectionOriented ? "DISABLED" : "N/A";
        diagnostics.listenReason = connectionOriented ? "administratively disabled" : "protocol does not use listen()";
        return diagnostics;
    }

    if (isS1ap) {
        diagnostics.implementation = "IMPLEMENTED";
        if (!diagnostics.localBind) {
            diagnostics.operState = "DEGRADED";
            diagnostics.reason = "configured on non-local address";
            diagnostics.bindState = "FAILED";
            diagnostics.bindReason = "configured on non-local address";
            diagnostics.listenState = "BLOCKED";
            diagnostics.listenReason = "bind stage did not pass";
        } else if (mme == "Running") {
            diagnostics.operState = "UP";
            diagnostics.reason = "S1AP server thread is active";
            diagnostics.bindState = "OK";
            diagnostics.bindReason = "S1AP socket is managed by the MME thread";
            diagnostics.listenState = "OK";
            diagnostics.listenReason = "S1AP server thread is active";
        } else {
            diagnostics.operState = "DOWN";
            diagnostics.reason = "S1AP server thread is not running";
            diagnostics.bindState = "UNKNOWN";
            diagnostics.bindReason = "S1AP bind is managed by the MME thread";
            diagnostics.listenState = "DOWN";
            diagnostics.listenReason = "S1AP server thread is not running";
        }
        return diagnostics;
    }

    if (isGtpControl) {
        diagnostics.implementation = "IMPLEMENTED";
        if (!diagnostics.localBind) {
            diagnostics.operState = "DEGRADED";
            diagnostics.reason = "configured on non-local address";
            diagnostics.bindState = "FAILED";
            diagnostics.bindReason = "configured on non-local address";
            diagnostics.listenState = "N/A";
            diagnostics.listenReason = "protocol does not use listen()";
        } else if (sgsn == "Running") {
            diagnostics.operState = "UP";
            diagnostics.reason = "GTP-C server thread is active";
            diagnostics.bindState = "OK";
            diagnostics.bindReason = "GTP-C socket is managed by the SGSN thread";
            diagnostics.listenState = "N/A";
            diagnostics.listenReason = "protocol does not use listen()";
        } else {
            diagnostics.operState = "DOWN";
            diagnostics.reason = "GTP-C server thread is not running";
            diagnostics.bindState = "UNKNOWN";
            diagnostics.bindReason = "GTP-C bind is managed by the SGSN thread";
            diagnostics.listenState = "N/A";
            diagnostics.listenReason = "protocol does not use listen()";
        }
        return diagnostics;
    }

    if (isGtpUser || isGenericProtocol) {
        diagnostics.implementation = endpointRuntime.handlerImplemented ? "IMPLEMENTED" : "PLANNED";
        if (!endpointRuntime.handlerImplemented) {
            diagnostics.operState = "PLANNED";
            diagnostics.reason = "protocol handler is not implemented in current build";
            diagnostics.bindState = "PLANNED";
            diagnostics.bindReason = diagnostics.reason;
            diagnostics.listenState = connectionOriented ? "PLANNED" : "N/A";
            diagnostics.listenReason = connectionOriented ? diagnostics.reason : "protocol does not use listen()";
        } else if (!diagnostics.localBind) {
            diagnostics.operState = "DOWN";
            diagnostics.reason = endpointRuntime.reason.empty() ? "configured on non-local address" : endpointRuntime.reason;
            diagnostics.bindState = "FAILED";
            diagnostics.bindReason = diagnostics.reason;
            diagnostics.listenState = connectionOriented ? "BLOCKED" : "N/A";
            diagnostics.listenReason = connectionOriented ? "bind stage did not pass" : "protocol does not use listen()";
        } else if (endpointRuntime.listenerActive) {
            diagnostics.operState = "UP";
            diagnostics.reason = endpointRuntime.reason.empty() ? "endpoint is active" : endpointRuntime.reason;
            diagnostics.bindState = "OK";
            diagnostics.bindReason = "socket bound successfully";
            diagnostics.listenState = connectionOriented ? "OK" : "N/A";
            diagnostics.listenReason = connectionOriented ? diagnostics.reason : "protocol does not use listen()";
        } else {
            diagnostics.operState = "DOWN";
            diagnostics.reason = endpointRuntime.reason.empty() ? "endpoint failed to start" : endpointRuntime.reason;

            if (startsWith(diagnostics.reason, "listen() failed:")) {
                diagnostics.bindState = "OK";
                diagnostics.bindReason = "bind() completed successfully";
                diagnostics.listenState = "FAILED";
                diagnostics.listenReason = diagnostics.reason;
            } else if (startsWith(diagnostics.reason, "bind() failed:")) {
                diagnostics.bindState = "FAILED";
                diagnostics.bindReason = diagnostics.reason;
                diagnostics.listenState = connectionOriented ? "BLOCKED" : "N/A";
                diagnostics.listenReason = connectionOriented ? "bind stage did not pass" : "protocol does not use listen()";
            } else if (diagnostics.reason == "starting endpoint thread") {
                diagnostics.bindState = "PENDING";
                diagnostics.bindReason = "endpoint thread is starting";
                diagnostics.listenState = connectionOriented ? "PENDING" : "N/A";
                diagnostics.listenReason = connectionOriented ? "waiting for bind() to complete" : "protocol does not use listen()";
            } else if (diagnostics.reason == "endpoint stopped") {
                diagnostics.bindState = "STOPPED";
                diagnostics.bindReason = "endpoint was previously bound and then stopped";
                diagnostics.listenState = connectionOriented ? "STOPPED" : "N/A";
                diagnostics.listenReason = connectionOriented ? "listener stopped with the endpoint" : "protocol does not use listen()";
            } else {
                diagnostics.bindState = "FAILED";
                diagnostics.bindReason = diagnostics.reason;
                diagnostics.listenState = connectionOriented ? "BLOCKED" : "N/A";
                diagnostics.listenReason = connectionOriented ? "endpoint did not reach listen()" : "protocol does not use listen()";
            }
        }
        return diagnostics;
    }

    diagnostics.implementation = "PLANNED";
    diagnostics.operState = "PLANNED";
    diagnostics.reason = "protocol handler is not implemented in current build";
    diagnostics.bindState = "PLANNED";
    diagnostics.bindReason = diagnostics.reason;
    diagnostics.listenState = connectionOriented ? "PLANNED" : "N/A";
    diagnostics.listenReason = connectionOriented ? diagnostics.reason : "protocol does not use listen()";
    return diagnostics;
}

std::string VNodeController::formatInterfaceStatus(const std::string& name) const {
    const auto entries = loadInterfaceConfigEntries();
    const auto entryIt = std::find_if(entries.begin(), entries.end(), [&](const InterfaceConfigEntry& entry) {
        return entry.name == name;
    });
    if (entryIt == entries.end()) {
        return "Interface '" + name + "' not found in interfaces.conf\n";
    }

    const InterfaceEndpointRuntime endpointRuntime = getInterfaceEndpointState(*entryIt);
    const std::string bindIp = resolveInterfaceBindIp(*entryIt, config);
    const InterfaceDiagnostics diagnostics = buildInterfaceDiagnostics(*entryIt);

    std::ostringstream oss;
    oss << "Interface " << entryIt->name << " status:\n"
        << "  Protocol      : " << entryIt->proto << "\n"
        << "  Address       : " << entryIt->address << "\n"
        << "  Effective Bind: " << bindIp << (entryIt->port.empty() ? "" : ":" + entryIt->port) << "\n"
        << "  Peer          : " << entryIt->peer << "\n"
        << "  Admin State   : " << (diagnostics.adminUp ? "UP" : "DOWN") << "\n"
        << "  Oper State    : " << diagnostics.operState << "\n"
        << "  Implementation: " << diagnostics.implementation << "\n"
        << "  Local Bind    : " << (diagnostics.localBind ? "YES" : "NO") << "\n"
        << "  Bind State    : " << diagnostics.bindState << "\n"
        << "  Bind Reason   : " << diagnostics.bindReason << "\n"
        << "  Listen State  : " << diagnostics.listenState << "\n"
        << "  Listen Reason : " << diagnostics.listenReason << "\n"
        << "  Reason        : " << diagnostics.reason << "\n"
        << "  Rx Packets    : " << endpointRuntime.receivedPackets << "\n"
        << "  Rx Bytes      : " << endpointRuntime.receivedBytes << "\n"
        << "  Last Peer     : " << (endpointRuntime.lastPeer.empty() ? "n/a" : endpointRuntime.lastPeer) << "\n"
        << "  Last Message  : " << (endpointRuntime.lastMessage.empty() ? "n/a" : endpointRuntime.lastMessage) << "\n"
        << "  Last Detail   : " << (endpointRuntime.lastDetail.empty() ? "n/a" : endpointRuntime.lastDetail) << "\n"
        << "  Last Activity : " << (endpointRuntime.lastActivityAt == 0 ? "n/a" : formatTimestamp(endpointRuntime.lastActivityAt)) << "\n";
    return oss.str();
}

std::string VNodeController::formatInterfaceOverview() const {
    const auto entries = loadInterfaceConfigEntries();
    if (entries.empty()) {
        return "No interfaces loaded from interfaces.conf\n";
    }

    std::ostringstream oss;
    oss << std::left
        << std::setw(12) << "Name"
        << std::setw(12) << "Proto"
        << std::setw(22) << "Address"
        << std::setw(12) << "Admin"
        << std::setw(12) << "Oper"
        << std::setw(16) << "Impl"
        << "Peer\n";
    oss << std::string(96, '-') << "\n";

    for (const auto& entry : entries) {
        const InterfaceDiagnostics diagnostics = buildInterfaceDiagnostics(entry);

        oss << std::left
            << std::setw(12) << entry.name
            << std::setw(12) << entry.proto
            << std::setw(22) << entry.address
            << std::setw(12) << (diagnostics.adminUp ? "UP" : "DOWN")
            << std::setw(12) << diagnostics.operState
            << std::setw(16) << diagnostics.implementation
            << entry.peer << "\n"
            << "  diag: bind=" << diagnostics.bindState << " (" << diagnostics.bindReason << ")"
            << "; listen=" << diagnostics.listenState << " (" << diagnostics.listenReason << ")"
            << "; reason=" << diagnostics.reason << "\n";
    }

    return oss.str();
}

std::string VNodeController::applyInterfaceAction(const std::string& action, const std::string& name) {
    const auto entries = loadInterfaceConfigEntries();
    const auto entryIt = std::find_if(entries.begin(), entries.end(), [&](const InterfaceConfigEntry& entry) {
        return entry.name == name;
    });
    if (entryIt == entries.end()) {
        return "Interface '" + name + "' not found in interfaces.conf\n";
    }

    std::string actionText;
    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        if (action == "up") {
            interfaceAdminState[name] = true;
            actionText = "set to administrative UP";
        } else if (action == "down") {
            interfaceAdminState[name] = false;
            actionText = "set to administrative DOWN";
        } else {
            interfaceAdminState.erase(name);
            actionText = "reset to default administrative state";
        }
    }

    std::string persistWarning;
    try {
        saveInterfaceAdminStateToFile();
    } catch (const std::exception& ex) {
        log("MAIN", ex.what());
        persistWarning = std::string("Warning: ") + ex.what() + "\n";
    }

    log("MAIN", "Interface " + name + " " + actionText);

    std::ostringstream oss;
    if (!persistWarning.empty()) {
        oss << persistWarning;
    }
    oss << "Interface " << name << " " << actionText << "\n";
    oss << formatInterfaceStatus(name);
    return oss.str();
}

std::string VNodeController::handleCliCommand(const std::string& cmd, bool& shouldStop) {
    shouldStop = false;

    if (cmd == "status") {
        return getStatus();
    }
    if (cmd == "logs" || cmd == "log") {
        std::lock_guard<std::mutex> lock(logMutex);
        std::ostringstream oss;
        const size_t start = logs.size() > 20 ? logs.size() - 20 : 0;
        for (size_t i = start; i < logs.size(); ++i) {
            char buf[64];
            std::tm localTime{};
            if (tryGetLocalTime(logs[i].time, localTime)) {
                std::strftime(buf, sizeof(buf), "%H:%M:%S", &localTime);
                oss << "[" << buf << "][" << logs[i].node << "] " << logs[i].msg << "\n";
            } else {
                oss << "[time?][" << logs[i].node << "] " << logs[i].msg << "\n";
            }
        }
        return oss.str();
    }
    if (cmd == "state") {
        std::lock_guard<std::mutex> lock(stateMutex);
        return formatStateSnapshotLocked();
    }
    if (cmd == "show" || cmd == "show config") {
        std::ostringstream oss;
        for (const auto& item : config) {
            oss << item.first << " = " << item.second << "\n";
        }
        return oss.str();
    }
    if (cmd == "show iface" || cmd == "show interface") {
        return formatInterfaceOverview();
    }
    if (cmd.rfind("set ", 0) == 0) {
        std::istringstream iss(cmd);
        std::string action;
        std::string key;
        iss >> action >> key;
        std::string value;
        std::getline(iss, value);
        value = trimCopy(value);
        if (key.empty() || value.empty()) {
            return "Usage: set <key> <value>\n";
        }
        setValue(key, value);
        return "Set " + key + " = " + value + "\n";
    }
    if (cmd == "stop") {
        shouldStop = true;
        return "vEPC is stopping...\n";
    }
    if (cmd == "restart") {
        restartRequested = true;
        closeCliListenSocket();
        std::ostringstream oss;
        oss << "vEPC restart requested\n"
            << "Current Status : " << getStatus() << "\n"
            << "Reload Configs : " << CONFIG_FILE << ", " << MME_CONFIG_FILE << ", " << SGSN_CONFIG_FILE << ", " << INTERFACE_ADMIN_STATE_FILE << "\n"
            << "CLI Endpoint   : " << CLI_ENDPOINT << "\n"
            << "Next Step      : server threads will stop, config will be reloaded, and CLI will become available again after restart\n";
        return oss.str();
    }
    if (cmd.rfind("iface_status ", 0) == 0) {
        return formatInterfaceStatus(cmd.substr(13));
    }
    if (cmd.rfind("iface_up ", 0) == 0) {
        return applyInterfaceAction("up", cmd.substr(9));
    }
    if (cmd.rfind("iface_down ", 0) == 0) {
        return applyInterfaceAction("down", cmd.substr(11));
    }
    if (cmd.rfind("iface_reset ", 0) == 0) {
        return applyInterfaceAction("reset", cmd.substr(12));
    }

    return "Unknown command: " + cmd + "\n"
        "Available: status, logs, state, show, show interface, show iface, set <key> <value>, restart, stop\n";
}

// ----------------------------------------------------------------
//  Статус / состояние
// ----------------------------------------------------------------

std::string VNodeController::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex);
    std::ostringstream oss;
    oss << "MME: "  << mmeStatus
        << " | SGSN: " << sgsnStatus
        << " | PLMN: " << config.at("mcc") << "-" << config.at("mnc");
    return oss.str();
}

std::string VNodeController::formatStateSnapshotLocked() const {
    std::ostringstream oss;
    oss << "PDP contexts: " << pdpContexts.size() << "\n";
    if (pdpContexts.empty()) {
        oss << "PDP context details: none\n";
    } else {
        oss << "PDP context index: TEID\n";
        for (const auto& [teid, context] : pdpContexts) {
            oss << "- TEID: 0x"
                << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << teid
                << std::dec << std::setfill(' ') << "\n";
            oss << "  Sequence: " << context.sequence << "\n";
            oss << "  Message Type: " << vepc::formatGtpMessageType(context.last_message_type) << "\n";
            oss << "  Peer IP: " << (context.peer_ip.empty() ? "n/a" : context.peer_ip) << "\n";
            oss << "  IMSI: " << (context.imsi.empty() ? "n/a" : context.imsi) << "\n";
            oss << "  APN: " << (context.apn.empty() ? "n/a" : context.apn) << "\n";
            oss << "  PDP Type: " << formatPdpTypeValue(context) << "\n";
            oss << "  GGSN IP: " << (context.ggsn_ip.empty() ? "n/a" : context.ggsn_ip) << "\n";
            oss << "  Updated At: " << formatTimestamp(context.updated_at) << "\n";
        }
    }

    const auto interfaceEntries = loadInterfaceConfigEntries();
    std::set<std::string> renderedEndpoints;
    bool hasEndpointTelemetry = false;
    for (const auto& entry : interfaceEntries) {
        if (!isGenericEndpointProtocol(entry, config)) {
            continue;
        }

        const std::string endpointKey = makeEndpointKey(entry);
        if (!renderedEndpoints.insert(endpointKey).second) {
            continue;
        }

        const InterfaceEndpointRuntime endpointRuntime = getInterfaceEndpointState(entry);
        if (!endpointRuntime.handlerImplemented) {
            continue;
        }

        if (!hasEndpointTelemetry) {
            oss << "Endpoint telemetry:\n";
            hasEndpointTelemetry = true;
        }

        oss << "- Name: " << entry.name << "\n";
        oss << "  Protocol: " << entry.proto << "\n";
        oss << "  Address: " << entry.address << "\n";
        oss << "  Effective Bind: " << resolveInterfaceBindIp(entry, config)
            << (entry.port.empty() ? "" : ":" + entry.port) << "\n";
        oss << "  Active: " << (endpointRuntime.listenerActive ? "yes" : "no") << "\n";
        oss << "  Rx Packets: " << endpointRuntime.receivedPackets << "\n";
        oss << "  Rx Bytes: " << endpointRuntime.receivedBytes << "\n";
        oss << "  Last Peer: " << (endpointRuntime.lastPeer.empty() ? "n/a" : endpointRuntime.lastPeer) << "\n";
        oss << "  Last Message: " << (endpointRuntime.lastMessage.empty() ? "n/a" : endpointRuntime.lastMessage) << "\n";
        oss << "  Last Detail: " << (endpointRuntime.lastDetail.empty() ? "n/a" : endpointRuntime.lastDetail) << "\n";
        oss << "  Last Activity: " << (endpointRuntime.lastActivityAt == 0 ? "n/a" : formatTimestamp(endpointRuntime.lastActivityAt)) << "\n";
    }
    if (!hasEndpointTelemetry) {
        oss << "Endpoint telemetry: none\n";
    }

    oss << "UE contexts:  " << ueContexts.size() << "\n";
    if (ueContexts.empty()) {
        oss << "UE context details: none\n";
    } else {
        oss << "UE context index: IMSI\n";
        for (const auto& [imsi, context] : ueContexts) {
            oss << "- IMSI: " << (imsi.empty() ? "n/a" : imsi) << "\n";
            oss << "  GUTI: " << (context.guti.empty() ? "n/a" : context.guti) << "\n";
            oss << "  Peer: " << (context.peer_id.empty() ? "n/a" : context.peer_id) << "\n";
            oss << "  S1AP Procedure: " << formatS1apProcedureValue(context) << "\n";
            oss << "  NAS Message: " << formatNasMessageTypeValue(context) << "\n";
            oss << "  Security Context ID: " << formatSecurityContextIdValue(context) << "\n";
            oss << "  NAS Algorithm: " << formatNasAlgorithmValue(context) << "\n";
            oss << "  Default Bearer ID: " << formatBearerIdValue(context) << "\n";
            oss << "  Tracking Area Code: " << formatTrackingAreaCodeValue(context) << "\n";
            oss << "  Auth Flow: " << formatAuthFlowState(context) << "\n";
            oss << "  Auth Request Sent: " << (context.auth_request_sent ? "yes" : "no") << "\n";
            oss << "  Auth Response Received: " << (context.auth_response_received ? "yes" : "no") << "\n";
            oss << "  Security Mode Command Sent: " << (context.security_mode_command_sent ? "yes" : "no") << "\n";
            oss << "  Security Mode Complete: " << (context.security_mode_complete ? "yes" : "no") << "\n";
            oss << "  Attach Accept Sent: " << (context.attach_accept_sent ? "yes" : "no") << "\n";
            oss << "  Attach Complete Received: " << (context.attach_complete_received ? "yes" : "no") << "\n";
            oss << "  Attached: " << (context.attached ? "yes" : "no") << "\n";
            oss << "  Service Request Received: " << (context.service_request_received ? "yes" : "no") << "\n";
            oss << "  Service Accept Sent: " << (context.service_accept_sent ? "yes" : "no") << "\n";
            oss << "  Service Active: " << (context.service_active ? "yes" : "no") << "\n";
            oss << "  Service Resume Request Received: " << (context.service_resume_request_received ? "yes" : "no") << "\n";
            oss << "  Service Resume Accept Sent: " << (context.service_resume_accept_sent ? "yes" : "no") << "\n";
            oss << "  Service Release Request Received: " << (context.service_release_request_received ? "yes" : "no") << "\n";
            oss << "  Service Release Complete Sent: " << (context.service_release_complete_sent ? "yes" : "no") << "\n";
            oss << "  Detach Request Received: " << (context.detach_request_received ? "yes" : "no") << "\n";
            oss << "  Detach Accept Sent: " << (context.detach_accept_sent ? "yes" : "no") << "\n";
            oss << "  Detached: " << (context.detached ? "yes" : "no") << "\n";
            oss << "  TAU Request Received: " << (context.tracking_area_update_request_received ? "yes" : "no") << "\n";
            oss << "  TAU Accept Sent: " << (context.tracking_area_update_accept_sent ? "yes" : "no") << "\n";
            oss << "  TAU Complete Received: " << (context.tracking_area_update_complete_received ? "yes" : "no") << "\n";
            oss << "  Authenticated: " << (context.authenticated ? "yes" : "no") << "\n";
            oss << "  Updated At: " << formatTimestamp(context.updated_at) << "\n";
        }
    }
    return oss.str();
}

void VNodeController::upsertPdpContext(const PDPContext& context) {
    std::lock_guard<std::mutex> lock(stateMutex);
    pdpContexts[context.teid] = context;
}

bool VNodeController::tryGetPdpContext(uint32_t teid, PDPContext& context) const {
    std::lock_guard<std::mutex> lock(stateMutex);
    const auto it = pdpContexts.find(teid);
    if (it == pdpContexts.end()) {
        return false;
    }
    context = it->second;
    return true;
}

bool VNodeController::removePdpContext(uint32_t teid, PDPContext* removedContext) {
    std::lock_guard<std::mutex> lock(stateMutex);
    const auto it = pdpContexts.find(teid);
    if (it == pdpContexts.end()) {
        return false;
    }
    if (removedContext != nullptr) {
        *removedContext = it->second;
    }
    pdpContexts.erase(it);
    return true;
}

void VNodeController::upsertUeContext(const UEContext& context) {
    std::lock_guard<std::mutex> lock(stateMutex);
    ueContexts[context.imsi] = context;
}

void VNodeController::clearRuntimeState() {
    std::lock_guard<std::mutex> lock(stateMutex);
    pdpContexts.clear();
    ueContexts.clear();
}

void VNodeController::saveRuntimeStateToFile() {
    const std::string outputPath = resolveWritableConfigPath(RUNTIME_STATE_FILE);
    std::map<std::string, bool> interfaceStateCopy;
    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        interfaceStateCopy = interfaceAdminState;
    }

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"schema_version\": " << RUNTIME_STATE_SCHEMA_VERSION << ",\n";
    oss << "  \"saved_at\": \"" << escapeJsonString(formatTimestamp(std::time(nullptr))) << "\",\n";
    oss << "  \"metadata\": {\n";
    appendJsonStringField(oss, "cli_endpoint", CLI_ENDPOINT);
    appendJsonStringField(oss, "status", getStatus());
    appendJsonIntField(oss, "interface_admin_state_count", interfaceStateCopy.size(), false);
    oss << "  },\n";
    oss << "  \"interface_admin_state\": [\n";

    bool firstInterface = true;
    for (const auto& [name, adminUp] : interfaceStateCopy) {
        if (!firstInterface) {
            oss << ",\n";
        }
        firstInterface = false;
        oss << "    {\n";
        appendJsonStringField(oss, "name", name);
        appendJsonBoolField(oss, "admin_up", adminUp, false);
        oss << "    }";
    }

    oss << "\n  ],\n";
    oss << "  \"pdp_contexts\": [\n";

    {
        std::lock_guard<std::mutex> lock(stateMutex);

        bool firstPdp = true;
        for (const auto& entry : pdpContexts) {
            const PDPContext& context = entry.second;
            if (!firstPdp) {
                oss << ",\n";
            }
            firstPdp = false;

            oss << "    {\n";
            appendJsonIntField(oss, "teid", context.teid);
            appendJsonIntField(oss, "sequence", context.sequence);
            appendJsonIntField(oss, "pdp_type", context.pdp_type);
            appendJsonBoolField(oss, "has_pdp_type", context.has_pdp_type);
            appendJsonIntField(oss, "last_message_type", context.last_message_type);
            appendJsonStringField(oss, "peer_ip", context.peer_ip);
            appendJsonStringField(oss, "ggsn_ip", context.ggsn_ip);
            appendJsonStringField(oss, "imsi", context.imsi);
            appendJsonStringField(oss, "apn", context.apn);
            appendJsonStringField(oss, "updated_at", formatTimestamp(context.updated_at), false);
            oss << "    }";
        }

        oss << "\n  ],\n";
        oss << "  \"ue_contexts\": [\n";

        bool firstUe = true;
        for (const auto& entry : ueContexts) {
            const UEContext& context = entry.second;
            if (!firstUe) {
                oss << ",\n";
            }
            firstUe = false;

            oss << "    {\n";
            appendJsonStringField(oss, "imsi", context.imsi);
            appendJsonStringField(oss, "guti", context.guti);
            appendJsonStringField(oss, "peer_id", context.peer_id);
            appendJsonBoolField(oss, "authenticated", context.authenticated);
            appendJsonBoolField(oss, "auth_request_sent", context.auth_request_sent);
            appendJsonBoolField(oss, "auth_response_received", context.auth_response_received);
            appendJsonBoolField(oss, "security_mode_command_sent", context.security_mode_command_sent);
            appendJsonBoolField(oss, "security_mode_complete", context.security_mode_complete);
            appendJsonBoolField(oss, "attach_accept_sent", context.attach_accept_sent);
            appendJsonBoolField(oss, "attach_complete_received", context.attach_complete_received);
            appendJsonBoolField(oss, "attached", context.attached);
            appendJsonBoolField(oss, "service_request_received", context.service_request_received);
            appendJsonBoolField(oss, "service_accept_sent", context.service_accept_sent);
            appendJsonBoolField(oss, "service_active", context.service_active);
            appendJsonBoolField(oss, "service_resume_request_received", context.service_resume_request_received);
            appendJsonBoolField(oss, "service_resume_accept_sent", context.service_resume_accept_sent);
            appendJsonBoolField(oss, "service_release_request_received", context.service_release_request_received);
            appendJsonBoolField(oss, "service_release_complete_sent", context.service_release_complete_sent);
            appendJsonBoolField(oss, "detach_request_received", context.detach_request_received);
            appendJsonBoolField(oss, "detach_accept_sent", context.detach_accept_sent);
            appendJsonBoolField(oss, "detached", context.detached);
            appendJsonBoolField(oss, "tracking_area_update_request_received", context.tracking_area_update_request_received);
            appendJsonBoolField(oss, "tracking_area_update_accept_sent", context.tracking_area_update_accept_sent);
            appendJsonBoolField(oss, "tracking_area_update_complete_received", context.tracking_area_update_complete_received);
            appendJsonIntField(oss, "last_nas_message_type", context.last_nas_message_type);
            appendJsonBoolField(oss, "has_last_nas_message_type", context.has_last_nas_message_type);
            appendJsonIntField(oss, "last_s1ap_procedure", context.last_s1ap_procedure);
            appendJsonIntField(oss, "security_context_id", context.security_context_id);
            appendJsonBoolField(oss, "has_security_context_id", context.has_security_context_id);
            appendJsonIntField(oss, "selected_nas_algorithm", context.selected_nas_algorithm);
            appendJsonBoolField(oss, "has_selected_nas_algorithm", context.has_selected_nas_algorithm);
            appendJsonIntField(oss, "default_bearer_id", context.default_bearer_id);
            appendJsonBoolField(oss, "has_default_bearer_id", context.has_default_bearer_id);
            appendJsonIntField(oss, "tracking_area_code", context.tracking_area_code);
            appendJsonBoolField(oss, "has_tracking_area_code", context.has_tracking_area_code);
            appendJsonStringField(oss, "auth_flow", formatAuthFlowState(context));
            appendJsonStringField(oss, "updated_at", formatTimestamp(context.updated_at), false);
            oss << "    }";
        }
    }

    oss << "\n  ]\n";
    oss << "}\n";

    std::filesystem::path outputFile(outputPath);
    if (outputFile.has_parent_path()) {
        std::filesystem::create_directories(outputFile.parent_path());
    }

    std::ofstream stateFile(outputPath, std::ios::trunc);
    if (!stateFile) {
        throw std::runtime_error("Failed to open runtime state file: " + outputPath);
    }

    stateFile << oss.str();
    if (!stateFile.good()) {
        throw std::runtime_error("Failed to write runtime state file: " + outputPath);
    }

    log("MAIN", "Runtime state saved to " + outputPath);
}

void VNodeController::loadRuntimeStateFromFile() {
    clearRuntimeState();

    const std::string inputPath = resolveWritableConfigPath(RUNTIME_STATE_FILE);
    if (!std::filesystem::exists(inputPath)) {
        log("MAIN", "Runtime state file not found, starting with empty contexts: " + inputPath);
        return;
    }

    std::ifstream stateFile(inputPath);
    if (!stateFile) {
        throw std::runtime_error("Failed to open runtime state file: " + inputPath);
    }

    std::ostringstream buffer;
    buffer << stateFile.rdbuf();
    const std::string json = buffer.str();

    uint64_t schemaVersion = 0;
    if (!extractJsonUintField(json, "schema_version", schemaVersion)) {
        throw std::runtime_error("Runtime state file is missing schema_version: " + inputPath);
    }
    if (schemaVersion != RUNTIME_STATE_SCHEMA_VERSION) {
        std::ostringstream oss;
        oss << "Unsupported runtime state schema_version=" << schemaVersion
            << " (expected=" << RUNTIME_STATE_SCHEMA_VERSION << "): " << inputPath;
        throw std::runtime_error(oss.str());
    }

    std::string savedAt;
    std::time_t savedAtTime = 0;
    if (!extractJsonStringField(json, "saved_at", savedAt) || !parseTimestamp(savedAt, savedAtTime)) {
        throw std::runtime_error("Runtime state file has invalid saved_at timestamp: " + inputPath);
    }

    size_t arrayStart = 0;
    size_t arrayEnd = 0;

    std::map<std::string, bool> loadedInterfaceState;
    if (findJsonArrayBounds(json, "interface_admin_state", arrayStart, arrayEnd)) {
        std::set<std::string> validInterfaces;
        for (const auto& entry : loadInterfaceConfigEntries()) {
            validInterfaces.insert(entry.name);
        }

        const auto objects = splitTopLevelJsonObjects(json.substr(arrayStart, arrayEnd - arrayStart));
        for (const auto& object : objects) {
            std::string name;
            bool adminUp = true;
            if (!extractJsonStringField(object, "name", name) || name.empty()) {
                throw std::runtime_error("Runtime state interface_admin_state entry is missing name: " + inputPath);
            }
            if (!extractJsonBoolField(object, "admin_up", adminUp)) {
                throw std::runtime_error("Runtime state interface_admin_state entry is missing admin_up: " + inputPath);
            }
            if (!validInterfaces.empty() && validInterfaces.find(name) == validInterfaces.end()) {
                throw std::runtime_error("Runtime state interface_admin_state entry references unknown interface: " + name);
            }
            loadedInterfaceState[name] = adminUp;
        }
    }

    std::vector<PDPContext> loadedPdpContexts;
    if (!findJsonArrayBounds(json, "pdp_contexts", arrayStart, arrayEnd)) {
        throw std::runtime_error("Runtime state file is missing pdp_contexts array: " + inputPath);
    }
    {
        const auto objects = splitTopLevelJsonObjects(json.substr(arrayStart, arrayEnd - arrayStart));
        for (const auto& object : objects) {
            PDPContext context;
            uint64_t number = 0;
            std::string timestamp;

            if (!extractJsonUintField(object, "teid", number)) {
                throw std::runtime_error("Runtime PDP context is missing teid: " + inputPath);
            }
            context.teid = static_cast<uint32_t>(number);
            if (extractJsonUintField(object, "sequence", number)) {
                context.sequence = static_cast<uint16_t>(number);
            }
            if (extractJsonUintField(object, "pdp_type", number)) {
                context.pdp_type = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_pdp_type", context.has_pdp_type);
            if (extractJsonUintField(object, "last_message_type", number)) {
                context.last_message_type = static_cast<uint8_t>(number);
            }
            extractJsonStringField(object, "peer_ip", context.peer_ip);
            extractJsonStringField(object, "ggsn_ip", context.ggsn_ip);
            extractJsonStringField(object, "imsi", context.imsi);
            extractJsonStringField(object, "apn", context.apn);
            if (extractJsonStringField(object, "updated_at", timestamp) && !parseTimestamp(timestamp, context.updated_at)) {
                throw std::runtime_error("Runtime PDP context has invalid updated_at: " + inputPath);
            }
            std::string validationError;
            if (!validateLoadedPdpContext(context, validationError)) {
                throw std::runtime_error("Runtime PDP context validation failed for TEID 0x"
                    + [&context]() {
                        std::ostringstream oss;
                        oss << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << context.teid;
                        return oss.str();
                    }()
                    + ": " + validationError);
            }

            loadedPdpContexts.push_back(context);
        }
    }

    std::vector<UEContext> loadedUeContexts;
    if (!findJsonArrayBounds(json, "ue_contexts", arrayStart, arrayEnd)) {
        throw std::runtime_error("Runtime state file is missing ue_contexts array: " + inputPath);
    }
    {
        const auto objects = splitTopLevelJsonObjects(json.substr(arrayStart, arrayEnd - arrayStart));
        for (const auto& object : objects) {
            UEContext context;
            uint64_t number = 0;
            std::string timestamp;

            if (!extractJsonStringField(object, "imsi", context.imsi) || context.imsi.empty()) {
                throw std::runtime_error("Runtime UE context is missing imsi: " + inputPath);
            }
            extractJsonStringField(object, "guti", context.guti);
            extractJsonStringField(object, "peer_id", context.peer_id);
            extractJsonBoolField(object, "authenticated", context.authenticated);
            extractJsonBoolField(object, "auth_request_sent", context.auth_request_sent);
            extractJsonBoolField(object, "auth_response_received", context.auth_response_received);
            extractJsonBoolField(object, "security_mode_command_sent", context.security_mode_command_sent);
            extractJsonBoolField(object, "security_mode_complete", context.security_mode_complete);
            extractJsonBoolField(object, "attach_accept_sent", context.attach_accept_sent);
            extractJsonBoolField(object, "attach_complete_received", context.attach_complete_received);
            extractJsonBoolField(object, "attached", context.attached);
            extractJsonBoolField(object, "service_request_received", context.service_request_received);
            extractJsonBoolField(object, "service_accept_sent", context.service_accept_sent);
            extractJsonBoolField(object, "service_active", context.service_active);
            extractJsonBoolField(object, "service_resume_request_received", context.service_resume_request_received);
            extractJsonBoolField(object, "service_resume_accept_sent", context.service_resume_accept_sent);
            extractJsonBoolField(object, "service_release_request_received", context.service_release_request_received);
            extractJsonBoolField(object, "service_release_complete_sent", context.service_release_complete_sent);
            extractJsonBoolField(object, "detach_request_received", context.detach_request_received);
            extractJsonBoolField(object, "detach_accept_sent", context.detach_accept_sent);
            extractJsonBoolField(object, "detached", context.detached);
            extractJsonBoolField(object, "tracking_area_update_request_received", context.tracking_area_update_request_received);
            extractJsonBoolField(object, "tracking_area_update_accept_sent", context.tracking_area_update_accept_sent);
            extractJsonBoolField(object, "tracking_area_update_complete_received", context.tracking_area_update_complete_received);
            if (extractJsonUintField(object, "last_nas_message_type", number)) {
                context.last_nas_message_type = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_last_nas_message_type", context.has_last_nas_message_type);
            if (extractJsonUintField(object, "last_s1ap_procedure", number)) {
                context.last_s1ap_procedure = static_cast<uint8_t>(number);
            }
            if (extractJsonUintField(object, "security_context_id", number)) {
                context.security_context_id = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_security_context_id", context.has_security_context_id);
            if (extractJsonUintField(object, "selected_nas_algorithm", number)) {
                context.selected_nas_algorithm = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_selected_nas_algorithm", context.has_selected_nas_algorithm);
            if (extractJsonUintField(object, "default_bearer_id", number)) {
                context.default_bearer_id = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_default_bearer_id", context.has_default_bearer_id);
            if (extractJsonUintField(object, "tracking_area_code", number)) {
                context.tracking_area_code = static_cast<uint8_t>(number);
            }
            extractJsonBoolField(object, "has_tracking_area_code", context.has_tracking_area_code);
            if (extractJsonStringField(object, "updated_at", timestamp) && !parseTimestamp(timestamp, context.updated_at)) {
                throw std::runtime_error("Runtime UE context has invalid updated_at: " + inputPath);
            }
            std::string validationError;
            if (!validateLoadedUeContext(context, validationError)) {
                throw std::runtime_error("Runtime UE context validation failed for IMSI " + context.imsi + ": " + validationError);
            }

            loadedUeContexts.push_back(context);
        }
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (const auto& context : loadedPdpContexts) {
            pdpContexts[context.teid] = context;
        }
        for (const auto& context : loadedUeContexts) {
            ueContexts[context.imsi] = context;
        }
    }

    {
        std::lock_guard<std::mutex> lock(ifaceMutex);
        for (const auto& [name, adminUp] : loadedInterfaceState) {
            interfaceAdminState[name] = adminUp;
        }
    }

    std::ostringstream message;
    message << "Runtime state loaded from " << inputPath
            << " (ue_contexts=" << loadedUeContexts.size()
            << ", pdp_contexts=" << loadedPdpContexts.size() << ")";
    log("MAIN", message.str());
}

bool VNodeController::tryGetUeContext(const std::string& imsi, UEContext& context) const {
    std::lock_guard<std::mutex> lock(stateMutex);
    const auto it = ueContexts.find(imsi);
    if (it == ueContexts.end()) {
        return false;
    }

    context = it->second;
    return true;
}

bool VNodeController::sendGtpResponse(NativeSocket socketHandle,
                                      const sockaddr_in& peerAddr,
                                      const std::vector<uint8_t>& response,
                                      const std::string& peerIp,
                                      const std::string& responseLabel,
                                      uint32_t teid,
                                      uint16_t sequence) {
#ifdef _WIN32
    const int sent = sendto(socketHandle,
                            reinterpret_cast<const char*>(response.data()),
                            static_cast<int>(response.size()),
                            0,
                            reinterpret_cast<const sockaddr*>(&peerAddr),
                            sizeof(peerAddr));
    if (sent == SOCKET_ERROR) {
        log("GTP", "Failed to send " + responseLabel + " to " + peerIp + ": socket error " + std::to_string(WSAGetLastError()));
        return false;
    }
#else
    const int sent = static_cast<int>(sendto(socketHandle,
                                             response.data(),
                                             response.size(),
                                             0,
                                             reinterpret_cast<const sockaddr*>(&peerAddr),
                                             sizeof(peerAddr)));
    if (sent < 0) {
        log("GTP", "Failed to send " + responseLabel + " to " + peerIp + ": " + getSocketErrorText());
        return false;
    }
#endif

    std::ostringstream sendLog;
    sendLog << "Sent " << responseLabel << " to " << peerIp
            << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << teid
            << std::dec << std::setfill(' ')
            << ", sequence=" << sequence
            << ", bytes=" << sent;
    log("GTP", sendLog.str());
    return true;
}

bool VNodeController::handleRealGtpMessage(NativeSocket socketHandle,
                                           const sockaddr_in& peerAddr,
                                           const std::string& peerIp,
                                           const std::vector<uint8_t>& packet,
                                           const vepc::GtpV1Header& header) {
    if (header.messageType == 0x01) {
        const std::vector<uint8_t> response = vepc::buildEchoResponse(header);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Echo Response", header.teid, header.sequence);
    }

    if (header.messageType == 0x10) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseCreatePdpContextRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Create PDP request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        context.teid = allocateCreatePdpTeid(header);
        context.sequence = header.sequence;
        context.last_message_type = header.messageType;
        context.peer_ip = peerIp;
        context.updated_at = std::time(nullptr);
        context.imsi = request.imsi;
        context.apn = request.apn;
        context.ggsn_ip = request.ggsnIp;
        context.pdp_type = request.pdpType;
        context.has_pdp_type = request.hasPdpType;
        upsertPdpContext(context);

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Create PDP request parsed from " << peerIp
                         << ": imsi=" << (request.hasImsi ? request.imsi : "n/a")
                         << ", apn=" << (request.hasApn ? request.apn : "n/a")
                         << ", pdp_type=" << (request.hasPdpType ? formatPdpTypeValue(context) : "n/a")
                         << ", ggsn_ip=" << (request.hasGgsnIp ? request.ggsnIp : "n/a")
                         << ", assigned_teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << context.teid
                         << std::dec << std::setfill(' ');
        log("GTP", parsedRequestLog.str());

        const std::vector<uint8_t> response = vepc::buildCreatePdpContextResponse(header, context.teid);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Create PDP response", context.teid, header.sequence);
    }

    if (header.messageType == 0x12) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseUpdatePdpContextRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Update PDP request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Update PDP request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", updated=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildUpdatePdpContextResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Update PDP response", header.teid, header.sequence);
    }

    if (header.messageType == 0x16) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseInitiatePdpContextActivationRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Initiate PDP Context Activation request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Initiate PDP Context Activation request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", activated=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildInitiatePdpContextActivationResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Initiate PDP Context Activation response", header.teid, header.sequence);
    }

    if (header.messageType == 0x1B) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parsePduNotificationRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected PDU Notification request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "PDU Notification request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", notified=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildPduNotificationResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "PDU Notification response", header.teid, header.sequence);
    }

    if (header.messageType == 0x1D) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parsePduNotificationRejectRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected PDU Notification Reject request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "PDU Notification Reject request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", rejected=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildPduNotificationRejectResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "PDU Notification Reject response", header.teid, header.sequence);
    }

    if (header.messageType == 0x22) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseFailureReportRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Failure Report request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Failure Report request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", reported=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildFailureReportResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Failure Report response", header.teid, header.sequence);
    }

    if (header.messageType == 0x24) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseNoteMsGprsPresentRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Note MS GPRS Present request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Note MS GPRS Present request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", present=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildNoteMsGprsPresentResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Note MS GPRS Present response", header.teid, header.sequence);
    }

    if (header.messageType == 0x30) {
        vepc::CreatePdpRequestInfo request;
        std::string requestError;
        if (!vepc::parseIdentificationRequest(packet, header, request, requestError)) {
            log("GTP", "Rejected Identification request from " + peerIp + ": " + requestError);
            return true;
        }

        PDPContext context;
        const bool found = tryGetPdpContext(header.teid, context);
        if (found) {
            context.sequence = header.sequence;
            context.last_message_type = header.messageType;
            context.peer_ip = peerIp;
            context.updated_at = std::time(nullptr);
            if (request.hasImsi) {
                context.imsi = request.imsi;
            }
            if (request.hasApn) {
                context.apn = request.apn;
            }
            if (request.hasGgsnIp) {
                context.ggsn_ip = request.ggsnIp;
            }
            if (request.hasPdpType) {
                context.pdp_type = request.pdpType;
                context.has_pdp_type = true;
            }
            upsertPdpContext(context);
        }

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Identification request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", identified=" << (found ? "yes" : "no");
        if (request.hasApn) {
            parsedRequestLog << ", apn=" << request.apn;
        }
        if (request.hasPdpType) {
            PDPContext formatted;
            formatted.pdp_type = request.pdpType;
            formatted.has_pdp_type = true;
            parsedRequestLog << ", pdp_type=" << formatPdpTypeValue(formatted);
        }
        if (request.hasGgsnIp) {
            parsedRequestLog << ", ggsn_ip=" << request.ggsnIp;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = found ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildIdentificationResponse(header, header.teid, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Identification response", header.teid, header.sequence);
    }

    if (header.messageType == 0x14) {
        PDPContext removedContext;
        const bool removed = removePdpContext(header.teid, &removedContext);

        std::ostringstream parsedRequestLog;
        parsedRequestLog << "Delete PDP request parsed from " << peerIp
                         << ": teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
                         << std::dec << std::setfill(' ')
                         << ", removed=" << (removed ? "yes" : "no");
        if (removed && !removedContext.imsi.empty()) {
            parsedRequestLog << ", imsi=" << removedContext.imsi;
        }
        log("GTP", parsedRequestLog.str());

        const uint8_t cause = removed ? 0x80 : 0xC0;
        const std::vector<uint8_t> response = vepc::buildDeletePdpContextResponse(header, cause);
        return sendGtpResponse(socketHandle, peerAddr, response, peerIp, "Delete PDP response", header.teid, header.sequence);
    }

    return false;
}

bool VNodeController::handleDemoS1apMessage(NativeSocket clientSocket,
                                            const std::string& peerId,
                                            const std::vector<uint8_t>& packet) {
    vepc::DemoInitialUeMessage message;
    std::string parseError;
    if (!vepc::parseDemoInitialUeMessage(packet, message, parseError)) {
        log("S1AP", "Rejected demo Initial UE Message from " + peerId + ": " + parseError);
        return false;
    }

    UEContext context;
    tryGetUeContext(message.imsi, context);
    context.imsi = message.imsi;
    context.guti = message.hasGuti ? message.guti : context.guti;
    context.peer_id = peerId;
    context.last_s1ap_procedure = message.procedureCode;
    context.last_nas_message_type = message.nasMessageType;
    context.has_last_nas_message_type = true;
    context.updated_at = std::time(nullptr);

    std::ostringstream parsedMessageLog;
    parsedMessageLog << "Parsed demo Initial UE Message from " << peerId
                     << ": imsi=" << message.imsi
                     << ", guti=" << (message.hasGuti ? message.guti : "n/a")
                     << ", nas=" << vepc::formatNasMessageType(message.nasMessageType)
                     << ", auth_flow=" << formatAuthFlowState(context);
    log("S1AP", parsedMessageLog.str());

    if (message.nasMessageType == 0x50) {
        vepc::DemoNasServiceResumeRequest requestInfo;
        if (!vepc::parseNasServiceResumeRequest(message.nasPdu, requestInfo, parseError)) {
            log("S1AP", "Rejected demo Service Resume Request from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || context.service_active || !context.tracking_area_update_complete_received || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Service Resume Request from " + peerId + ": UE is not in tau-updated idle state for IMSI " + message.imsi);
            return false;
        }
        if (!requestInfo.hasKeySetIdentifier || requestInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Service Resume Request from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(requestInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        const std::vector<uint8_t> serviceResumeAcceptNas = vepc::buildNasServiceResumeAccept(context.security_context_id, 0x05);
        vepc::DemoNasServiceResumeAccept acceptInfo;
        if (!vepc::parseNasServiceResumeAccept(serviceResumeAcceptNas, acceptInfo, parseError)) {
            log("S1AP", "Failed to build demo Service Resume Accept for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.service_request_received = false;
        context.service_accept_sent = false;
        context.service_active = true;
        context.service_resume_request_received = true;
        context.service_resume_accept_sent = true;
        context.service_release_request_received = false;
        context.service_release_complete_sent = false;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;
        context.default_bearer_id = acceptInfo.bearerId;
        context.has_default_bearer_id = acceptInfo.hasBearerId;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, serviceResumeAcceptNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Service Resume Accept to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(serviceResumeAcceptNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context)
                    << ", bearer_id=" << formatBearerIdValue(context)
                    << ", tracking_area=" << formatTrackingAreaCodeValue(context);
        log("S1AP", responseLog.str());
        return true;
    }

    if (message.nasMessageType == 0x4A) {
        vepc::DemoNasTrackingAreaUpdateComplete completeInfo;
        if (!vepc::parseNasTrackingAreaUpdateComplete(message.nasPdu, completeInfo, parseError)) {
            log("S1AP", "Rejected demo Tracking Area Update Complete from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || !context.tracking_area_update_accept_sent || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Tracking Area Update Complete from " + peerId + ": no pending TAU accept for IMSI " + message.imsi);
            return false;
        }
        if (!completeInfo.hasKeySetIdentifier || completeInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Tracking Area Update Complete from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(completeInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        context.tracking_area_update_complete_received = true;
        upsertUeContext(context);
        log("S1AP", "Tracking Area Update Complete accepted for IMSI " + message.imsi);
        return true;
    }

    if (message.nasMessageType == 0x48) {
        vepc::DemoNasTrackingAreaUpdateRequest requestInfo;
        if (!vepc::parseNasTrackingAreaUpdateRequest(message.nasPdu, requestInfo, parseError)) {
            log("S1AP", "Rejected demo Tracking Area Update Request from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || context.service_active || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Tracking Area Update Request from " + peerId + ": UE is not in attached-idle for IMSI " + message.imsi);
            return false;
        }
        if (!requestInfo.hasKeySetIdentifier || requestInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Tracking Area Update Request from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(requestInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        const std::vector<uint8_t> tauAcceptNas = vepc::buildNasTrackingAreaUpdateAccept(context.security_context_id,
                                                                                           requestInfo.trackingAreaCode);
        vepc::DemoNasTrackingAreaUpdateAccept acceptInfo;
        if (!vepc::parseNasTrackingAreaUpdateAccept(tauAcceptNas, acceptInfo, parseError)) {
            log("S1AP", "Failed to build demo Tracking Area Update Accept for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.tracking_area_update_request_received = true;
        context.tracking_area_update_accept_sent = true;
        context.tracking_area_update_complete_received = false;
        context.service_resume_request_received = false;
        context.service_resume_accept_sent = false;
        context.tracking_area_code = acceptInfo.trackingAreaCode;
        context.has_tracking_area_code = acceptInfo.hasTrackingAreaCode;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, tauAcceptNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Tracking Area Update Accept to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(tauAcceptNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context)
                    << ", tracking_area=" << formatTrackingAreaCodeValue(context);
        log("S1AP", responseLog.str());
        return true;
    }

    if (message.nasMessageType == 0x45) {
        vepc::DemoNasDetachRequest requestInfo;
        if (!vepc::parseNasDetachRequest(message.nasPdu, requestInfo, parseError)) {
            log("S1AP", "Rejected demo Detach Request from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Detach Request from " + peerId + ": UE is not attached for IMSI " + message.imsi);
            return false;
        }
        if (!requestInfo.hasKeySetIdentifier || requestInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Detach Request from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(requestInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        const std::vector<uint8_t> detachAcceptNas = vepc::buildNasDetachAccept(context.security_context_id, 0x00);
        vepc::DemoNasDetachAccept acceptInfo;
        if (!vepc::parseNasDetachAccept(detachAcceptNas, acceptInfo, parseError)) {
            log("S1AP", "Failed to build demo Detach Accept for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.detach_request_received = true;
        context.detach_accept_sent = true;
        context.detached = true;
        context.authenticated = false;
        context.auth_request_sent = false;
        context.auth_response_received = false;
        context.security_mode_command_sent = false;
        context.security_mode_complete = false;
        context.attach_accept_sent = false;
        context.attach_complete_received = false;
        context.attached = false;
        context.service_request_received = false;
        context.service_accept_sent = false;
        context.service_active = false;
        context.service_resume_request_received = false;
        context.service_resume_accept_sent = false;
        context.service_release_request_received = false;
        context.service_release_complete_sent = false;
        context.has_security_context_id = false;
        context.security_context_id = 0;
        context.has_selected_nas_algorithm = false;
        context.selected_nas_algorithm = 0;
        context.has_default_bearer_id = false;
        context.default_bearer_id = 0;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;
        context.has_tracking_area_code = false;
        context.tracking_area_code = 0;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, detachAcceptNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Detach Accept to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(detachAcceptNas.front())
                    << ", bytes=" << response.size();
        log("S1AP", responseLog.str());
        return true;
    }

    if (message.nasMessageType == 0x4E) {
        vepc::DemoNasServiceReleaseRequest requestInfo;
        if (!vepc::parseNasServiceReleaseRequest(message.nasPdu, requestInfo, parseError)) {
            log("S1AP", "Rejected demo Service Release Request from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || !context.service_active || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Service Release Request from " + peerId + ": UE has no active service for IMSI " + message.imsi);
            return false;
        }
        if (!requestInfo.hasKeySetIdentifier || requestInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Service Release Request from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(requestInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        const std::vector<uint8_t> releaseCompleteNas = vepc::buildNasServiceReleaseComplete(context.security_context_id, 0x00);
        vepc::DemoNasServiceReleaseComplete completeInfo;
        if (!vepc::parseNasServiceReleaseComplete(releaseCompleteNas, completeInfo, parseError)) {
            log("S1AP", "Failed to build demo Service Release Complete for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.service_release_request_received = true;
        context.service_release_complete_sent = true;
        context.service_request_received = false;
        context.service_accept_sent = false;
        context.service_active = false;
        context.service_resume_request_received = false;
        context.service_resume_accept_sent = false;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, releaseCompleteNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Service Release Complete to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(releaseCompleteNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context)
                    << ", bearer_id=" << formatBearerIdValue(context);
        log("S1AP", responseLog.str());
        return true;
    }

    if (message.nasMessageType == 0x4C) {
        vepc::DemoNasServiceRequest requestInfo;
        if (!vepc::parseNasServiceRequest(message.nasPdu, requestInfo, parseError)) {
            log("S1AP", "Rejected demo Service Request from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attached || !context.security_mode_complete || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Service Request from " + peerId + ": UE is not attached for IMSI " + message.imsi);
            return false;
        }
        if (!requestInfo.hasKeySetIdentifier || requestInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Service Request from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(requestInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        const std::vector<uint8_t> serviceAcceptNas = vepc::buildNasServiceAccept(context.security_context_id, 0x05);
        vepc::DemoNasServiceAccept acceptInfo;
        if (!vepc::parseNasServiceAccept(serviceAcceptNas, acceptInfo, parseError)) {
            log("S1AP", "Failed to build demo Service Accept for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.service_request_received = true;
        context.service_accept_sent = true;
        context.service_active = true;
        context.service_resume_request_received = false;
        context.service_resume_accept_sent = false;
        context.service_release_request_received = false;
        context.service_release_complete_sent = false;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;
        context.default_bearer_id = acceptInfo.bearerId;
        context.has_default_bearer_id = acceptInfo.hasBearerId;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, serviceAcceptNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Service Accept to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(serviceAcceptNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context)
                    << ", bearer_id=" << formatBearerIdValue(context);
        log("S1AP", responseLog.str());
        return true;
    }

    if (message.nasMessageType == 0x43) {
        vepc::DemoNasAttachComplete completeInfo;
        if (!vepc::parseNasAttachComplete(message.nasPdu, completeInfo, parseError)) {
            log("S1AP", "Rejected demo Attach Complete from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.attach_accept_sent || !context.security_mode_complete || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Attach Complete from " + peerId + ": no pending attach accept for IMSI " + message.imsi);
            return false;
        }
        if (!completeInfo.hasKeySetIdentifier || completeInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Attach Complete from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(completeInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        context.attach_complete_received = true;
        context.attached = true;
        context.service_request_received = false;
        context.service_accept_sent = false;
        context.service_active = false;
        context.service_resume_request_received = false;
        context.service_resume_accept_sent = false;
        context.service_release_request_received = false;
        context.service_release_complete_sent = false;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;
        context.has_default_bearer_id = false;
        context.default_bearer_id = 0;
        upsertUeContext(context);
        log("S1AP", "Attach Complete accepted for IMSI " + message.imsi);
        return true;
    }

    if (message.nasMessageType == 0x5E) {
        vepc::DemoNasSecurityModeComplete completeInfo;
        if (!vepc::parseNasSecurityModeComplete(message.nasPdu, completeInfo, parseError)) {
            log("S1AP", "Rejected demo Security Mode Complete from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.security_mode_command_sent || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Security Mode Complete from " + peerId + ": no pending security mode command for IMSI " + message.imsi);
            return false;
        }
        if (!completeInfo.hasKeySetIdentifier || completeInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Security Mode Complete from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(completeInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        context.security_mode_complete = true;
        const std::vector<uint8_t> attachAcceptNas = vepc::buildNasAttachAccept(context.security_context_id, 0x01);
        vepc::DemoNasAttachAccept attachAcceptInfo;
        if (!vepc::parseNasAttachAccept(attachAcceptNas, attachAcceptInfo, parseError)) {
            log("S1AP", "Failed to build demo Attach Accept for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.attach_accept_sent = true;
        context.attach_complete_received = false;
        context.attached = false;
        context.service_request_received = false;
        context.service_accept_sent = false;
        context.service_active = false;
        context.service_release_request_received = false;
        context.service_release_complete_sent = false;
        context.detach_request_received = false;
        context.detach_accept_sent = false;
        context.detached = false;
        context.tracking_area_update_request_received = false;
        context.tracking_area_update_accept_sent = false;
        context.tracking_area_update_complete_received = false;
        context.has_default_bearer_id = false;
        context.default_bearer_id = 0;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, attachAcceptNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Attach Accept to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(attachAcceptNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context);
        log("S1AP", responseLog.str());
        log("S1AP", "Security Mode Complete accepted for IMSI " + message.imsi);
        return true;
    }

    if (message.nasMessageType == 0x53) {
        vepc::DemoNasAuthenticationResponse responseInfo;
        if (!vepc::parseNasAuthenticationResponse(message.nasPdu, responseInfo, parseError)) {
            log("S1AP", "Rejected demo Authentication Response from " + peerId + ": " + parseError);
            return false;
        }
        if (!context.auth_request_sent || !context.has_security_context_id) {
            log("S1AP", "Rejected demo Authentication Response from " + peerId + ": no pending authentication request for IMSI " + message.imsi);
            return false;
        }
        if (!responseInfo.hasKeySetIdentifier || responseInfo.keySetIdentifier != context.security_context_id) {
            std::ostringstream mismatch;
            mismatch << "Rejected demo Authentication Response from " << peerId
                     << ": security context mismatch for IMSI " << message.imsi
                     << " (expected=" << formatSecurityContextIdValue(context)
                     << ", got=0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<int>(responseInfo.keySetIdentifier) << std::dec << std::setfill(' ') << ")";
            log("S1AP", mismatch.str());
            return false;
        }

        context.auth_response_received = true;
        context.authenticated = true;
        const std::vector<uint8_t> securityModeNas = vepc::buildNasSecurityModeCommand(context.security_context_id, 0x01);
        vepc::DemoNasSecurityModeCommand securityModeInfo;
        if (!vepc::parseNasSecurityModeCommand(securityModeNas, securityModeInfo, parseError)) {
            log("S1AP", "Failed to build demo Security Mode Command for IMSI " + message.imsi + ": " + parseError);
            return false;
        }

        context.selected_nas_algorithm = securityModeInfo.selectedAlgorithm;
        context.has_selected_nas_algorithm = securityModeInfo.hasSelectedAlgorithm;
        context.security_mode_command_sent = true;
        context.security_mode_complete = false;

        const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, context.guti, securityModeNas);
        const int sent = send(clientSocket,
#ifdef _WIN32
                              reinterpret_cast<const char*>(response.data()),
#else
                              response.data(),
#endif
                              static_cast<int>(response.size()),
                              0);
        if (sent != static_cast<int>(response.size())) {
            log("S1AP", "Failed to send demo Security Mode Command to " + peerId + ": " + getSocketErrorText());
            return false;
        }

        upsertUeContext(context);

        std::ostringstream responseLog;
        responseLog << "Sent demo Downlink NAS Transport to " << peerId
                    << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                    << ", nas=" << vepc::formatNasMessageType(securityModeNas.front())
                    << ", bytes=" << response.size()
                    << ", security_context=" << formatSecurityContextIdValue(context)
                    << ", algorithm=" << formatNasAlgorithmValue(context);
        log("S1AP", responseLog.str());
        log("S1AP", "Authentication Response accepted for IMSI " + message.imsi);
        return true;
    }

    if (message.nasMessageType != 0x41) {
        context.authenticated = false;
        upsertUeContext(context);
        log("S1AP", "No authentication handler for NAS message " + vepc::formatNasMessageType(message.nasMessageType)
            + " from " + peerId + "; context updated without response");
        return true;
    }

    const std::vector<uint8_t> nasResponse = vepc::buildNasAuthenticationRequest();
    vepc::DemoNasAuthenticationRequest requestInfo;
    if (!vepc::parseNasAuthenticationRequest(nasResponse, requestInfo, parseError)) {
        log("S1AP", "Failed to build demo Authentication Request for IMSI " + message.imsi + ": " + parseError);
        return false;
    }

    context.authenticated = false;
    context.auth_request_sent = true;
    context.auth_response_received = false;
    context.security_mode_command_sent = false;
    context.security_mode_complete = false;
    context.attach_accept_sent = false;
    context.attach_complete_received = false;
    context.attached = false;
    context.service_request_received = false;
    context.service_accept_sent = false;
    context.service_active = false;
    context.service_release_request_received = false;
    context.service_release_complete_sent = false;
    context.detach_request_received = false;
    context.detach_accept_sent = false;
    context.detached = false;
    context.tracking_area_update_request_received = false;
    context.tracking_area_update_accept_sent = false;
    context.tracking_area_update_complete_received = false;
    context.security_context_id = requestInfo.keySetIdentifier;
    context.has_security_context_id = requestInfo.hasKeySetIdentifier;
    context.has_selected_nas_algorithm = false;
    context.selected_nas_algorithm = 0;
    context.has_default_bearer_id = false;
    context.default_bearer_id = 0;
    context.has_tracking_area_code = false;
    context.tracking_area_code = 0;

    const std::vector<uint8_t> response = vepc::buildDemoDownlinkNasTransport(message.imsi, message.guti, nasResponse);
    const int sent = send(clientSocket,
#ifdef _WIN32
                          reinterpret_cast<const char*>(response.data()),
#else
                          response.data(),
#endif
                          static_cast<int>(response.size()),
                          0);
    if (sent != static_cast<int>(response.size())) {
        log("S1AP", "Failed to send demo Authentication Request to " + peerId + ": " + getSocketErrorText());
        return false;
    }

    upsertUeContext(context);

    std::ostringstream responseLog;
    responseLog << "Sent demo Downlink NAS Transport to " << peerId
                << ": s1ap=" << vepc::formatS1apProcedureCode(0x0D)
                << ", nas=" << vepc::formatNasMessageType(nasResponse.front())
                << ", bytes=" << response.size()
                << ", security_context=" << formatSecurityContextIdValue(context);
    log("S1AP", responseLog.str());
    return true;
}

void VNodeController::printLogs() const {
    std::lock_guard<std::mutex> lock(logMutex);
    for (auto& entry : logs) {
        char buf[64];
        std::tm localTime{};
        if (tryGetLocalTime(entry.time, localTime)) {
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
            std::cout << "[" << buf << "] [" << entry.node << "] " << entry.msg << "\n";
        } else {
            std::cout << "[timestamp-unavailable] [" << entry.node << "] " << entry.msg << "\n";
        }
    }
}

void VNodeController::printState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    std::cout << formatStateSnapshotLocked();
}

void VNodeController::printConfig() const {
    std::cout << COLOR_BOLD << COLOR_WHITE << "Configuration:\n" << COLOR_RESET;
    std::cout << COLOR_GREEN << "TEST COLOR" << COLOR_RESET << std::endl;
    for (auto& p : config)
        std::cout << "  " << COLOR_CYAN << p.first << COLOR_RESET << " = " << COLOR_GREEN << p.second << COLOR_RESET << "\n";
}

// ----------------------------------------------------------------
//  Рабочие потоки
// ----------------------------------------------------------------

void VNodeController::mmeThreadFunc() {
    { std::lock_guard<std::mutex> l(statusMutex); mmeStatus = "Running"; }
    log("MME", "MME started");
    while (running) {
        log("MME", "Waiting for Initial UE Message...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    { std::lock_guard<std::mutex> l(statusMutex); mmeStatus = "Stopped"; }
    log("MME", "MME stopped");
}

void VNodeController::sgsnThreadFunc() {
    { std::lock_guard<std::mutex> l(statusMutex); sgsnStatus = "Running"; }
    log("SGSN", "SGSN started");
    while (running) {
        log("SGSN", "Waiting for GTP-C messages...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    { std::lock_guard<std::mutex> l(statusMutex); sgsnStatus = "Stopped"; }
    log("SGSN", "SGSN stopped");
}

void VNodeController::gtpServerThreadFunc() {
    int port = std::stoi(config.at("gtp-c-port"));
    const std::string bindIp = config.count("sgsn-ip") != 0 ? config.at("sgsn-ip") : "0.0.0.0";

    const auto entries = loadInterfaceConfigEntries();
    for (const auto& entry : entries) {
        if (entry.name == "S11" && entry.proto == "UDP" && entry.port == std::to_string(port)) {
            log("GTP", "S11 endpoint is handled by generic endpoint thread");
            return;
        }
    }

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log("GTP", "WSAStartup failed");
        return;
    }
#endif

    NativeSocket socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_NATIVE_SOCKET) {
        log("GTP", "socket() failed: " + getSocketErrorText());
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int reuseAddr = 1;
#ifdef _WIN32
    const char* reuseValue = reinterpret_cast<const char*>(&reuseAddr);
    DWORD timeoutMs = 1000;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    const void* reuseValue = &reuseAddr;
    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, reuseValue, sizeof(reuseAddr));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bindIp.c_str(), &addr.sin_addr) != 1) {
        log("GTP", "inet_pton failed for GTP-C bind address " + bindIp);
        closeNativeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        log("GTP", "bind() failed: " + getSocketErrorText());
        closeNativeSocket(socketHandle);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    log("GTP", "GTP-C server started on UDP " + bindIp + ":" + std::to_string(port));

    std::vector<uint8_t> buffer(4096);
    while (running) {
        sockaddr_in peerAddr{};
#ifdef _WIN32
        int peerAddrSize = sizeof(peerAddr);
        const int received = recvfrom(socketHandle,
                                      reinterpret_cast<char*>(buffer.data()),
                                      static_cast<int>(buffer.size()),
                                      0,
                                      reinterpret_cast<sockaddr*>(&peerAddr),
                                      &peerAddrSize);
        if (received == SOCKET_ERROR) {
            const int errorCode = WSAGetLastError();
            if (!running || restartRequested) {
                break;
            }
            if (errorCode == WSAETIMEDOUT || errorCode == WSAEWOULDBLOCK) {
                continue;
            }
            log("GTP", "recvfrom() failed: socket error " + std::to_string(errorCode));
            continue;
        }
#else
        socklen_t peerAddrSize = sizeof(peerAddr);
        const int received = static_cast<int>(recvfrom(socketHandle,
                                                       buffer.data(),
                                                       buffer.size(),
                                                       0,
                                                       reinterpret_cast<sockaddr*>(&peerAddr),
                                                       &peerAddrSize));
        if (received < 0) {
            if (!running || restartRequested) {
                break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            log("GTP", "recvfrom() failed: " + getSocketErrorText());
            continue;
        }
#endif

        if (received <= 0) {
            continue;
        }

        const std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + received);
        const std::string peerIp = socketAddressToString(peerAddr);
        vepc::GtpV1Header header;
        std::string parseError;
        if (!vepc::parseGtpV1Header(packet, header, parseError)) {
            log("GTP", "Rejected packet from " + peerIp + ": " + parseError);
            continue;
        }

        std::ostringstream oss;
        oss << "Parsed GTPv1-C header from " << peerIp
            << ": type=" << vepc::formatGtpMessageType(header.messageType)
            << ", teid=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << header.teid
            << std::dec << std::setfill(' ')
            << ", sequence=" << header.sequence
            << ", payload_length=" << header.payloadLength;
        log("GTP", oss.str());

        if (!handleRealGtpMessage(socketHandle, peerAddr, peerIp, packet, header)) {
            log("GTP", "No real handler for parsed message type " + vepc::formatGtpMessageType(header.messageType) + "; packet stays in parser-only demo path");
        }
    }

    closeNativeSocket(socketHandle);
#ifdef _WIN32
    WSACleanup();
#endif
}

void VNodeController::s1apServerThreadFunc() {
    const int port = std::stoi(config.at("s1ap-port"));
    const std::string bindIp = config.count("s1ap-bind-ip") != 0 ? config.at("s1ap-bind-ip")
                                                                   : (config.count("mme-ip") != 0 ? config.at("mme-ip") : "0.0.0.0");

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log("S1AP", "WSAStartup failed");
        return;
    }
#endif

    NativeSocket listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_NATIVE_SOCKET) {
        log("S1AP", "socket() failed: " + getSocketErrorText());
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int reuseAddr = 1;
#ifdef _WIN32
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
#else
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bindIp.c_str(), &addr.sin_addr) != 1) {
        log("S1AP", "inet_pton failed for S1AP bind address " + bindIp);
        closeNativeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        log("S1AP", "bind() failed: " + getSocketErrorText());
        closeNativeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (listen(listenSocket, SOMAXCONN) != 0) {
        log("S1AP", "listen() failed: " + getSocketErrorText());
        closeNativeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    log("S1AP", "S1AP demo server started on TCP " + bindIp + ":" + std::to_string(port) + " (SCTP placeholder path)");

    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        const int ready = select(static_cast<int>(listenSocket) + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (!running || restartRequested) {
                break;
            }
            log("S1AP", "select() failed: " + getSocketErrorText());
            continue;
        }
        if (ready == 0 || !FD_ISSET(listenSocket, &readSet)) {
            continue;
        }

        sockaddr_in peerAddr{};
#ifdef _WIN32
        int peerAddrSize = sizeof(peerAddr);
#else
        socklen_t peerAddrSize = sizeof(peerAddr);
#endif
        NativeSocket clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&peerAddr), &peerAddrSize);
        if (clientSocket == INVALID_NATIVE_SOCKET) {
            if (!running || restartRequested) {
                break;
            }
            log("S1AP", "accept() failed: " + getSocketErrorText());
            continue;
        }

        const std::string peerId = socketEndpointToString(peerAddr);
        log("S1AP", "Accepted demo S1AP client " + peerId);

#ifdef _WIN32
        DWORD timeoutMs = 2000;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
        timeval recvTimeout{};
        recvTimeout.tv_sec = 2;
        recvTimeout.tv_usec = 0;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
#endif

        std::vector<uint8_t> buffer(4096);
        const int received = recv(clientSocket,
#ifdef _WIN32
                                  reinterpret_cast<char*>(buffer.data()),
#else
                                  buffer.data(),
#endif
                                  static_cast<int>(buffer.size()),
                                  0);
        if (received > 0) {
            const std::vector<uint8_t> packet(buffer.begin(), buffer.begin() + received);
            handleDemoS1apMessage(clientSocket, peerId, packet);
        } else if (received == 0) {
            log("S1AP", "Demo S1AP client closed connection without payload: " + peerId);
        } else if (running && !restartRequested) {
            log("S1AP", "recv() failed for demo S1AP client " + peerId + ": " + getSocketErrorText());
        }

        closeNativeSocket(clientSocket);
    }

    closeNativeSocket(listenSocket);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ----------------------------------------------------------------
//  CLI сервер
// ----------------------------------------------------------------

void VNodeController::cliServerThread() {
#ifdef _WIN32
    // Windows: TCP сервер для CLI на localhost:CLI_TCP_PORT
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        WSACleanup();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(CLI_TCP_PORT);
    if (inet_pton(AF_INET, CLI_TCP_HOST, &addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed\n";
        closesocket(sockfd);
        WSACleanup();
        return;
    }

    char opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed\n";
        closesocket(sockfd);
        WSACleanup();
        return;
    }
    if (listen(sockfd, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n";
        closesocket(sockfd);
        WSACleanup();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(cliSocketMutex);
        cliListenSocket = sockfd;
    }

    log("MAIN", std::string("CLI TCP ready: ") + CLI_ENDPOINT);

    while (running) {
        SOCKET client = accept(sockfd, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!running || restartRequested) break;
            continue;
        }

        char buffer[1024]{};
        int n = recv(client, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
        if (n > 0) {
            // Убираем возможный перевод строки
            std::string cmd(buffer, n);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                cmd.pop_back();

            bool shouldStop = false;
            const std::string reply = handleCliCommand(cmd, shouldStop);

            send(client, reply.c_str(), static_cast<int>(reply.size()), 0);

            if (shouldStop) {
                closesocket(client);
                running = false;
                g_running = false;
                closeCliListenSocket();
                WSACleanup();
                return;
            }
        }
        closesocket(client);
    }

    closeCliListenSocket();
    WSACleanup();
#else
    // Linux/Unix: оригинальный вариант с UNIX domain socket
    unlink(CLI_SOCKET);

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return; }

    // Таймаут accept — чтобы поток мог завершиться при stop()
    struct timeval tv{1, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLI_SOCKET, sizeof(addr.sun_path) - 1);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sockfd); return;
    }
    if (listen(sockfd, 5) < 0) {
        perror("listen"); close(sockfd); return;
    }

    {
        std::lock_guard<std::mutex> lock(cliSocketMutex);
        cliListenSocket = sockfd;
    }

    log("MAIN", "CLI socket ready: " CLI_SOCKET);

    while (running) {
        int client = accept(sockfd, nullptr, nullptr);
        if (client < 0) {
            if (!running || restartRequested) break;
            continue;
        }

        char buffer[1024]{};
        int n = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            // Убираем возможный перевод строки
            std::string cmd(buffer, n);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                cmd.pop_back();

            bool shouldStop = false;
            const std::string reply = handleCliCommand(cmd, shouldStop);

            send(client, reply.c_str(), reply.size(), 0);

            if (shouldStop) {
                close(client);
                running = false;
                g_running = false;
                closeCliListenSocket();
                unlink(CLI_SOCKET);
                return;
            }
        }
        close(client);
    }

    closeCliListenSocket();
    unlink(CLI_SOCKET);
#endif
}

// ----------------------------------------------------------------
//  Старт / стоп
// ----------------------------------------------------------------

void VNodeController::start() {
    running = true;
    restartRequested = false;
    // Создаём папку логов если нет (кросс-платформенно)
    try {
        std::filesystem::create_directories("build/logs");
    } catch (...) {
        std::cerr << "Failed to create logs directory build/logs\n";
    }

    logFile.open(LOG_FILE, std::ios::app);
    if (!logFile.is_open())
        log("MAIN", "Failed to open log file: " LOG_FILE);

    try {
        loadRuntimeStateFromFile();
    } catch (const std::exception& ex) {
        clearRuntimeState();
        const std::string inputPath = resolveWritableConfigPath(RUNTIME_STATE_FILE);
        try {
            const std::string quarantinePath = quarantineRuntimeStateFile(inputPath);
            if (!quarantinePath.empty()) {
                log("MAIN", "Corrupt runtime state moved to " + quarantinePath);
                const std::size_t prunedCount = pruneCorruptRuntimeStateFiles(std::filesystem::path(inputPath).parent_path(),
                                                                              RUNTIME_STATE_CORRUPT_RETENTION,
                                                                              quarantinePath);
                if (prunedCount > 0) {
                    log("MAIN", "Pruned " + std::to_string(prunedCount) + " old corrupt runtime state snapshot(s)");
                }
            }
        } catch (const std::exception& quarantineEx) {
            log("MAIN", std::string("Failed to quarantine runtime state: ") + quarantineEx.what());
        }
        log("MAIN", std::string("Failed to load runtime state: ") + ex.what());
    }

    mmeThread  = std::thread(&VNodeController::mmeThreadFunc,      this);
    sgsnThread = std::thread(&VNodeController::sgsnThreadFunc,     this);
    gtpThread  = std::thread(&VNodeController::gtpServerThreadFunc,this);
    s1apThread = std::thread(&VNodeController::s1apServerThreadFunc,this);
    startConfiguredInterfaceEndpoints();
    cliThread  = std::thread(&VNodeController::cliServerThread,    this);

    log("MAIN", "vEPC started");
}

void VNodeController::stop() {
    if (!running) return;

    try {
        saveRuntimeStateToFile();
    } catch (const std::exception& ex) {
        log("MAIN", std::string("Failed to save runtime state: ") + ex.what());
    }

    running = false;
    closeCliListenSocket();

    if (mmeThread.joinable())  mmeThread.join();
    if (sgsnThread.joinable()) sgsnThread.join();
    if (gtpThread.joinable())  gtpThread.join();
    if (s1apThread.joinable()) s1apThread.join();
    for (auto& endpointThread : endpointThreads) {
        if (endpointThread.joinable()) {
            endpointThread.join();
        }
    }
    endpointThreads.clear();
    if (cliThread.joinable())  cliThread.join();

    if (logFile.is_open()) logFile.close();
    log("MAIN", "vEPC остановлен");
}

void VNodeController::restart() {
    stop();
    loadConfigFromFile();
    start();
}

// ----------------------------------------------------------------
//  main
// ----------------------------------------------------------------

static VNodeController* g_ctrl = nullptr;

void signalHandler(int) {
    g_running = false;
    if (g_ctrl) g_ctrl->stop();
    std::exit(0);
}

int main() {
#ifdef _WIN32
    configureWindowsConsoleUtf8();
#endif

    std::cout << "vEPC - Virtual EPC (SGSN + MME)\n";
    std::cout << "Starting...\n";

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    VNodeController ctrl;
    g_ctrl = &ctrl;
    ctrl.start();

    std::cout << "To stop use Ctrl+C or CLI 'stop'\n";

    while (g_running) {
        if (ctrl.consumeRestartRequest()) {
            ctrl.restart();
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ctrl.stop();
    return 0;
}
