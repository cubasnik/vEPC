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
    uint8_t     last_nas_message_type = 0;
    bool        has_last_nas_message_type = false;
    uint8_t     last_s1ap_procedure = 0;
    uint8_t     security_context_id = 0;
    bool        has_security_context_id = false;
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
    return entry.proto == "UDP"
        && gtpUserPortIt != config.end()
        && entry.port == gtpUserPortIt->second;
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
    void upsertUeContext(const UEContext& context);
    bool tryGetUeContext(const std::string& imsi, UEContext& context) const;

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
    InterfaceEndpointRuntime runtimeState;
    runtimeState.handlerImplemented = true;

    if (!isLocalInterfaceAddress(entry.ip, config)) {
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
    if (inet_pton(AF_INET, entry.ip.c_str(), &addr.sin_addr) != 1) {
        runtimeState.reason = "inet_pton failed for address " + entry.ip;
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
    log("MAIN", "Interface endpoint " + entry.name + " ready on " + entry.address + " (" + entry.proto + ")");

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
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

    diagnostics.localBind = isLocalInterfaceAddress(entry.ip, config);

    const auto s1apPortIt = config.find("s1ap-port");
    const auto gtpControlPortIt = config.find("gtp-c-port");
    const auto gtpUserPortIt = config.find("gtp-u-port");
    const bool connectionOriented = isConnectionOrientedProtocol(entry);
    const bool isS1ap = s1apPortIt != config.end() && entry.proto == "SCTP" && entry.port == s1apPortIt->second;
    const bool isGtpControl = gtpControlPortIt != config.end() && entry.proto == "UDP" && entry.port == gtpControlPortIt->second;
    const bool isGtpUser = gtpUserPortIt != config.end() && entry.proto == "UDP" && entry.port == gtpUserPortIt->second;
    const bool isGenericProtocol = isGenericEndpointProtocol(entry, config);
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

    const InterfaceDiagnostics diagnostics = buildInterfaceDiagnostics(*entryIt);

    std::ostringstream oss;
    oss << "Interface " << entryIt->name << " status:\n"
        << "  Protocol      : " << entryIt->proto << "\n"
        << "  Address       : " << entryIt->address << "\n"
        << "  Peer          : " << entryIt->peer << "\n"
        << "  Admin State   : " << (diagnostics.adminUp ? "UP" : "DOWN") << "\n"
        << "  Oper State    : " << diagnostics.operState << "\n"
        << "  Implementation: " << diagnostics.implementation << "\n"
        << "  Local Bind    : " << (diagnostics.localBind ? "YES" : "NO") << "\n"
        << "  Bind State    : " << diagnostics.bindState << "\n"
        << "  Bind Reason   : " << diagnostics.bindReason << "\n"
        << "  Listen State  : " << diagnostics.listenState << "\n"
        << "  Listen Reason : " << diagnostics.listenReason << "\n"
        << "  Reason        : " << diagnostics.reason << "\n";
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
            oss << "  Auth Flow: " << formatAuthFlowState(context) << "\n";
            oss << "  Auth Request Sent: " << (context.auth_request_sent ? "yes" : "no") << "\n";
            oss << "  Auth Response Received: " << (context.auth_response_received ? "yes" : "no") << "\n";
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

void VNodeController::upsertUeContext(const UEContext& context) {
    std::lock_guard<std::mutex> lock(stateMutex);
    ueContexts[context.imsi] = context;
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
        upsertUeContext(context);
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
    context.security_context_id = requestInfo.keySetIdentifier;
    context.has_security_context_id = requestInfo.hasKeySetIdentifier;

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
