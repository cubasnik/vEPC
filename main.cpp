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
#include <map>
#include <cstring>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include <csignal>
#include <filesystem>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// Пути относительно рабочей директории (папки проекта)
#define LOG_FILE    "build/logs/vepc.log"
#define CONFIG_FILE "config/vepc.config"

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
    uint32_t    teid;
    std::string ggsn_ip;
    uint8_t     pdp_type;
    std::string imsi;
    std::string apn;
};

struct UEContext {
    std::string imsi;
    std::string guti;
    bool        authenticated = false;
};

// Глобальный указатель для обработки сигналов
static std::atomic<bool> g_running{true};

class VNodeController {
public:
    VNodeController() : running(true) {
        config["mcc"]       = "250";
        config["mnc"]       = "20";
        config["gtp-c-port"]  = "2123";
        config["gtp-u-port"]  = "2152";
        config["s1ap-port"] = "36412";
        config["sgsn-ip"]   = "127.0.0.1";
        config["mme-ip"]    = "127.0.0.1";
        loadConfigFromFile();
    }

    ~VNodeController() { stop(); }

    void start();
    void stop();
    void restart();

    void        log(const std::string& node, const std::string& msg);
    std::string getStatus() const;
    void        printLogs()   const;
    void        printState()  const;
    void        printConfig() const;
    bool        loadConfig(const std::string& filename);

    void setValue(const std::string& key, const std::string& value) {
        config[key] = value;
        log("MAIN", "Установлено " + key + " = " + value);
        saveConfigToFile();
    }

    void cliServerThread();

private:
    void mmeThreadFunc();
    void sgsnThreadFunc();
    void gtpServerThreadFunc();
    void s1apServerThreadFunc();

    std::atomic<bool> running;
    std::thread mmeThread;
    std::thread sgsnThread;
    std::thread gtpThread;
    std::thread s1apThread;
    std::thread cliThread;

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

    void colorLog(const std::string& node, const std::string& msg);
    void saveConfigToFile() const;
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
        std::tm* tm = std::localtime(&logs.back().time);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        logFile << "[" << buf << "] [" << node << "] " << msg << "\n";
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
    std::ifstream file(filename);
    if (!file.is_open()) {
        log("MAIN", "Failed to open config: " + filename);
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key   = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };
        trim(key); trim(value);
        config[key] = value;
    }
    log("MAIN", "Конфиг загружен: " + filename);
    return true;
}

void VNodeController::saveConfigToFile() const {
    std::ofstream file(CONFIG_FILE);
    if (!file.is_open()) { std::cerr << "Failed to save config\n"; return; }
    for (const auto& [k, v] : config)
        file << k << " = " << v << "\n";
}

void VNodeController::loadConfigFromFile() {
    loadConfig(CONFIG_FILE);
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

void VNodeController::printLogs() const {
    std::lock_guard<std::mutex> lock(logMutex);
    for (auto& entry : logs) {
        std::tm* tm = std::localtime(&entry.time);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        std::cout << "[" << buf << "] [" << entry.node << "] " << entry.msg << "\n";
    }
}

void VNodeController::printState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    std::cout << "PDP contexts: " << pdpContexts.size() << "\n";
    std::cout << "UE contexts:  " << ueContexts.size()  << "\n";
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
    log("GTP", "GTP-C server started on UDP " + std::to_string(port));
    while (running)
        std::this_thread::sleep_for(std::chrono::seconds(5));
}

void VNodeController::s1apServerThreadFunc() {
    log("S1AP", "S1AP server started on SCTP " + config.at("s1ap-port"));
    while (running)
        std::this_thread::sleep_for(std::chrono::seconds(5));
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

    log("MAIN", std::string("CLI TCP ready: ") + CLI_ENDPOINT);

    while (running) {
        SOCKET client = accept(sockfd, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!running) break;
            continue;
        }

        char buffer[1024]{};
        int n = recv(client, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
        if (n > 0) {
            // Убираем возможный перевод строки
            std::string cmd(buffer, n);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                cmd.pop_back();

            std::string reply;

            if (cmd == "status") {
                reply = getStatus();
            }
            else if (cmd == "logs") {
                std::lock_guard<std::mutex> lock(logMutex);
                std::ostringstream oss;
                // Последние 20 записей
                size_t start = logs.size() > 20 ? logs.size() - 20 : 0;
                for (size_t i = start; i < logs.size(); ++i) {
                    std::tm* tm = std::localtime(&logs[i].time);
                    char buf[64];
                    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
                    oss << "[" << buf << "][" << logs[i].node << "] " << logs[i].msg << "\n";
                }
                reply = oss.str();
            }
            else if (cmd == "state") {
                std::lock_guard<std::mutex> lock(stateMutex);
                std::ostringstream oss;
                oss << "PDP contexts: " << pdpContexts.size() << "\n";
                oss << "UE contexts:  " << ueContexts.size()  << "\n";
                reply = oss.str();
            }
            else if (cmd == "show") {
                std::ostringstream oss;
                for (auto& p : config)
                    oss << COLOR_CYAN << p.first << COLOR_RESET << " = " << COLOR_GREEN << p.second << COLOR_RESET << "\n";
                reply = oss.str();
            }
            else if (cmd == "stop") {
                reply = "vEPC is stopping...\n";
                send(client, reply.c_str(), static_cast<int>(reply.size()), 0);
                closesocket(client);
                running     = false;
                g_running   = false;
                closesocket(sockfd);
                WSACleanup();
                return;
            }
            else if (cmd.rfind("iface_status ", 0) == 0) {
                std::string name = cmd.substr(13);
                // Look up interface in interfaces.conf
                std::ifstream f("config/interfaces.conf");
                if (!f.is_open()) {
                    f.open("../config/interfaces.conf");
                }
                bool found = false;
                std::ostringstream oss;
                if (f.is_open()) {
                    std::string line;
                    while (std::getline(f, line)) {
                        // trim
                        size_t b = line.find_first_not_of(" \t");
                        if (b == std::string::npos || line[b] == '#') continue;
                        std::istringstream ss(line);
                        std::string n, proto, addrStr, peer;
                        std::getline(ss, n,     '|');
                        std::getline(ss, proto, '|');
                        std::getline(ss, addrStr,'|');
                        std::getline(ss, peer,  '|');
                        auto tr = [](std::string& s){
                            size_t b2=s.find_first_not_of(" \t\r\n");
                            size_t e2=s.find_last_not_of(" \t\r\n");
                            s=(b2==std::string::npos)?"":s.substr(b2,e2-b2+1);
                        };
                        tr(n); tr(proto); tr(addrStr); tr(peer);
                        if (n == name) {
                            oss << "Interface " << n << " status:\n"
                                << "  Protocol : " << proto   << "\n"
                                << "  Address  : " << addrStr << "\n"
                                << "  Peer     : " << peer    << "\n"
                                << "  State    : UP (simulated)\n";
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                    oss << "Interface '" << name << "' not found in interfaces.conf\n";
                reply = oss.str();
            }
            else if (cmd.rfind("iface_up ",   0) == 0 ||
                     cmd.rfind("iface_down ",  0) == 0 ||
                     cmd.rfind("iface_reset ", 0) == 0) {
                size_t sp = cmd.find(' ');
                std::string action = cmd.substr(6, sp - 6); // up/down/reset
                std::string name   = cmd.substr(sp + 1);
                reply = "Interface " + name + ": action '" + action + "' executed (simulated)\n";
            }
            else {
                reply = "Unknown command: " + cmd + "\n"
                        "Available: status, logs, state, show, stop\n";
            }

            send(client, reply.c_str(), static_cast<int>(reply.size()), 0);
        }
        closesocket(client);
    }

    closesocket(sockfd);
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

    log("MAIN", "CLI socket ready: " CLI_SOCKET);

    while (running) {
        int client = accept(sockfd, nullptr, nullptr);
        if (client < 0) continue;   // таймаут — проверяем running

        char buffer[1024]{};
        int n = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            // Убираем возможный перевод строки
            std::string cmd(buffer, n);
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
                cmd.pop_back();

            std::string reply;

            if (cmd == "status") {
                reply = getStatus();
            }
            else if (cmd == "logs") {
                std::lock_guard<std::mutex> lock(logMutex);
                std::ostringstream oss;
                // Последние 20 записей
                size_t start = logs.size() > 20 ? logs.size() - 20 : 0;
                for (size_t i = start; i < logs.size(); ++i) {
                    std::tm* tm = std::localtime(&logs[i].time);
                    char buf[64];
                    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
                    oss << "[" << buf << "][" << logs[i].node << "] " << logs[i].msg << "\n";
                }
                reply = oss.str();
            }
            else if (cmd == "state") {
                std::lock_guard<std::mutex> lock(stateMutex);
                std::ostringstream oss;
                oss << "PDP contexts: " << pdpContexts.size() << "\n";
                oss << "UE contexts:  " << ueContexts.size()  << "\n";
                reply = oss.str();
            }
            else if (cmd == "show") {
                std::ostringstream oss;
                for (auto& p : config)
                    oss << p.first << " = " << p.second << "\n";
                reply = oss.str();
            }
            else if (cmd == "stop") {
                reply = "vEPC is stopping...\n";
                send(client, reply.c_str(), reply.size(), 0);
                close(client);
                running     = false;
                g_running   = false;
                close(sockfd);
                unlink(CLI_SOCKET);
                return;
            }
			            else if (cmd.rfind("iface_status ", 0) == 0) {
                std::string name = cmd.substr(13);
                // Look up interface in interfaces.conf
                std::ifstream f("config/interfaces.conf");
                if (!f.is_open()) {
                    f.open("../config/interfaces.conf");
                }
                bool found = false;
                std::ostringstream oss;
                if (f.is_open()) {
                    std::string line;
                    while (std::getline(f, line)) {
                        // trim
                        size_t b = line.find_first_not_of(" \t");
                        if (b == std::string::npos || line[b] == '#') continue;
                        std::istringstream ss(line);
                        std::string n, proto, addr, peer;
                        std::getline(ss, n,     '|');
                        std::getline(ss, proto, '|');
                        std::getline(ss, addr,  '|');
                        std::getline(ss, peer,  '|');
                        auto tr = [](std::string& s){
                            size_t b2=s.find_first_not_of(" \t\r\n");
                            size_t e2=s.find_last_not_of(" \t\r\n");
                            s=(b2==std::string::npos)?"":s.substr(b2,e2-b2+1);
                        };
                        tr(n); tr(proto); tr(addr); tr(peer);
                        if (n == name) {
                            oss << "Interface " << n << " status:\n"
                                << "  Protocol : " << proto << "\n"
                                << "  Address  : " << addr  << "\n"
                                << "  Peer     : " << peer  << "\n"
                                << "  State    : UP (simulated)\n";
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                    oss << "Interface '" << name << "' not found in interfaces.conf\n";
                reply = oss.str();
            }
            else if (cmd.rfind("iface_up ",   0) == 0 ||
                     cmd.rfind("iface_down ",  0) == 0 ||
                     cmd.rfind("iface_reset ", 0) == 0) {
                size_t sp = cmd.find(' ');
                std::string action = cmd.substr(6, sp - 6); // up/down/reset
                std::string name   = cmd.substr(sp + 1);
                reply = "Interface " + name + ": action '" + action + "' executed (simulated)\n";
            }
			
            else {
                reply = "Unknown command: " + cmd + "\n"
                        "Available: status, logs, state, show, stop\n";
            }

            send(client, reply.c_str(), reply.size(), 0);
        }
        close(client);
    }

    close(sockfd);
    unlink(CLI_SOCKET);
#endif
}

// ----------------------------------------------------------------
//  Старт / стоп
// ----------------------------------------------------------------

void VNodeController::start() {
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
    cliThread  = std::thread(&VNodeController::cliServerThread,    this);

    log("MAIN", "vEPC started");
}

void VNodeController::stop() {
    if (!running) return;
    running = false;

    if (mmeThread.joinable())  mmeThread.join();
    if (sgsnThread.joinable()) sgsnThread.join();
    if (gtpThread.joinable())  gtpThread.join();
    if (s1apThread.joinable()) s1apThread.join();
    if (cliThread.joinable())  cliThread.join();

    if (logFile.is_open()) logFile.close();
    log("MAIN", "vEPC остановлен");
}

void VNodeController::restart() {
    stop();
    running = true;
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
    std::cout << "vEPC - Virtual EPC (SGSN + MME)\n";
    std::cout << "Starting...\n";

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    VNodeController ctrl;
    g_ctrl = &ctrl;
    ctrl.start();

    std::cout << "To stop use Ctrl+C or CLI 'stop'\n";

    while (g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ctrl.stop();
    return 0;
}
