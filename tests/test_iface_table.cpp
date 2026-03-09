#include <iostream>
#include <vector>
#include <iomanip>
#include <string>

#define RST  "\033[0m"
#define BOLD "\033[1m"
#define CYAN "\033[1;36m"
#define GRN  "\033[1;32m"
#define MAG  "\033[1;35m"
#define WHT  "\033[0;37m"

struct Interface {
    std::string name;
    std::string proto;
    std::string ip;
    std::string port;
    std::string peer;
};

std::vector<Interface> g_ifaces = {
    {"S1-MME", "SCTP", "127.0.0.1", "36412", "eNodeB"},
    {"S6a", "DIAMETER", "127.0.0.1", "3868", "HSS"},
    {"S11", "UDP", "127.0.0.1", "2123", "SGW"},
    {"S10", "UDP", "127.0.0.1", "2123", "MME"},
    {"S3", "UDP", "127.0.0.1", "2123", "SGSN"},
    {"S13", "DIAMETER", "127.0.0.1", "3870", "EIR"},
    {"Gn", "UDP", "10.10.10.1", "2152", "GGSN"},
    {"Gr", "TCP", "192.168.100.5", "3868", "HLR"},
    {"Gn", "UDP", "10.10.10.1", "2152", "GGSN"},
    {"Gb", "NS", "192.168.88.17", "23000", "BSC"},
    {"Gp", "UDP", "10.0.0.2", "2123", "SGSN-roaming"},
    {"Gf", "DIAMETER", "127.0.0.1", "3870", "EIR"}
};

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

int main() {
    // --- CLI команды (пример) ---
    struct CliCmd {
        std::string name, proto, ip, port, peer;
    };
    std::vector<CliCmd> cli_cmds = {
        {"help", "-", "-", "-", "Show help"},
        {"show config", "-", "-", "-", "Show config"},
        {"show iface", "-", "-", "-", "Show interfaces"},
        {"restart", "-", "-", "-", "Restart server"},
        {"log", "-", "-", "-", "Show log"},
        {"exit", "-", "-", "-", "Exit CLI"}
    };
    // Вывод CLI таблицы
    auto printCliTable = [&]() {
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
        int n = 0;
        for (const auto& c : cli_cmds) {
            row(std::to_string(n++), c.name, c.proto, c.ip, c.port, c.peer, false);
        }
        hline();
    };
    std::cout << "\n" << BOLD << MAG << "==================== CLI COMMANDS =======================" << RST << "\n";
    printCliTable();

    // --- SGSN/MME разделение ---
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
    std::cout << "\n" << BOLD << MAG << "==================== SGSN INTERFACES ====================" << RST << "\n";
    printIfaceTableCustom(sgsn_idx, iface_num);
    std::cout << "\n" << BOLD << MAG << "==================== MME INTERFACES =====================" << RST << "\n";
    printIfaceTableCustom(mme_idx, iface_num);
    return 0;
}
