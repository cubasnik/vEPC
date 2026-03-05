#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <cstring>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#ifdef _WIN32
#define CLI_TCP_HOST    "127.0.0.1"
#define CLI_TCP_PORT    5555
#define CLI_ENDPOINT    "127.0.0.1:5555"
#else
#define CLI_SOCKET      "/tmp/vepc.sock"
#define CLI_ENDPOINT    CLI_SOCKET
#endif

#define INTERFACES_CONF "config/interfaces.conf"

#define RST  "\033[0m"
#define BOLD "\033[1m"
#define CYAN "\033[1;36m"
#define GRN  "\033[1;32m"
#define YEL  "\033[1;33m"
#define RED  "\033[1;31m"
#define WHT  "\033[0;37m"
#define DIM  "\033[2m"
#define MAG  "\033[1;35m"

struct Interface {
    std::string name;
    std::string proto;
    std::string ip;
    std::string port;
    std::string peer;
    std::string desc;
};
static std::vector<Interface> g_ifaces;

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

static void loadInterfaces() {
    g_ifaces.clear();
    std::ifstream f(INTERFACES_CONF);
    if (!f.is_open()) {
        std::string alt = std::string("../") + INTERFACES_CONF;
        f.open(alt);
        if (!f.is_open()) return;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Interface cur;
        std::string addr;
        std::getline(ss, cur.name, '|');
        std::getline(ss, cur.proto, '|');
        std::getline(ss, addr,     '|');
        std::getline(ss, cur.peer, '|');
        auto tr = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
        };
        tr(cur.name); tr(cur.proto); tr(addr); tr(cur.peer);
        if (cur.name.empty()) continue;
        size_t colon = addr.find(':');
        if (colon != std::string::npos) {
            cur.ip   = addr.substr(0, colon);
            cur.port = addr.substr(colon + 1);
            tr(cur.ip); tr(cur.port);
        } else {
            cur.ip   = addr;
            cur.port = "";
        }
        g_ifaces.push_back(cur);
    }
}

static void printIfaceTableCustom(const std::vector<size_t>& idxs, int& num) {
    const int wN = 3, wI = 12, wP = 16, wIP = 16, wPort = 7, wR = 14;
    auto hline = [&]() {
        auto seg = [](int w){ for(int i=0;i<w+2;i++) std::cout<<"-"; };
        std::cout << "+"; seg(wN); std::cout << "+";
                        seg(wI); std::cout << "+";
                        seg(wP); std::cout << "+";
                        seg(wIP); std::cout << "+";
                        seg(wPort); std::cout << "+";
                        seg(wR); std::cout << "+\n";
    };
    auto row = [&](const std::string& n, const std::string& i,
                   const std::string& p, const std::string& ip,
                   const std::string& port, const std::string& r, bool hdr = false) {
        auto cell = [&](const std::string& v, int w, bool h, const char* color = WHT) {
            std::cout << " " << color << std::left << std::setw(w) << v << RST << " |";
        };
        std::cout << "|";
        cell(n, wN, hdr, hdr ? CYAN : WHT);
        cell(i, wI, hdr, hdr ? CYAN : GRN);
        cell(p, wP, hdr, hdr ? CYAN : WHT);
        cell(ip, wIP, hdr, hdr ? CYAN : WHT);
        cell(port, wPort, hdr, hdr ? CYAN : WHT);
        cell(r, wR, hdr, hdr ? CYAN : WHT);
        std::cout << "\n";
    };
    hline();
    row("No", "Name", "Protocol", "IP", "Port", "Remote NE", true);
    hline();
    for (size_t idx : idxs) {
        const auto& f = g_ifaces[idx];
        row(std::to_string(num++), f.name, f.proto, f.ip, f.port, f.peer, false);
    }
    hline();
}

static void printHelp() {
    struct CliCmd {
        std::string name, proto, ip, port, peer;
    };
    std::vector<CliCmd> cli_cmds = {
        {"exit", "-", "-", "-", "Exit CLI"},
        {"help", "-", "-", "-", "Show help"},
        {"show config", "-", "-", "-", "Show config"},
        {"show iface", "-", "-", "-", "Show interfaces"},
        {"restart", "-", "-", "-", "Restart server"},
        {"log", "-", "-", "-", "Show log"}
    };
    auto printCenteredTitle = [](const std::string& title, int tableWidth) {
        int len = static_cast<int>(title.size());
        int pad = (tableWidth - len) / 2;
        if (pad < 0) pad = 0;
        std::cout << "\n" << BOLD << MAG << std::string(pad, ' ') << title << RST << "\n";
    };
    auto printCliTable = [&]() {
        const int wN = 3, wI = 12, wP = 16, wIP = 16, wPort = 7, wR = 14;
        int tableWidth = wN + wI + wP + wIP + wPort + wR + 6 * 3 + 7; // 6 columns, 7 borders
        auto hline = [&]() {
            auto seg = [](int w){ for(int i=0;i<w+2;i++) std::cout<<"-"; };
            std::cout << "+"; seg(wN); std::cout << "+";
                            seg(wI); std::cout << "+";
                            seg(wP); std::cout << "+";
                            seg(wIP); std::cout << "+";
                            seg(wPort); std::cout << "+";
                            seg(wR); std::cout << "+\n";
        };
        auto row = [&](const std::string& n, const std::string& i,
                       const std::string& p, const std::string& ip,
                       const std::string& port, const std::string& r, bool hdr = false, bool isExit = false) {
            auto cell = [&](const std::string& v, int w, bool h, const char* color = WHT) {
                std::cout << " " << color << std::left << std::setw(w) << v << RST << " |";
            };
            std::cout << "|";
            cell(n, wN, hdr, hdr ? CYAN : WHT);
            cell(i, wI, hdr, hdr ? CYAN : (isExit ? RED : GRN));
            cell(p, wP, hdr, hdr ? CYAN : WHT);
            cell(ip, wIP, hdr, hdr ? CYAN : WHT);
            cell(port, wPort, hdr, hdr ? CYAN : WHT);
            cell(r, wR, hdr, hdr ? CYAN : WHT);
            std::cout << "\n";
        };
        printCenteredTitle("==================== CLI COMMANDS =======================", tableWidth);
        hline();
        row("No", "Name", "Protocol", "IP", "Port", "Remote NE", true);
        hline();
        int n = 0;
        for (const auto& c : cli_cmds) {
            bool isExit = (c.name == "exit");
            row(std::to_string(n++), c.name, c.proto, c.ip, c.port, c.peer, false, isExit);
        }
        hline();
    };
    printCliTable();

    std::vector<size_t> sgsn_idx, mme_idx;
    for (size_t i = 0; i < g_ifaces.size(); ++i) {
        std::string remote = g_ifaces[i].peer;
        if (remote.find("GGSN") != std::string::npos ||
            remote.find("HLR") != std::string::npos ||
            remote.find("BSC") != std::string::npos ||
            remote.find("SGSN-roaming") != std::string::npos ||
            (remote.find("EIR") != std::string::npos && g_ifaces[i].name.find("Gf") != std::string::npos)) {
            sgsn_idx.push_back(i);
        } else {
            mme_idx.push_back(i);
        }
    }
    int iface_num = static_cast<int>(cli_cmds.size());
    auto printIfaceTableCustomCentered = [&](const std::vector<size_t>& idxs, int& num, const std::string& title) {
        const int wN = 3, wI = 12, wP = 16, wIP = 16, wPort = 7, wR = 14;
        int tableWidth = wN + wI + wP + wIP + wPort + wR + 6 * 3 + 7;
        printCenteredTitle(title, tableWidth);
        auto hline = [&]() {
            auto seg = [](int w){ for(int i=0;i<w+2;i++) std::cout<<"-"; };
            std::cout << "+"; seg(wN); std::cout << "+";
                            seg(wI); std::cout << "+";
                            seg(wP); std::cout << "+";
                            seg(wIP); std::cout << "+";
                            seg(wPort); std::cout << "+";
                            seg(wR); std::cout << "+\n";
        };
        auto row = [&](const std::string& n, const std::string& i,
                       const std::string& p, const std::string& ip,
                       const std::string& port, const std::string& r, bool hdr = false) {
            auto cell = [&](const std::string& v, int w, bool h, const char* color = WHT) {
                std::cout << " " << color << std::left << std::setw(w) << v << RST << " |";
            };
            std::cout << "|";
            cell(n, wN, hdr, hdr ? CYAN : WHT);
            cell(i, wI, hdr, hdr ? CYAN : GRN);
            cell(p, wP, hdr, hdr ? CYAN : WHT);
            cell(ip, wIP, hdr, hdr ? CYAN : WHT);
            cell(port, wPort, hdr, hdr ? CYAN : WHT);
            cell(r, wR, hdr, hdr ? CYAN : WHT);
            std::cout << "\n";
        };
        hline();
        row("No", "Name", "Protocol", "IP", "Port", "Remote NE", true);
        hline();
        for (size_t idx : idxs) {
            const auto& f = g_ifaces[idx];
            row(std::to_string(num++), f.name, f.proto, f.ip, f.port, f.peer, false);
        }
        hline();
    };
    printIfaceTableCustomCentered(sgsn_idx, iface_num, "==================== SGSN INTERFACES ====================");
    printIfaceTableCustomCentered(mme_idx, iface_num, "==================== MME INTERFACES =====================");

    std::cout << DIM "\nConnecting to " CLI_ENDPOINT RST "\n";
    std::cout << GRN "\n> " RST;
}

static void sendToServer(const std::string& cmd) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << RED "WSAStartup failed" RST "\n";
        return;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cout << RED "socket() failed" RST "\n";
        WSACleanup();
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(CLI_TCP_PORT);
    if (inet_pton(AF_INET, CLI_TCP_HOST, &addr.sin_addr) != 1) {
        std::cout << RED "inet_pton failed" RST "\n";
        closesocket(sock);
        WSACleanup();
        return;
    }
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        std::cout << RED "Cannot connect to server. Please make sure vEPC is running." RST "\n";
        return;
    }
    send(sock, cmd.c_str(), static_cast<int>(cmd.size()), 0);
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, static_cast<int>(sizeof(buf) - 1), 0);
        if (n <= 0) break;
        buf[n] = '\0';
        std::cout << buf << std::flush;
    }
    closesocket(sock);
    WSACleanup();
#else
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return; }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLI_SOCKET, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        std::cout << RED "Cannot connect to server. Please make sure vEPC is running." RST "\n";
        return;
    }
    send(sock, cmd.c_str(), cmd.size(), 0);
    char buf[4096];
    while (true) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        std::cout << buf << std::flush;
    }
    close(sock);
#endif
}

static void handleIface(const Interface& iface, const std::string& action) {
    if (action.empty() || action == "status") {
        std::cout << "\n" BOLD CYAN "== Interface: " RST BOLD << iface.name << RST << "\n"
                  << CYAN "   Protocol  : " RST << iface.proto << "\n"
                  << CYAN "   IP        : " RST << iface.ip    << "\n"
                  << CYAN "   Port      : " RST << iface.port  << "\n"
                  << CYAN "   Remote NE : " RST << iface.peer  << "\n";
        sendToServer("iface_status " + iface.name);
    } else if (action == "up" || action == "down" || action == "reset") {
        std::cout << YEL "→ " RST << "iface_" << action << " " << iface.name << "\n";
        sendToServer("iface_" + action + " " + iface.name);
    } else {
        std::cout << RED "Unknown action: " RST << action
                  << "  (allowed: up, down, reset)\n";
    }
}

static int resolveIface(const std::string& token) {
    bool isNum = !token.empty() &&
                 token.find_first_not_of("0123456789") == std::string::npos;
    if (isNum) {
        int idx = std::stoi(token) - 6;
        return (idx >= 0 && idx < (int)g_ifaces.size()) ? idx : -1;
    }
    std::string lo = token;
    for (auto& c : lo) c = (char)tolower(c);
    for (int i = 0; i < (int)g_ifaces.size(); i++) {
        std::string n = g_ifaces[i].name;
        for (auto& c : n) c = (char)tolower(c);
        if (n == lo) return i;
    }
    return -1;
}

static const char* SYS_CMDS[] = { "", "status", "logs", "state", "show", "stop" };

int main() {
    loadInterfaces();
    printHelp();
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) { std::cout << GRN "> " RST; continue; }
        std::istringstream ss(line);
        std::string token, action;
        ss >> token >> action;
        if (token == "exit" || token == "quit" || token == "0") break;
        bool isNum = !token.empty() &&
                     token.find_first_not_of("0123456789") == std::string::npos;
        if (isNum) {
            int n = std::stoi(token);
            if (n >= 1 && n <= 5) {
                sendToServer(SYS_CMDS[n]);
                std::cout << GRN "> " RST;
                continue;
            }
        }
        int idx = resolveIface(token);
        if (idx >= 0) {
            handleIface(g_ifaces[idx], action);
            std::cout << GRN "> " RST;
            continue;
        }
        sendToServer(line);
        std::cout << GRN "> " RST;
    }
    std::cout << "\nExiting vEPC CLI\n";
    return 0;
}