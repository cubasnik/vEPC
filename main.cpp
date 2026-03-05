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
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/sctp.h>

#define LOG_FILE "logs/vepc.log"

// Цвета ANSI
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

struct LogEntry {
    std::time_t time;
    std::string node;
    std::string msg;
};

class VNodeController {
public:
    VNodeController() : running(true) {
        // Значения по умолчанию
        config["mcc"] = "250";
        config["mnc"] = "20";
        config["gtp-c-port"] = "2123";
        config["s1ap-port"] = "36412";
        config["sgsn-ip"] = "127.0.0.1";
        config["mme-ip"] = "127.0.0.1";
    }
    ~VNodeController() { stop(); }

    void start();
    void stop();
    void restart();

    void log(const std::string& node, const std::string& msg);

    std::string getStatus() const;
    void printLogs() const;
    void printState() const;
    void printConfig() const;           // вывод всех параметров (const)

    bool loadConfig(const std::string& filename);

    void setValue(const std::string& key, const std::string& value);

private:
    void mmeThreadFunc();
    void sgsnThreadFunc();
    void consoleThreadFunc();
    void gtpServerThreadFunc();
    void s1apServerThreadFunc();

    std::atomic<bool> running;
    std::thread mmeThread;
    std::thread sgsnThread;
    std::thread consoleThread;
    std::thread gtpThread;
    std::thread s1apThread;

    mutable std::mutex logMutex;
    std::vector<LogEntry> logs;
    std::ofstream logFile;

    std::string mmeStatus{"Stopped"};
    std::string sgsnStatus{"Stopped"};
    mutable std::mutex statusMutex;

    std::map<std::string, std::string> config;  // все параметры

    // Состояние (заглушки)
    std::map<uint32_t, std::string> pdpContexts;  // TEID → IMSI
    std::map<std::string, std::string> ueContexts; // IMSI → GUTI
    mutable std::mutex stateMutex;

    void colorLog(const std::string& node, const std::string& msg);
};

bool VNodeController::loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        log("MAIN", "Ошибка открытия конфига: " + filename);
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        config[key] = value;
    }

    log("MAIN", "Конфиг загружен: " + filename);
    return true;
}

void VNodeController::setValue(const std::string& key, const std::string& value) {
    config[key] = value;
    log("MAIN", "Установлено " + key + " = " + value);
}

void VNodeController::printConfig() const {
    std::cout << COLOR_BOLD << COLOR_WHITE << "[MAIN] Текущие значения конфигурации:" << COLOR_RESET << std::endl;
    for (const auto& [key, val] : config) {
        std::cout << COLOR_WHITE << key << " = " << val << COLOR_RESET << std::endl;
    }
}

void VNodeController::colorLog(const std::string& node, const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    logs.push_back({std::time(nullptr), node, msg});

    std::string color = COLOR_WHITE;
    std::string style = "";

    if (node == "MME") {
        color = COLOR_GREEN;
        style = COLOR_BOLD;
    } else if (node == "SGSN") {
        color = COLOR_BLUE;
    } else if (node == "GTP") {
        color = COLOR_YELLOW;
    } else if (node == "S1AP") {
        color = COLOR_RED;
    } else if (node == "MAIN") {
        color = COLOR_WHITE;
        style = COLOR_BOLD;
    }

    std::cout << style << color << "[" << node << "] " << msg << COLOR_RESET << std::endl;

    if (logFile.is_open()) {
        std::tm* tm = std::localtime(&logs.back().time);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        logFile << "[" << buf << "] [" << node << "] " << msg << std::endl;
        logFile.flush();
    }
}

void VNodeController::log(const std::string& node, const std::string& msg) {
    colorLog(node, msg);
}

std::string VNodeController::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex);
    std::ostringstream oss;
    oss << "MME: " << mmeStatus << " | SGSN: " << sgsnStatus
        << " | PLMN: " << config.at("mcc") << "-" << config.at("mnc");
    return oss.str();
}

void VNodeController::printLogs() const {
    std::lock_guard<std::mutex> lock(logMutex);
    for (const auto& entry : logs) {
        std::tm* tm = std::localtime(&entry.time);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        std::cout << "[" << buf << "] [" << entry.node << "] " << entry.msg << std::endl;
    }
}

void VNodeController::printState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    std::cout << "PDP контексты (SGSN): " << pdpContexts.size() << "\n";
    for (const auto& [teid, imsi] : pdpContexts) {
        std::cout << "TEID=" << teid << " IMSI=" << imsi << "\n";
    }
    std::cout << "\nUE контексты (MME): " << ueContexts.size() << "\n";
    for (const auto& [imsi, guti] : ueContexts) {
        std::cout << "IMSI=" << imsi << " GUTI=" << guti << "\n";
    }
}

void VNodeController::mmeThreadFunc() {
    mmeStatus = "Running";
    log("MME", "MME запущен (MCC=" + config.at("mcc") + ", MNC=" + config.at("mnc") + ")");

    while (running) {
        log("MME", "MME: ожидание Initial UE Message...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    mmeStatus = "Stopped";
    log("MME", "MME остановлен");
}

void VNodeController::sgsnThreadFunc() {
    sgsnStatus = "Running";
    log("SGSN", "SGSN запущен (MCC=" + config.at("mcc") + ", MNC=" + config.at("mnc") + ")");

    while (running) {
        log("SGSN", "SGSN: ожидание GTP-C сообщений...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    sgsnStatus = "Stopped";
    log("SGSN", "SGSN остановлен");
}

void VNodeController::gtpServerThreadFunc() {
    log("GTP", "GTP-C сервер запущен на UDP " + config.at("gtp-c-port"));

    int port = std::stoi(config.at("gtp-c-port"));
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log("GTP", "Ошибка создания сокета");
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log("GTP", "Ошибка bind на порт " + config.at("gtp-c-port"));
        close(sock);
        return;
    }

    char buffer[2048];
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    while (running) {
        int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &len);
        if (n > 0) {
            log("GTP", "Получено сообщение длиной " + std::to_string(n) + " байт от " + inet_ntoa(client_addr.sin_addr));
        }
    }

    close(sock);
    log("GTP", "GTP-C сервер остановлен");
}

void VNodeController::s1apServerThreadFunc() {
    log("S1AP", "S1AP сервер запущен на SCTP " + config.at("s1ap-port"));

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void VNodeController::consoleThreadFunc() {
    std::string input;
    while (running) {
        std::cout << "\n[vEPC] Команды: status / logs / state / show / set <key> <value> / restart / stop\n> ";
        std::getline(std::cin, input);

        if (input == "status") {
            std::cout << getStatus() << std::endl;
        } else if (input == "logs") {
            printLogs();
        } else if (input == "state") {
            printState();
        } else if (input == "show") {
            printConfig();
        } else if (input.find("set ") == 0) {
            size_t spacePos = input.find(' ', 4);
            if (spacePos != std::string::npos) {
                std::string key = input.substr(4, spacePos - 4);
                std::string value = input.substr(spacePos + 1);
                setValue(key, value);
            } else {
                std::cout << "Формат: set <key> <value>\n";
            }
        } else if (input == "restart") {
            log("MAIN", "Перезапуск потоков...");
            stop();
            start();
        } else if (input == "stop") {
            stop();
            break;
        } else {
            std::cout << "Неизвестная команда\n";
        }
    }
}

void VNodeController::start() {
    loadConfig("../vmme.conf");
    loadConfig("../vsgsn.conf");

    logFile.open(LOG_FILE, std::ios::app);
    if (!logFile.is_open()) {
        log("MAIN", "Не удалось открыть файл логов " + std::string(LOG_FILE));
    }

    mmeThread = std::thread(&VNodeController::mmeThreadFunc, this);
    sgsnThread = std::thread(&VNodeController::sgsnThreadFunc, this);
    consoleThread = std::thread(&VNodeController::consoleThreadFunc, this);
    gtpThread = std::thread(&VNodeController::gtpServerThreadFunc, this);
    s1apThread = std::thread(&VNodeController::s1apServerThreadFunc, this);
    log("MAIN", "vEPC запущен");
}

void VNodeController::stop() {
    running = false;
    if (mmeThread.joinable()) mmeThread.join();
    if (sgsnThread.joinable()) sgsnThread.join();
    if (consoleThread.joinable()) consoleThread.join();
    if (gtpThread.joinable()) gtpThread.join();
    if (s1apThread.joinable()) s1apThread.join();

    if (logFile.is_open()) logFile.close();
    log("MAIN", "vEPC остановлен");
}

void VNodeController::restart() {
    stop();
    start();
}

int main() {
    system("mkdir -p logs");

    std::cout << "vEPC — Virtual EPC (SGSN + MME)\n";
    std::cout << "Запуск...\n";

    VNodeController ctrl;
    ctrl.start();

    std::cout << "Для остановки введите 'stop' в консоли\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "stop") break;
    }

    ctrl.stop();

    return 0;
}