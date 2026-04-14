#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <map>
#include <cctype>
#include <cstdint>
#include <thread>
#include <set>

// Глобальная карта: номер в таблице -> индекс в g_ifaces
std::map<int, size_t> iface_num_to_idx;

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <termios.h>
#ifdef VEPC_USE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif
#include "linux_interface.h"
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include "cisco_cli_commands.h"

#ifdef _WIN32
#define CLI_TCP_HOST    "127.0.0.1"
#define CLI_TCP_PORT    5555
#define CLI_ENDPOINT    "127.0.0.1:5555"
#else
#define CLI_SOCKET      "/tmp/vepc.sock"
#define CLI_ENDPOINT    CLI_SOCKET
#endif

#define INTERFACES_CONF "config/interfaces.conf"

static std::string resolveInterfacesConfPath() {
    const char* cp = getenv("CONFIG_PATH");
    if (cp && *cp) {
        return std::string(cp) + "/interfaces.conf";
    }
    return INTERFACES_CONF;
}

#define RST  "\033[0m"
#define BOLD "\033[1m"
#define CYAN "\033[1;36m"
#define GRN  "\033[1;32m"
#define YEL  "\033[1;33m"
#define BLU  "\033[1;34m"
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
static std::string g_cliHostname = "vepc";
static bool g_suppressPromptOutput = false;

struct InterfaceOverviewRow {
    std::string name;
    std::string proto;
    std::string address;
    std::string admin;
    std::string oper;
    std::string implementation;
    std::string peer;
    bool        valid = false;
};

struct InterfaceStatusEntry {
    std::string name;
    std::string proto;
    std::string ip;
    std::string port;
    std::string admin;
    std::string oper;
    std::string implementation;
    std::string peer;
    std::string bind;
    std::string listen;
    std::string reason;
    bool        valid = false;
};

static constexpr int IFACE_COL_NAME = 10;
static constexpr int IFACE_COL_PROTO = 10;
static constexpr int IFACE_COL_ADDR = 21;
static constexpr int IFACE_COL_ADMIN = 6;
static constexpr int IFACE_COL_OPER = 9;
static constexpr int IFACE_COL_IMPL = 8;

static bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

static std::string toUpperCopy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

static bool isPeerLabel(const std::string& label) {
    const std::string upper = toUpperCopy(trim(label));
    return upper == "PEER" || upper == "REMOTE NE";
}

static const char* peerAccentColor() {
    return BLU;
}

static const char* semanticColor(const std::string& value) {
    const std::string upper = toUpperCopy(trim(value));
    if (upper == "UP" || upper == "OK" || upper == "RUNNING" || upper == "IMPLEMENTED" || upper == "YES") {
        return GRN;
    }
    if (upper == "DOWN" || upper == "FAILED" || upper == "NO" || upper == "ERROR"
        || upper == "N/A" || upper == "NOT OK" || upper == "NOT_OK" || upper == "NOT-OK") {
        return RED;
    }
    if (upper == "DEGRADED" || upper == "PENDING" || upper == "BLOCKED" || upper == "DISABLED") {
        return YEL;
    }
    if (upper == "PLANNED" || upper == "STOPPED" || upper == "UNKNOWN") {
        return MAG;
    }
    return WHT;
}

static void printSemanticValue(const std::string& value) {
    std::cout << semanticColor(value) << value << RST;
}

static void printPrompt(const std::string& promptText = "> ") {
    if (g_suppressPromptOutput) {
        return;
    }
    std::cout << BOLD << GRN << promptText << RST;
}

static void printLocalInfo(const std::string& message) {
    std::cout << CYAN << message << RST << "\n";
}

static void printLocalWarning(const std::string& message) {
    std::cout << YEL << message << RST << "\n";
}

static void printLocalError(const std::string& message) {
    std::cout << BOLD << RED << message << RST << "\n";
}

static void printLocalBanner(const std::string& label, const std::string& value) {
    std::cout << "\n" << BOLD << CYAN << label << RST << " " << BOLD << GRN << value << RST << "\n";
}

static void printServerBlockHeader(const std::string& sourceCommand);
static void printServerBlockFooter();

static void printSectionTitle(const std::string& title, int tableWidth) {
    const int len = static_cast<int>(title.size());
    int pad = (tableWidth - len) / 2;
    if (pad < 0) {
        pad = 0;
    }
    std::cout << "\n" << BOLD << MAG << std::string(static_cast<size_t>(pad), ' ') << title << RST << "\n";
}

static std::string compactImplementation(const std::string& value) {
    const std::string upper = toUpperCopy(trim(value));
    if (upper == "IMPLEMENTED") {
        return "IMPL";
    }
    if (upper == "PLANNED") {
        return "PLAN";
    }
    return value;
}

static void printCompactInterfaceHeader() {
    std::cout << DIM
              << std::left
              << std::setw(IFACE_COL_NAME) << "Name"
              << std::setw(IFACE_COL_PROTO) << "Proto"
              << std::setw(IFACE_COL_ADDR) << "Address"
              << std::setw(IFACE_COL_ADMIN) << "Adm"
              << std::setw(IFACE_COL_OPER) << "Oper"
              << std::setw(IFACE_COL_IMPL) << "Impl"
              << "Peer"
              << RST << "\n";
    std::cout << DIM << std::string(IFACE_COL_NAME + IFACE_COL_PROTO + IFACE_COL_ADDR + IFACE_COL_ADMIN + IFACE_COL_OPER + IFACE_COL_IMPL + 4, '-') << RST << "\n";
}

static bool isInteractiveSession() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

static InterfaceOverviewRow parseInterfaceOverviewRow(const std::string& line) {
    InterfaceOverviewRow row;
    if (line.size() < 86) {
        return row;
    }

    row.name = trim(line.substr(0, 12));
    row.proto = trim(line.substr(12, 12));
    row.address = trim(line.substr(24, 22));
    row.admin = trim(line.substr(46, 12));
    row.oper = trim(line.substr(58, 12));
    row.implementation = trim(line.substr(70, 16));
    row.peer = trim(line.substr(86));
    row.valid = !row.name.empty() && !row.proto.empty() && !row.address.empty();
    return row;
}

static void printInterfaceOverviewRow(const InterfaceOverviewRow& row) {
    const std::string impl = compactImplementation(row.implementation);
    std::cout << std::left
              << GRN << std::setw(IFACE_COL_NAME) << row.name << RST
              << WHT << std::setw(IFACE_COL_PROTO) << row.proto
              << std::setw(IFACE_COL_ADDR) << row.address << RST
              << semanticColor(row.admin) << std::setw(IFACE_COL_ADMIN) << row.admin << RST
              << semanticColor(row.oper) << std::setw(IFACE_COL_OPER) << row.oper << RST
              << semanticColor(row.implementation) << std::setw(IFACE_COL_IMPL) << impl << RST
              << peerAccentColor() << row.peer << RST << "\n";
}

struct KeyValueEntry {
    std::string key;
    std::string value;
};

enum class ConfigSection {
    Common,
    Sgsn,
    Mme
};

enum class InterfaceSection {
    Common,
    Sgsn,
    Mme
};

static std::string toLowerCopy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

static bool isShowConfigCommand(const std::string& cmd) {
    return toLowerCopy(trim(cmd)) == "show config";
}

static bool isShowIfaceCommand(const std::string& cmd) {
    const std::string normalized = toLowerCopy(trim(cmd));
    return normalized == "show iface" || normalized == "show interface";
}

static ConfigSection classifyConfigEntry(const std::string& key) {
    const std::string normalized = toLowerCopy(trim(key));

    if (normalized == "mcc"
        || normalized == "mnc"
        || normalized == "gtp-c-port"
        || normalized == "gtp-u-port"
        || normalized == "mme-ip"
        || normalized == "sgsn-ip"
        || normalized == "s1ap-port") {
        return ConfigSection::Common;
    }

    if (startsWith(normalized, "gb-")
        || startsWith(normalized, "gn-")
        || normalized == "iups-enabled"
        || normalized == "sgsn-code") {
        return ConfigSection::Sgsn;
    }

    return ConfigSection::Mme;
}

static InterfaceSection classifyInterfaceEntry(const std::string& name) {
    const std::string normalized = toUpperCopy(trim(name));
    if (startsWith(normalized, "G")) {
        return InterfaceSection::Sgsn;
    }
    if (startsWith(normalized, "S")) {
        return InterfaceSection::Mme;
    }
    return InterfaceSection::Common;
}

static bool tryParseKeyValueLine(const std::string& line, KeyValueEntry& entry) {
    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
        return false;
    }

    size_t separatorPos = trimmed.find(" = ");
    size_t separatorWidth = 3;
    if (separatorPos == std::string::npos) {
        separatorPos = trimmed.find(':');
        separatorWidth = 1;
    }
    if (separatorPos == std::string::npos) {
        return false;
    }

    entry.key = trim(trimmed.substr(0, separatorPos));
    entry.value = trim(trimmed.substr(separatorPos + separatorWidth));
    return !entry.key.empty();
}

static void printTableRule(size_t keyWidth, size_t valueWidth) {
    std::cout << DIM << "+-" << std::string(keyWidth, '-') << "-+-"
              << std::string(valueWidth, '-') << "-+" << RST << "\n";
}

static void printTableCell(const std::string& text, size_t width, const char* color) {
    std::cout << color << std::left << std::setw(static_cast<int>(width)) << text << RST;
}

static void printTableSectionRow(const std::string& title, size_t keyWidth, size_t valueWidth) {
    const size_t totalWidth = keyWidth + valueWidth + 3;
    std::cout << DIM << "| " << RST;
    printTableCell(title, totalWidth, MAG);
    std::cout << DIM << " |" << RST << "\n";
}

static void printGroupedConfigTable(const std::vector<KeyValueEntry>& entries) {
    if (entries.empty()) {
        return;
    }

    std::vector<KeyValueEntry> commonEntries;
    std::vector<KeyValueEntry> sgsnEntries;
    std::vector<KeyValueEntry> mmeEntries;

    size_t keyWidth = std::string("Key").size();
    size_t valueWidth = std::string("Value").size();
    for (const auto& entry : entries) {
        keyWidth = (std::max)(keyWidth, entry.key.size());
        valueWidth = (std::max)(valueWidth, entry.value.size());

        switch (classifyConfigEntry(entry.key)) {
        case ConfigSection::Common:
            commonEntries.push_back(entry);
            break;
        case ConfigSection::Sgsn:
            sgsnEntries.push_back(entry);
            break;
        case ConfigSection::Mme:
            mmeEntries.push_back(entry);
            break;
        }
    }

    auto printRows = [&](const std::vector<KeyValueEntry>& sectionEntries) {
        for (const auto& entry : sectionEntries) {
            std::cout << DIM << "| " << RST;
            printTableCell(entry.key, keyWidth, CYAN);
            std::cout << DIM << " | " << RST;
            printTableCell(entry.value, valueWidth, isPeerLabel(entry.key) ? peerAccentColor() : semanticColor(entry.value));
            std::cout << DIM << " |" << RST << "\n";
        }
    };

    printTableRule(keyWidth, valueWidth);
    std::cout << DIM << "| " << RST;
    printTableCell("Key", keyWidth, CYAN);
    std::cout << DIM << " | " << RST;
    printTableCell("Value", valueWidth, CYAN);
    std::cout << DIM << " |" << RST << "\n";
    printTableRule(keyWidth, valueWidth);

    const auto printSection = [&](const std::string& title, const std::vector<KeyValueEntry>& sectionEntries) {
        if (sectionEntries.empty()) {
            return;
        }
        printTableSectionRow(title, keyWidth, valueWidth);
        printTableRule(keyWidth, valueWidth);
        printRows(sectionEntries);
        printTableRule(keyWidth, valueWidth);
    };

    printSection("COMMON", commonEntries);
    printSection("SGSN", sgsnEntries);
    printSection("MME", mmeEntries);
}

static InterfaceStatusEntry makeInterfaceStatusEntry(const InterfaceOverviewRow& row) {
    InterfaceStatusEntry entry;
    const size_t separatorPos = row.address.rfind(':');
    entry.name = row.name;
    entry.proto = row.proto;
    if (separatorPos == std::string::npos) {
        entry.ip = row.address;
        entry.port = "-";
    } else {
        entry.ip = trim(row.address.substr(0, separatorPos));
        entry.port = trim(row.address.substr(separatorPos + 1));
    }
    entry.admin = row.admin;
    entry.oper = row.oper;
    entry.implementation = compactImplementation(row.implementation);
    entry.peer = row.peer;
    entry.bind = "?";
    entry.listen = "?";
    entry.valid = row.valid;
    return entry;
}

static void applyDiagToInterfaceStatus(InterfaceStatusEntry& entry, const std::string& line) {
    const size_t diagPos = line.find("diag:");
    if (diagPos == std::string::npos) {
        return;
    }

    std::string remaining = trim(line.substr(diagPos + std::string("diag:").size()));
    std::istringstream diagStream(remaining);
    std::string segment;
    while (std::getline(diagStream, segment, ';')) {
        segment = trim(segment);
        const size_t equals = segment.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(segment.substr(0, equals));
        const std::string value = trim(segment.substr(equals + 1));
        const size_t reasonPos = value.find(" (");
        const std::string state = (reasonPos == std::string::npos || value.empty() || value.back() != ')')
            ? value
            : value.substr(0, reasonPos);

        if (key == "bind") {
            entry.bind = state;
        } else if (key == "listen") {
            entry.listen = state;
        } else if (key == "reason") {
            entry.reason = value;
        }
    }
}

static void printGroupedInterfaceTable(const std::vector<InterfaceStatusEntry>& entries) {
    if (entries.empty()) {
        return;
    }

    std::vector<InterfaceStatusEntry> commonEntries;
    std::vector<InterfaceStatusEntry> sgsnEntries;
    std::vector<InterfaceStatusEntry> mmeEntries;

    size_t nameWidth = std::string("Name").size();
    size_t protoWidth = std::string("Proto").size();
    size_t ipWidth = std::string("IP").size();
    size_t portWidth = std::string("Port").size();
    size_t adminWidth = std::string("Adm").size();
    size_t operWidth = std::string("Oper").size();
    size_t implWidth = std::string("Impl").size();
    size_t peerWidth = std::string("Peer").size();
    size_t bindWidth = std::string("Bind").size();
    size_t listenWidth = std::string("Listen").size();
    size_t reasonWidth = std::string("Reason").size();

    for (const auto& entry : entries) {
        nameWidth = (std::max)(nameWidth, entry.name.size());
        protoWidth = (std::max)(protoWidth, entry.proto.size());
        ipWidth = (std::max)(ipWidth, entry.ip.size());
        portWidth = (std::max)(portWidth, entry.port.size());
        adminWidth = (std::max)(adminWidth, entry.admin.size());
        operWidth = (std::max)(operWidth, entry.oper.size());
        implWidth = (std::max)(implWidth, entry.implementation.size());
        peerWidth = (std::max)(peerWidth, entry.peer.size());
        bindWidth = (std::max)(bindWidth, entry.bind.size());
        listenWidth = (std::max)(listenWidth, entry.listen.size());
        reasonWidth = (std::max)(reasonWidth, entry.reason.size());

        switch (classifyInterfaceEntry(entry.name)) {
        case InterfaceSection::Common:
            commonEntries.push_back(entry);
            break;
        case InterfaceSection::Sgsn:
            sgsnEntries.push_back(entry);
            break;
        case InterfaceSection::Mme:
            mmeEntries.push_back(entry);
            break;
        }
    }

    const auto printOverviewRule = [&]() {
        std::cout << DIM
                  << "+-" << std::string(nameWidth, '-')
                  << "-+-" << std::string(protoWidth, '-')
                  << "-+-" << std::string(ipWidth, '-')
                  << "-+-" << std::string(portWidth, '-')
                  << "-+-" << std::string(adminWidth, '-')
                  << "-+-" << std::string(operWidth, '-')
                  << "-+-" << std::string(implWidth, '-')
                  << "-+-" << std::string(peerWidth, '-')
                  << "-+" << RST << "\n";
    };

    const size_t overviewWidth = nameWidth + protoWidth + ipWidth + portWidth + adminWidth + operWidth + implWidth + peerWidth + 21;
    const size_t diagWidth = nameWidth + bindWidth + listenWidth + reasonWidth + 9;
    const size_t diagReasonWidth = reasonWidth + (overviewWidth - diagWidth);

    const auto printDiagRule = [&]() {
        std::cout << DIM
                  << "+-" << std::string(nameWidth, '-')
                  << "-+-" << std::string(bindWidth, '-')
                  << "-+-" << std::string(listenWidth, '-')
                  << "-+-" << std::string(diagReasonWidth, '-')
                  << "-+" << RST << "\n";
    };

    const auto printSectionBanner = [&](const std::string& title, size_t totalWidth) {
        std::cout << DIM << "| " << RST;
        printTableCell(title, totalWidth, CYAN);
        std::cout << DIM << " |" << RST << "\n";
    };

    const auto printOverviewHeader = [&]() {
        std::cout << DIM << "| " << RST;
        printTableCell("Name", nameWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Proto", protoWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("IP", ipWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Port", portWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Adm", adminWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Oper", operWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Impl", implWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Peer", peerWidth, CYAN);
        std::cout << DIM << " |" << RST << "\n";
    };

    const auto printDiagHeader = [&]() {
        std::cout << DIM << "| " << RST;
        printTableCell("Name", nameWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Bind", bindWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Listen", listenWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell("Reason", diagReasonWidth, CYAN);
        std::cout << DIM << " |" << RST << "\n";
    };

    const auto printOverviewEntry = [&](const InterfaceStatusEntry& entry) {
        std::cout << DIM << "| " << RST;
        printTableCell(entry.name, nameWidth, GRN);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.proto, protoWidth, WHT);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.ip, ipWidth, WHT);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.port, portWidth, WHT);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.admin, adminWidth, semanticColor(entry.admin));
        std::cout << DIM << " | " << RST;
        printTableCell(entry.oper, operWidth, semanticColor(entry.oper));
        std::cout << DIM << " | " << RST;
        printTableCell(entry.implementation, implWidth, semanticColor(entry.implementation));
        std::cout << DIM << " | " << RST;
        printTableCell(entry.peer, peerWidth, peerAccentColor());
        std::cout << DIM << " |" << RST << "\n";
    };

    const auto printDiagEntry = [&](const InterfaceStatusEntry& entry) {
        std::cout << DIM << "| " << RST;
        printTableCell(entry.name, nameWidth, GRN);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.bind, bindWidth, semanticColor(entry.bind));
        std::cout << DIM << " | " << RST;
        printTableCell(entry.listen, listenWidth, semanticColor(entry.listen));
        std::cout << DIM << " | " << RST;
        printTableCell(entry.reason, diagReasonWidth, semanticColor(entry.reason));
        std::cout << DIM << " |" << RST << "\n";
    };

    const auto printSection = [&](const std::string& title, const std::vector<InterfaceStatusEntry>& sectionEntries) {
        if (sectionEntries.empty()) {
            return;
        }
        printOverviewRule();
        printSectionBanner(title, overviewWidth);
        printOverviewRule();
        printOverviewHeader();
        printOverviewRule();
        for (const auto& entry : sectionEntries) {
            printOverviewEntry(entry);
        }
        printOverviewRule();
        printDiagRule();
        printSectionBanner(title + " DIAG", overviewWidth);
        printDiagRule();
        printDiagHeader();
        printDiagRule();
        for (const auto& entry : sectionEntries) {
            printDiagEntry(entry);
        }
        printDiagRule();
    };

    printSection("COMMON", commonEntries);
    printSection("SGSN", sgsnEntries);
    printSection("MME", mmeEntries);
}

static void printKeyValueTable(const std::vector<KeyValueEntry>& entries) {
    if (entries.empty()) {
        return;
    }

    size_t keyWidth = std::string("Key").size();
    size_t valueWidth = std::string("Value").size();
    for (const auto& entry : entries) {
        keyWidth = (std::max)(keyWidth, entry.key.size());
        valueWidth = (std::max)(valueWidth, entry.value.size());
    }

    printTableRule(keyWidth, valueWidth);
    std::cout << DIM << "| " << RST;
    printTableCell("Key", keyWidth, CYAN);
    std::cout << DIM << " | " << RST;
    printTableCell("Value", valueWidth, CYAN);
    std::cout << DIM << " |" << RST << "\n";
    printTableRule(keyWidth, valueWidth);

    for (const auto& entry : entries) {
        std::cout << DIM << "| " << RST;
        printTableCell(entry.key, keyWidth, CYAN);
        std::cout << DIM << " | " << RST;
        printTableCell(entry.value, valueWidth, isPeerLabel(entry.key) ? peerAccentColor() : semanticColor(entry.value));
        std::cout << DIM << " |" << RST << "\n";
    }

    printTableRule(keyWidth, valueWidth);
}

static void printKeyValueLine(const std::string& key, const std::string& value) {
    std::cout << CYAN << key << RST << DIM << " : " << RST;
    printSemanticValue(value);
    std::cout << "\n";
}

static void printStatusSummary(const std::string& value) {
    std::istringstream ss(value);
    std::string part;
    bool first = true;
    while (std::getline(ss, part, '|')) {
        part = trim(part);
        const size_t colon = part.find(':');
        if (!first) {
            std::cout << DIM << " | " << RST;
        }
        first = false;
        if (colon == std::string::npos) {
            printSemanticValue(part);
            continue;
        }
        const std::string subKey = trim(part.substr(0, colon));
        const std::string subValue = trim(part.substr(colon + 1));
        std::cout << YEL << subKey << RST << DIM << ": " << RST;
        printSemanticValue(subValue);
    }
    std::cout << "\n";
}

static void printInterfaceDiagLine(const std::string& line) {
    const size_t diagPos = line.find("diag:");
    const std::string rowPart = trim(diagPos == std::string::npos ? line : line.substr(0, diagPos));
    const std::string diagPart = diagPos == std::string::npos ? "" : trim(line.substr(diagPos));

    if (!rowPart.empty()) {
        const InterfaceOverviewRow row = parseInterfaceOverviewRow(rowPart);
        if (row.valid) {
            printInterfaceOverviewRow(row);
        }
    }

    if (!diagPart.empty()) {
        std::cout << DIM << "  diag " << RST;
        std::string remaining = diagPart.substr(std::string("diag:").size());
        std::istringstream diagStream(remaining);
        std::string segment;
        std::string bindState;
        std::string listenState;
        std::string reasonState;
        bool first = true;
        while (std::getline(diagStream, segment, ';')) {
            segment = trim(segment);
            first = false;

            const size_t equals = segment.find('=');
            if (equals == std::string::npos) {
                continue;
            }

            const std::string key = trim(segment.substr(0, equals));
            const std::string value = trim(segment.substr(equals + 1));
            const size_t reasonPos = value.find(" (");
            const std::string state = (reasonPos == std::string::npos || value.back() != ')')
                ? value
                : value.substr(0, reasonPos);

            if (key == "bind") {
                bindState = state;
            } else if (key == "listen") {
                listenState = state;
            } else if (key == "reason") {
                reasonState = value;
            }
        }

        std::cout << CYAN << "b=" << RST;
        printSemanticValue(bindState.empty() ? "?" : bindState);
        std::cout << DIM << "  " << RST << CYAN << "l=" << RST;
        printSemanticValue(listenState.empty() ? "?" : listenState);
        if (!reasonState.empty()) {
            std::cout << DIM << "  " << RST << CYAN << "r=" << RST << WHT << reasonState << RST;
        }
    } else {
        std::cout << WHT << line << RST;
    }

    std::cout << "\n";
}

static void printServerResponse(const std::string& response, const std::string& sourceCommand = "") {
    if (trim(response).empty()) {
        return;
    }

    printServerBlockHeader(sourceCommand);

    if (isShowConfigCommand(sourceCommand)) {
        std::istringstream configStream(response);
        std::string configLine;
        std::vector<KeyValueEntry> configEntries;
        while (std::getline(configStream, configLine)) {
            if (!configLine.empty() && configLine.back() == '\r') {
                configLine.pop_back();
            }

            KeyValueEntry entry;
            if (tryParseKeyValueLine(configLine, entry)) {
                configEntries.push_back(entry);
            }
        }

        printGroupedConfigTable(configEntries);
        printServerBlockFooter();
        return;
    }

    if (isShowIfaceCommand(sourceCommand)) {
        std::istringstream ifaceStream(response);
        std::string ifaceLine;
        std::vector<InterfaceStatusEntry> ifaceEntries;
        while (std::getline(ifaceStream, ifaceLine)) {
            if (!ifaceLine.empty() && ifaceLine.back() == '\r') {
                ifaceLine.pop_back();
            }

            const std::string trimmed = trim(ifaceLine);
            if (trimmed.empty()) {
                continue;
            }
            if ((startsWith(trimmed, "Name") && trimmed.find("Proto") != std::string::npos)
                || startsWith(trimmed, "----")) {
                continue;
            }
            if (trimmed.find("diag:") != std::string::npos) {
                if (!ifaceEntries.empty()) {
                    applyDiagToInterfaceStatus(ifaceEntries.back(), trimmed);
                }
                continue;
            }

            const InterfaceOverviewRow row = parseInterfaceOverviewRow(ifaceLine);
            if (row.valid) {
                ifaceEntries.push_back(makeInterfaceStatusEntry(row));
            }
        }

        printGroupedInterfaceTable(ifaceEntries);
        printServerBlockFooter();
        return;
    }

    std::istringstream stream(response);
    std::string line;
    std::vector<KeyValueEntry> keyValueTable;

    const auto flushKeyValueTable = [&]() {
        if (keyValueTable.empty()) {
            return;
        }
        printKeyValueTable(keyValueTable);
        keyValueTable.clear();
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = trim(line);
        if (trimmed.empty()) {
            flushKeyValueTable();
            std::cout << "\n";
            continue;
        }

        if (startsWith(trimmed, "vEPC restart requested")) {
            flushKeyValueTable();
            std::cout << BOLD << YEL << trimmed << RST << "\n";
            continue;
        }

        if (startsWith(trimmed, "Interface ") && trimmed.find(" status:") != std::string::npos) {
            flushKeyValueTable();
            std::cout << BOLD << MAG << trimmed << RST << "\n";
            continue;
        }

        if (trimmed.find("diag: bind=") != std::string::npos) {
            flushKeyValueTable();
            printInterfaceDiagLine(trimmed);
            continue;
        }

        if (startsWith(line, "  ")) {
            const std::string indented = trim(line);
            const size_t colonPos = indented.find(':');
            if (colonPos != std::string::npos) {
                const std::string key = trim(indented.substr(0, colonPos));
                const std::string value = trim(indented.substr(colonPos + 1));
                keyValueTable.push_back({key, value});
                continue;
            }
        }

        const InterfaceOverviewRow overviewRow = parseInterfaceOverviewRow(line);
        if (overviewRow.valid) {
            flushKeyValueTable();
            printInterfaceOverviewRow(overviewRow);
            continue;
        }

        if ((startsWith(trimmed, "Name") && trimmed.find("Proto") != std::string::npos)
            || startsWith(trimmed, "----")) {
            flushKeyValueTable();
            if (startsWith(trimmed, "Name") && trimmed.find("Proto") != std::string::npos) {
                printCompactInterfaceHeader();
            }
            continue;
        }

        const size_t eqPos = trimmed.find(" = ");
        if (eqPos != std::string::npos) {
            const std::string key = trim(trimmed.substr(0, eqPos));
            const std::string value = trim(trimmed.substr(eqPos + 3));
            keyValueTable.push_back({key, value});
            continue;
        }

        const size_t colonPos = trimmed.find(':');
        if (colonPos != std::string::npos) {
            const std::string key = trim(trimmed.substr(0, colonPos));
            const std::string value = trim(trimmed.substr(colonPos + 1));
            if (key == "Current Status") {
                flushKeyValueTable();
                std::cout << CYAN << key << RST << DIM << " : " << RST;
                printStatusSummary(value);
            } else {
                keyValueTable.push_back({key, value});
            }
            continue;
        }

        if (startsWith(trimmed, "[") && trimmed.find(']') != std::string::npos) {
            flushKeyValueTable();
            const size_t end = trimmed.find(']');
            std::cout << MAG << trimmed.substr(0, end + 1) << RST;
            if (end + 1 < trimmed.size()) {
                std::cout << " " << WHT << trim(trimmed.substr(end + 1)) << RST;
            }
            std::cout << "\n";
            continue;
        }

        if (trimmed.find("failed") != std::string::npos || trimmed.find("not running") != std::string::npos) {
            flushKeyValueTable();
            std::cout << RED << trimmed << RST << "\n";
            continue;
        }

        flushKeyValueTable();
        std::cout << WHT << trimmed << RST << "\n";
    }

    flushKeyValueTable();
    printServerBlockFooter();
}

static void loadInterfaces() {
    g_ifaces.clear();
    const std::string primary = resolveInterfacesConfPath();
    std::ifstream f(primary);
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

static bool saveInterfaces() {
    const std::string primary = resolveInterfacesConfPath();
    std::ofstream out(primary, std::ios::trunc);
    if (!out.is_open()) {
        const std::string alt = std::string("../") + INTERFACES_CONF;
        out.open(alt, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
    }

    out << "# Name      | Protocol | IP:Port        | RemoteNE\n";
    for (const auto& iface : g_ifaces) {
        out << iface.name << " | " << iface.proto << " | " << iface.ip;
        if (!iface.port.empty()) {
            out << ":" << iface.port;
        }
        out << " | " << iface.peer << "\n";
    }
    return true;
}

static void printIfaceTableCustom(const std::vector<size_t>& idxs, int& num) {
    const int wN = 3, wI = 12, wP = 8, wIP = 14, wPort = 6, wR = 16;
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
        cell(r, wR, hdr, hdr ? CYAN : peerAccentColor());
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
        {"show interface", "-", "-", "-", "Show interfaces"},
        {"restart", "-", "-", "-", "Restart server"},
        {"log", "-", "-", "-", "Show log"}
    };
    auto printCliTable = [&]() {
        const int wN = 3, wI = 12, wP = 8, wIP = 14, wPort = 6, wR = 16;
        int tableWidth = wN + wI + wP + wIP + wPort + wR + 6 * 3 + 7; // 6 columns, 7 borders
        auto hline = [&]() {
            auto seg = [](int w){ for(int i=0;i<w+2;i++) std::cout<<"-"; };
            std::cout << DIM << "+"; seg(wN); std::cout << "+";
            seg(wI); std::cout << "+";
            seg(wP); std::cout << "+";
            seg(wIP); std::cout << "+";
            seg(wPort); std::cout << "+";
            seg(wR); std::cout << "+\n" << RST;
        };
        auto titleRow = [&](const std::string& title) {
            const int innerWidth = tableWidth - 4;
            const int titleLen = static_cast<int>(title.size());
            const int leftPad = innerWidth > titleLen ? (innerWidth - titleLen) / 2 : 0;
            const int rightPad = innerWidth > titleLen ? innerWidth - titleLen - leftPad : 0;
            std::cout << DIM << "| " << RST
                      << BOLD << CYAN << std::string(static_cast<size_t>(leftPad), ' ') << title
                      << std::string(static_cast<size_t>(rightPad), ' ') << RST
                      << DIM << " |" << RST << "\n";
        };
        auto row = [&](const std::string& n, const std::string& i,
                       const std::string& p, const std::string& ip,
                       const std::string& port, const std::string& r, bool hdr = false, bool isExit = false) {
            auto cell = [&](const std::string& v, int w, bool h, const char* color = WHT) {
                std::cout << DIM << "| " << RST << color << std::left << std::setw(w) << v << RST << " ";
            };
            cell(n, wN, hdr, hdr ? CYAN : WHT);
            cell(i, wI, hdr, hdr ? CYAN : (isExit ? RED : GRN));
            cell(p, wP, hdr, hdr ? CYAN : WHT);
            cell(ip, wIP, hdr, hdr ? CYAN : WHT);
            cell(port, wPort, hdr, hdr ? CYAN : WHT);
            cell(r, wR, hdr, hdr ? CYAN : peerAccentColor());
            std::cout << DIM << "|" << RST << "\n";
        };
        hline();
        titleRow("CLI COMMANDS");
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
    // Глобальная карта: номер в таблице -> индекс в g_ifaces
    iface_num_to_idx.clear();
    auto printIfaceTableCustomCentered = [&](const std::vector<size_t>& idxs, int startNum, const std::string& title) {
        const int wN = 3, wI = 12, wP = 8, wIP = 14, wPort = 6, wR = 16;
        int tableWidth = wN + wI + wP + wIP + wPort + wR + 6 * 3 + 7;
        auto hline = [&]() {
            auto seg = [](int w){ for(int i=0;i<w+2;i++) std::cout<<"-"; };
            std::cout << DIM << "+"; seg(wN); std::cout << "+";
            seg(wI); std::cout << "+";
            seg(wP); std::cout << "+";
            seg(wIP); std::cout << "+";
            seg(wPort); std::cout << "+";
            seg(wR); std::cout << "+\n" << RST;
        };
        auto titleRow = [&](const std::string& sectionTitle) {
            const int innerWidth = tableWidth - 4;
            const int titleLen = static_cast<int>(sectionTitle.size());
            const int leftPad = innerWidth > titleLen ? (innerWidth - titleLen) / 2 : 0;
            const int rightPad = innerWidth > titleLen ? innerWidth - titleLen - leftPad : 0;
            std::cout << DIM << "| " << RST
                      << BOLD << CYAN << std::string(static_cast<size_t>(leftPad), ' ') << sectionTitle
                      << std::string(static_cast<size_t>(rightPad), ' ') << RST
                      << DIM << " |" << RST << "\n";
        };
        auto row = [&](const std::string& n, const std::string& i,
                       const std::string& p, const std::string& ip,
                       const std::string& port, const std::string& r, bool hdr = false) {
            auto cell = [&](const std::string& v, int w, bool h, const char* color = WHT) {
                std::cout << DIM << "| " << RST << color << std::left << std::setw(w) << v << RST << " ";
            };
            cell(n, wN, hdr, hdr ? CYAN : WHT);
            cell(i, wI, hdr, hdr ? CYAN : GRN);
            cell(p, wP, hdr, hdr ? CYAN : WHT);
            cell(ip, wIP, hdr, hdr ? CYAN : WHT);
            cell(port, wPort, hdr, hdr ? CYAN : WHT);
            cell(r, wR, hdr, hdr ? CYAN : peerAccentColor());
            std::cout << DIM << "|" << RST << "\n";
        };
        hline();
        titleRow(title);
        hline();
        row("No", "Name", "Protocol", "IP", "Port", "Remote NE", true);
        hline();
        int num = startNum;
        for (size_t idx : idxs) {
            iface_num_to_idx[num] = idx;
            const auto& f = g_ifaces[idx];
            row(std::to_string(num++), f.name, f.proto, f.ip, f.port, f.peer, false);
        }
        hline();
    };
    int cli_count = static_cast<int>(cli_cmds.size());
    printIfaceTableCustomCentered(sgsn_idx, cli_count, "SGSN INTERFACES");
    printIfaceTableCustomCentered(mme_idx, cli_count + static_cast<int>(sgsn_idx.size()), "MME INTERFACES");

    std::cout << "\n";
    printSectionTitle("MODE COMMANDS", 72);
    printKeyValueTable({
        {"Exec", "configure terminal | show running-config | show logging | save"},
        {"Config", "interface <name> | end | exit | commit"},
        {"Interface", "shutdown | no shutdown | default | show interface <name> detail"}
    });

    std::cout << "\n" << DIM << "Connecting to " << RST << CYAN << CLI_ENDPOINT << RST << "\n\n";
    printPrompt();
}

static void sendToServer(const std::string& cmd) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printLocalError("WSAStartup failed");
        return;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printLocalError("socket() failed");
        WSACleanup();
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(CLI_TCP_PORT);
    if (inet_pton(AF_INET, CLI_TCP_HOST, &addr.sin_addr) != 1) {
        printLocalError("inet_pton failed");
        closesocket(sock);
        WSACleanup();
        return;
    }

    bool connected = false;
    constexpr int kConnectRetryCount = 12;
    for (int attempt = 0; attempt < kConnectRetryCount; ++attempt) {
        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!connected) {
        closesocket(sock);
        WSACleanup();
        printLocalError("Cannot connect to server. Please make sure vEPC is running.");
        return;
    }
    send(sock, cmd.c_str(), static_cast<int>(cmd.size()), 0);
    std::string response;
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, static_cast<int>(sizeof(buf) - 1), 0);
        if (n <= 0) break;
        buf[n] = '\0';
        response.append(buf, static_cast<size_t>(n));
    }
    printServerResponse(response, cmd);
    closesocket(sock);
    WSACleanup();
#else
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return; }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLI_SOCKET, sizeof(addr.sun_path) - 1);

    bool connected = false;
    constexpr int kConnectRetryCount = 12;
    for (int attempt = 0; attempt < kConnectRetryCount; ++attempt) {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!connected) {
        close(sock);
        printLocalError("Cannot connect to server. Please make sure vEPC is running.");
        return;
    }
    send(sock, cmd.c_str(), cmd.size(), 0);
    std::string response;
    char buf[4096];
    while (true) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        response.append(buf, static_cast<size_t>(n));
    }
    printServerResponse(response, cmd);
    close(sock);
#endif
}

#ifndef _WIN32
/**
 * Create a VLAN sub-interface on Linux
 * Usage: create-vlan eth0 100
 */
static void handleCreateVlanInterface(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        printLocalError("Usage: create-vlan <parent-interface> <vlan-id>");
        return;
    }
    
    std::string parent = tokens[1];
    std::string vlan_str = tokens[2];
    
    try {
        int vlan_id = std::stoi(vlan_str);
        if (createVlanInterface(parent, vlan_id)) {
            printLocalInfo("VLAN interface " + parent + "." + std::to_string(vlan_id) + " created successfully");
        }
    } catch (const std::exception& e) {
        printLocalError("Failed to create VLAN: " + std::string(e.what()));
    }
}

/**
 * Delete a Linux interface
 * Usage: delete-interface eth0.100
 */
static void handleDeleteInterface(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        printLocalError("Usage: delete-interface <interface-name>");
        return;
    }
    
    std::string iface_name = tokens[1];
    if (deleteInterface(iface_name)) {
        printLocalInfo("Interface " + iface_name + " deleted successfully");
    }
}

/**
 * Bring up a Linux interface
 * Usage: up-interface eth0.100
 */
static void handleUpInterface(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        printLocalError("Usage: up-interface <interface-name>");
        return;
    }
    
    std::string iface_name = tokens[1];
    if (bringUpInterface(iface_name)) {
        printLocalInfo("Interface " + iface_name + " brought up");
    }
}

/**
 * Bring down a Linux interface
 * Usage: down-interface eth0.100
 */
static void handleDownInterface(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        printLocalError("Usage: down-interface <interface-name>");
        return;
    }
    
    std::string iface_name = tokens[1];
    if (bringDownInterface(iface_name)) {
        printLocalInfo("Interface " + iface_name + " brought down");
    }
}

/**
 * Assign IP address to a Linux interface
 * Usage: set-ip eth0.100 192.168.1.1/24
 */
static void handleSetInterfaceIp(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        printLocalError("Usage: set-ip <interface-name> <ip-address>/<prefix>");
        return;
    }
    
    std::string iface_name = tokens[1];
    std::string ip = tokens[2];
    if (setInterfaceIp(iface_name, ip)) {
        printLocalInfo("IP address " + ip + " assigned to " + iface_name);
    }
}

/**
 * List all Linux interfaces
 * Usage: list-interfaces
 */
static void handleListInterfaces(const std::vector<std::string>& tokens) {
    auto interfaces = listAllInterfaces();
    if (interfaces.empty()) {
        printLocalWarning("No interfaces found");
        return;
    }
    
    printLocalBanner("System Interfaces", "");
    for (const auto& iface : interfaces) {
        std::string status = isInterfaceUp(iface) ? "UP" : "DOWN";
        std::string ip = getInterfaceIp(iface);
        std::cout << GRN << std::left << std::setw(20) << iface << RST
                  << semanticColor(status) << std::setw(10) << status << RST
                  << (ip.empty() ? "N/A" : ip) << "\n";
    }
}
#endif

static void handleIface(const Interface& iface, const std::string& action) {
    if (action.empty() || action == "status") {
        printLocalBanner("== Interface:", iface.name);
        printKeyValueTable({
            {"Name", iface.name},
            {"Protocol", iface.proto},
            {"IP", iface.ip},
            {"Port", iface.port},
            {"Remote NE", iface.peer}
        });
        sendToServer("iface_status " + iface.name);
    } else if (action == "up" || action == "down" || action == "reset") {
        std::cout << BOLD << YEL << "Action" << RST << DIM << " : " << RST << WHT << "iface_" << action << " " << RST << GRN << iface.name << RST << "\n";
        sendToServer("iface_" + action + " " + iface.name);
    } else {
        std::cout << BOLD << RED << "Unknown action" << RST << DIM << ": " << RST << WHT << action << RST
                  << DIM << "  (allowed: up, down, reset)" << RST << "\n";
    }
}

enum class CliMode {
    Exec,
    Config,
    InterfaceConfig
};

static void printExecHelp() {
    printSectionTitle("EXEC MODE", 72);
    printKeyValueTable({
        {"Navigation", "configure terminal | exit"},
        {"Show", "show running-config | show logging | show interface [<name> [detail]] | show about"},
        {"Runtime", "status | state | restart | stop | save"},
        {"Info", "about"},
        {"Config", "set <key> <value>"},
        {"Help", "help | ?"}
    });
}

static void printConfigHelp() {
    printSectionTitle("CONFIG MODE", 72);
    printKeyValueTable({
        {"Navigation", "interface <name> | end | exit"},
        {"Runtime", "restart | stop"},
        {"Config", "set <key> <value> | hostname <name> | commit"},
        {"Show", "show running-config | show logging | show interface [<name> [detail]] | show about"},
        {"Info", "about"},
        {"Help", "help | ?"}
    });
}

static void printInterfaceHelp(const std::string& ifaceName) {
    printSectionTitle("INTERFACE MODE", 72);
    printKeyValueTable({
        {"Interface", ifaceName.empty() ? "interface <name>" : ifaceName},
        {"Actions", "shutdown | no shutdown | default | ip address <ip[/prefix]> | ip address <ip> <mask> | bind <linux-iface>"},
        {"Show", ifaceName.empty() ? "show interface <name> [detail]" : "show interface " + ifaceName + " [detail]"},
        {"Navigation", "exit | end"},
        {"Help", "help | ?"}
    });
}

static void printModeHelp(CliMode mode, const std::string& ifaceName = "") {
    if (mode == CliMode::Config) {
        printConfigHelp();
        return;
    }
    if (mode == CliMode::InterfaceConfig) {
        printInterfaceHelp(ifaceName);
        return;
    }
    printExecHelp();
}

static std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

#ifndef _WIN32
static void redrawInteractiveLine(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[2K";
    printPrompt(prompt);
    std::cout << line;
    std::cout.flush();
}

static bool startsWithCaseInsensitive(const std::string& value, const std::string& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

static std::vector<std::string> firstWordCommandsForMode(CliMode mode) {
    std::vector<std::string> out = {
        "show", "about", "status", "state", "restart", "stop", "help", "exit", "quit", "set", "save", "write"
    };

    if (mode == CliMode::Exec) {
        out.push_back("configure");
        out.push_back("conf");
    }
    if (mode == CliMode::Config) {
        out.push_back("interface");
        out.push_back("int");
        out.push_back("hostname");
        out.push_back("commit");
        out.push_back("end");
    }
    if (mode == CliMode::InterfaceConfig) {
        out.push_back("shutdown");
        out.push_back("no");
        out.push_back("ip");
        out.push_back("bind");
        out.push_back("default");
        out.push_back("end");
    }

#ifndef _WIN32
    if (mode == CliMode::Exec || mode == CliMode::Config) {
        out.push_back("create-vlan");
        out.push_back("delete-interface");
        out.push_back("up-interface");
        out.push_back("down-interface");
        out.push_back("set-ip");
        out.push_back("list-interfaces");
    }
#endif

    return out;
}

static std::vector<std::string> completionCandidates(
    CliMode mode,
    const std::string& currentLine,
    const std::string& selectedInterface
) {
    std::vector<std::string> candidates;
    std::vector<std::string> tokens = splitTokens(currentLine);
    const bool endsWithSpace = !currentLine.empty() && std::isspace(static_cast<unsigned char>(currentLine.back()));

    int tokenIndex = endsWithSpace ? static_cast<int>(tokens.size()) : static_cast<int>(tokens.size()) - 1;
    std::string prefix;
    if (!endsWithSpace && !tokens.empty()) {
        prefix = tokens.back();
    }

    const std::string t0 = tokens.size() > 0 ? toLowerCopy(tokens[0]) : "";
    const std::string t1 = tokens.size() > 1 ? toLowerCopy(tokens[1]) : "";

    if (tokenIndex <= 0) {
        candidates = firstWordCommandsForMode(mode);
    } else if (t0 == "show" && tokenIndex == 1) {
        candidates = {"running-config", "logging", "log", "interface", "iface", "config", "about"};
    } else if ((t0 == "show") && (t1 == "interface" || t1 == "iface") && tokenIndex == 2) {
        for (const auto& iface : g_ifaces) {
            candidates.push_back(iface.name);
        }
    } else if ((t0 == "interface" || t0 == "int") && tokenIndex == 1) {
        for (const auto& iface : g_ifaces) {
            candidates.push_back(iface.name);
        }
    } else if (mode == CliMode::InterfaceConfig && t0 == "no" && tokenIndex == 1) {
        candidates = {"shutdown", "ip"};
    } else if (mode == CliMode::InterfaceConfig && t0 == "no" && t1 == "ip" && tokenIndex == 2) {
        candidates = {"address"};
    } else if (mode == CliMode::InterfaceConfig && t0 == "ip" && tokenIndex == 1) {
        candidates = {"address"};
    } else if (mode == CliMode::InterfaceConfig && t0 == "bind" && tokenIndex == 1) {
        const auto systemIfaces = listAllInterfaces();
        for (const auto& iface : systemIfaces) {
            candidates.push_back(iface);
        }
    } else if (mode == CliMode::Exec && (t0 == "configure" || t0 == "conf") && tokenIndex == 1) {
        candidates = {"terminal"};
    }

    if (!selectedInterface.empty() && (t0 == "show") && (t1 == "interface" || t1 == "iface") && tokenIndex == 2) {
        candidates.push_back(selectedInterface);
    }

    std::set<std::string> dedup;
    std::vector<std::string> filtered;
    for (const auto& candidate : candidates) {
        if (!startsWithCaseInsensitive(candidate, prefix)) {
            continue;
        }
        if (dedup.insert(candidate).second) {
            filtered.push_back(candidate);
        }
    }
    return filtered;
}

#ifdef VEPC_USE_READLINE
static CliMode g_readlineMode = CliMode::Exec;
static std::string g_readlineSelectedInterface;
static std::vector<std::string> g_readlineMatches;

static char* vepcReadlineGenerator(const char* text, int state) {
    static size_t index = 0;
    if (state == 0) {
        index = 0;
    }

    while (index < g_readlineMatches.size()) {
        const std::string& candidate = g_readlineMatches[index++];
        if (startsWithCaseInsensitive(candidate, text == nullptr ? "" : text)) {
            char* result = static_cast<char*>(std::malloc(candidate.size() + 1));
            if (!result) {
                return nullptr;
            }
            std::memcpy(result, candidate.c_str(), candidate.size() + 1);
            return result;
        }
    }
    return nullptr;
}

static char** vepcReadlineCompletion(const char* text, int start, int end) {
    (void)start;
    (void)end;
    g_readlineMatches = completionCandidates(g_readlineMode, rl_line_buffer ? rl_line_buffer : "", g_readlineSelectedInterface);
    if (g_readlineMatches.empty()) {
        return nullptr;
    }

    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, vepcReadlineGenerator);
}
#endif

static bool readInteractiveLineLinux(
    const std::string& prompt,
    CliMode mode,
    const std::string& selectedInterface,
    std::string& outLine
) {
#ifdef VEPC_USE_READLINE
    g_readlineMode = mode;
    g_readlineSelectedInterface = selectedInterface;
    rl_readline_name = const_cast<char*>("vepc-cli");
    rl_attempted_completion_function = vepcReadlineCompletion;
    rl_basic_word_break_characters = const_cast<char*>(" \t\n");
    rl_completion_append_character = ' ';
    rl_bind_key('\t', rl_complete);

    char* buffer = readline(prompt.c_str());
    if (buffer == nullptr) {
        outLine.clear();
        return false;
    }

    outLine = buffer;
    if (!outLine.empty()) {
        add_history(buffer);
    }
    std::free(buffer);
    return true;
#else
    outLine.clear();

    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return static_cast<bool>(std::getline(std::cin, outLine));
    }

    termios raw = original;
    raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return static_cast<bool>(std::getline(std::cin, outLine));
    }

    auto restore = [&]() {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
    };

    while (true) {
        char ch = 0;
        const ssize_t r = read(STDIN_FILENO, &ch, 1);
        if (r <= 0) {
            restore();
            return false;
        }

        if (ch == '\n' || ch == '\r') {
            std::cout << "\n";
            restore();
            return true;
        }

        if (ch == 4) { // Ctrl+D
            if (outLine.empty()) {
                restore();
                return false;
            }
            continue;
        }

        if (ch == 3) { // Ctrl+C
            std::cout << "^C\n";
            outLine.clear();
            restore();
            return true;
        }

        if (ch == '\t') {
            const std::vector<std::string> matches = completionCandidates(mode, outLine, selectedInterface);
            if (matches.empty()) {
                continue;
            }

            std::vector<std::string> tokens = splitTokens(outLine);
            const bool endsWithSpace = !outLine.empty() && std::isspace(static_cast<unsigned char>(outLine.back()));
            if (matches.size() == 1) {
                if (endsWithSpace) {
                    outLine += matches[0] + " ";
                } else if (!tokens.empty()) {
                    const std::string prefix = tokens.back();
                    outLine += matches[0].substr(prefix.size()) + " ";
                }
                redrawInteractiveLine(prompt, outLine);
            } else {
                std::cout << "\n";
                for (const auto& m : matches) {
                    std::cout << m << "  ";
                }
                std::cout << "\n";
                redrawInteractiveLine(prompt, outLine);
            }
            continue;
        }

        if (ch == 127 || ch == 8) {
            if (!outLine.empty()) {
                outLine.pop_back();
                redrawInteractiveLine(prompt, outLine);
            }
            continue;
        }

        if (ch == 27) { // Escape sequences (arrows, etc.)
            char seq[2] = {0, 0};
            read(STDIN_FILENO, &seq[0], 1);
            read(STDIN_FILENO, &seq[1], 1);
            continue;
        }

        if (std::isprint(static_cast<unsigned char>(ch)) || ch == ' ') {
            outLine.push_back(ch);
            std::cout << ch;
            std::cout.flush();
        }
    }
#endif
}
#endif

static std::string promptForMode(CliMode mode, const std::string& ifaceName = "") {
    if (mode == CliMode::Config) {
        return g_cliHostname + "(config)# ";
    }
    if (mode == CliMode::InterfaceConfig) {
        return g_cliHostname + "(config-if-" + ifaceName + ")# ";
    }
    return g_cliHostname + "# ";
}

static bool isConfigureTerminalCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() == 2 && toLowerCopy(tokens[0]) == "configure" && toLowerCopy(tokens[1]) == "terminal") {
        return true;
    }
    return tokens.size() == 2 && toLowerCopy(tokens[0]) == "conf" && toLowerCopy(tokens[1]) == "t";
}

static bool isShowInterfaceCommand(const std::vector<std::string>& tokens) {
    return tokens.size() >= 2
        && toLowerCopy(tokens[0]) == "show"
    && (toLowerCopy(tokens[1]) == "interface" || toLowerCopy(tokens[1]) == "iface");
}

static bool isShowRunningConfigCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 2
        && toLowerCopy(tokens[0]) == "show"
        && toLowerCopy(tokens[1]) == "running-config";
}

static bool isShowLoggingCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 2
        && toLowerCopy(tokens[0]) == "show"
        && (toLowerCopy(tokens[1]) == "logging" || toLowerCopy(tokens[1]) == "log");
}

static bool isShowAboutCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 2
        && toLowerCopy(tokens[0]) == "show"
        && toLowerCopy(tokens[1]) == "about";
}

static bool isAboutCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 && toLowerCopy(tokens[0]) == "about";
}

static bool isHostnameCommand(const std::vector<std::string>& tokens) {
    return !tokens.empty() && toLowerCopy(tokens[0]) == "hostname";
}

static bool isValidHostnameToken(const std::string& value) {
    if (value.empty() || value.size() > 63) {
        return false;
    }
    if (value.front() == '-' || value.back() == '-') {
        return false;
    }
    for (char ch : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static void printSoftwareAbout() {
    printSectionTitle("ABOUT", 72);
    printKeyValueTable({
        {"Product", "vEPC"},
        {"Description", "Virtual Evolved Packet Core"},
        {"Организация", "ООО \"ЭрикссонСофт\""}
    });
}

static std::string canonicalServerCommandLabel(const std::string& sourceCommand) {
    const std::string trimmed = trim(sourceCommand);
    if (trimmed.empty()) {
        return "server output";
    }
    if (isShowIfaceCommand(trimmed)) {
        return "server output: show interface";
    }
    if (isShowConfigCommand(trimmed)) {
        return "server output: show running-config";
    }
    if (isShowLoggingCommand(splitTokens(trimmed))) {
        return "server output: show logging";
    }
    return "server output: " + trimmed;
}

static void printServerBlockHeader(const std::string& sourceCommand) {
    std::cout << "\n" << BOLD << DIM << "==== " << canonicalServerCommandLabel(sourceCommand) << " ===="
              << RST << "\n";
}

static void printServerBlockFooter() {
    std::cout << BOLD << DIM << "==== end server output ====" << RST << "\n\n";
}

static bool isSaveCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() == 1 && toLowerCopy(tokens[0]) == "save") {
        return true;
    }
    if (tokens.size() == 2 && toLowerCopy(tokens[0]) == "write") {
        const std::string noun = toLowerCopy(tokens[1]);
        return noun == "memory" || noun == "mem";
    }
    return false;
}

static bool isStatusCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 && toLowerCopy(tokens[0]) == "status";
}

static bool isStateCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 && toLowerCopy(tokens[0]) == "state";
}

static bool isRestartCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 && toLowerCopy(tokens[0]) == "restart";
}

static bool isStopCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 && toLowerCopy(tokens[0]) == "stop";
}

static bool isSetCommand(const std::vector<std::string>& tokens) {
    return tokens.size() >= 3 && toLowerCopy(tokens[0]) == "set";
}

static bool isInterfaceEnterCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() != 2) {
        return false;
    }
    const std::string head = toLowerCopy(tokens[0]);
    return head == "interface" || head == "int";
}

static bool isHelpCommand(const std::vector<std::string>& tokens) {
    return tokens.size() == 1 &&
           (toLowerCopy(tokens[0]) == "help" || toLowerCopy(tokens[0]) == "table" || tokens[0] == "?");
}

static void printModeHint() {
    printLocalInfo("Structured mode commands: configure terminal | interface <name> | hostname <name> | shutdown | no shutdown | ip address <ip[/prefix]> | bind <linux-iface> | end");
    printLocalInfo("Cisco-style commands: show [running-config | interface | status | logging | about] | about");
#ifndef _WIN32
    printLocalInfo("Linux interface commands: create-vlan <parent> <vlan-id> | delete-interface <name>");
    printLocalInfo("                          up-interface <name> | down-interface <name> | set-ip <name> <ip/prefix>");
    printLocalInfo("                          list-interfaces");
#endif
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

static int dottedMaskToPrefix(const std::string& mask);
static bool parseIpv4Token(const std::string& value);
static bool parseIpv4WithOptionalPrefix(const std::string& token, std::string& ipOut, int& prefixOut);
static bool parseVirtualIpCommandValue(const std::vector<std::string>& tokens, std::string& ipOut, int& prefixOut);
static bool isSafeLinuxInterfaceName(const std::string& value);
static std::string bindIpKeyForInterfaceName(const std::string& ifaceName);
static std::string bindInterfaceKeyForInterfaceName(const std::string& ifaceName);

int main() {
    loadInterfaces();
    const bool interactiveSession = isInteractiveSession();
#if !defined(_WIN32) && defined(VEPC_USE_READLINE)
    g_suppressPromptOutput = interactiveSession;
#else
    g_suppressPromptOutput = false;
#endif
    if (interactiveSession) {
        printHelp();
        printModeHint();
    }
    // CLI команды для сопоставления с номерами
    struct CliCmd {
        std::string name, proto, ip, port, peer;
    };
    std::vector<CliCmd> cli_cmds = {
        {"exit", "-", "-", "-", "Exit CLI"},
        {"help", "-", "-", "-", "Show help"},
        {"show config", "-", "-", "-", "Show config"},
        {"show interface", "-", "-", "-", "Show interfaces"},
        {"restart", "-", "-", "-", "Restart server"},
        {"log", "-", "-", "-", "Show log"}
    };
    CliMode mode = CliMode::Exec;
    std::string selectedInterface;
    if (interactiveSession) {
        printPrompt(promptForMode(mode, selectedInterface));
    }
    while (true) {
        std::string line;
#ifndef _WIN32
        if (interactiveSession) {
            if (!readInteractiveLineLinux(promptForMode(mode, selectedInterface), mode, selectedInterface, line)) {
                break;
            }
        } else {
            if (!std::getline(std::cin, line)) break;
        }
#else
        if (!std::getline(std::cin, line)) break;
#endif
        line = trim(line);
        if (line.empty()) { printPrompt(promptForMode(mode, selectedInterface)); continue; }
        const std::vector<std::string> tokens = splitTokens(line);

        if (isHelpCommand(tokens)) {
            if (mode == CliMode::Exec && (tokens[0] == "table" || toLowerCopy(tokens[0]) == "help")) {
                printHelp();
                if (interactiveSession) {
                    printModeHint();
                }
            } else {
                printModeHelp(mode, selectedInterface);
            }
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if (isSaveCommand(tokens)) {
            printLocalInfo("Interface admin changes are persisted automatically; no separate save step is required.");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if (mode == CliMode::Exec && isConfigureTerminalCommand(tokens)) {
            mode = CliMode::Config;
            printLocalInfo("Entered configuration mode");
            printPrompt(promptForMode(mode));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isStatusCommand(tokens)) {
            sendToServer("status");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isStateCommand(tokens)) {
            sendToServer("state");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isRestartCommand(tokens)) {
            sendToServer("restart");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isStopCommand(tokens)) {
            sendToServer("stop");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isSetCommand(tokens)) {
            sendToServer(line);
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && tokens.size() >= 1 && toLowerCopy(tokens[0]) == "set") {
            printLocalError("Usage: set <key> <value>");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isShowRunningConfigCommand(tokens)) {
            sendToServer("show config");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && (isShowAboutCommand(tokens) || isAboutCommand(tokens))) {
            printSoftwareAbout();
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isShowLoggingCommand(tokens)) {
            sendToServer("log");
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if ((mode == CliMode::Exec || mode == CliMode::Config) && isShowInterfaceCommand(tokens)) {
            if (tokens.size() == 2) {
                sendToServer("show interface");
            } else if (tokens.size() == 3 || (tokens.size() == 4 && toLowerCopy(tokens[3]) == "detail")) {
                const int idx = resolveIface(tokens[2]);
                if (idx >= 0) {
                    handleIface(g_ifaces[idx], "status");
                } else {
                    printLocalError("Unknown interface: " + tokens[2]);
                }
            } else {
                printLocalError("Usage: show interface [<name> [detail]]");
            }
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        // Linux interface management commands (when in Exec or Config mode)
#ifndef _WIN32
        if ((mode == CliMode::Exec || mode == CliMode::Config) && !tokens.empty()) {
            std::string cmd = toLowerCopy(tokens[0]);
            
            if (cmd == "create-vlan") {
                handleCreateVlanInterface(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            
            if (cmd == "delete-interface") {
                handleDeleteInterface(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            
            if (cmd == "up-interface") {
                handleUpInterface(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            
            if (cmd == "down-interface") {
                handleDownInterface(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            
            if (cmd == "set-ip") {
                handleSetInterfaceIp(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            
            if (cmd == "list-interfaces") {
                handleListInterfaces(tokens);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
        }
#endif

        if (mode == CliMode::Config && isInterfaceEnterCommand(tokens)) {
            const int idx = resolveIface(tokens[1]);
            if (idx >= 0) {
                selectedInterface = g_ifaces[idx].name;
                mode = CliMode::InterfaceConfig;
                printLocalInfo("Entered interface configuration mode: " + selectedInterface);
            } else {
                printLocalError("Unknown interface: " + tokens[1]);
            }
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        if (mode == CliMode::Config || mode == CliMode::InterfaceConfig) {
            const std::string head = tokens.empty() ? "" : toLowerCopy(tokens[0]);
            if (mode == CliMode::Config && isHostnameCommand(tokens)) {
                if (tokens.size() != 2) {
                    printLocalError("Usage: hostname <name>");
                } else if (!isValidHostnameToken(tokens[1])) {
                    printLocalError("Invalid hostname. Allowed: letters, digits, '-', max 63 chars.");
                } else {
                    g_cliHostname = tokens[1];
                    printLocalInfo("Hostname set to: " + g_cliHostname);
                }
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (head == "end") {
                mode = CliMode::Exec;
                selectedInterface.clear();
                printPrompt(promptForMode(mode));
                continue;
            }
            if (head == "exit") {
                if (mode == CliMode::InterfaceConfig) {
                    mode = CliMode::Config;
                    selectedInterface.clear();
                } else {
                    mode = CliMode::Exec;
                }
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (head == "commit") {
                printLocalInfo("Configuration is applied immediately; no separate commit is required.");
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
        }

        if (mode == CliMode::InterfaceConfig) {
            const std::string head = tokens.empty() ? "" : toLowerCopy(tokens[0]);
            if (head == "shutdown") {
                sendToServer("iface_down " + selectedInterface);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (tokens.size() == 2 && head == "no" && toLowerCopy(tokens[1]) == "shutdown") {
                sendToServer("iface_up " + selectedInterface);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (head == "ip" && tokens.size() >= 2 && toLowerCopy(tokens[1]) == "address") {
                std::string virtualIp;
                int prefix = 32;
                if (!parseVirtualIpCommandValue(tokens, virtualIp, prefix)) {
                    printLocalError("Usage: ip address <ip[/prefix]> OR ip address <ip> <netmask>");
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

                const int idx = resolveIface(selectedInterface);
                if (idx < 0) {
                    printLocalError("Unknown interface context: " + selectedInterface);
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

                g_ifaces[idx].ip = virtualIp;
                if (!saveInterfaces()) {
                    printLocalWarning("Virtual IP applied at runtime, but interfaces.conf is not writable by current user.");
                } else {
                    printLocalInfo("Virtual IP for " + selectedInterface + " set to " + virtualIp + "/" + std::to_string(prefix));
                }

                const std::string bindIpKey = bindIpKeyForInterfaceName(selectedInterface);
                if (!bindIpKey.empty()) {
                    sendToServer("set " + bindIpKey + " " + virtualIp);
                } else {
                    printLocalWarning("No runtime bind key mapping for interface " + selectedInterface + "; only interfaces.conf updated.");
                }

                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (tokens.size() == 3 && head == "no" && toLowerCopy(tokens[1]) == "ip" && toLowerCopy(tokens[2]) == "address") {
                const int idx = resolveIface(selectedInterface);
                if (idx < 0) {
                    printLocalError("Unknown interface context: " + selectedInterface);
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

                g_ifaces[idx].ip = "0.0.0.0";
                if (!saveInterfaces()) {
                    printLocalWarning("Virtual IP reset applied at runtime, but interfaces.conf is not writable by current user.");
                } else {
                    printLocalInfo("Virtual IP for " + selectedInterface + " reset to 0.0.0.0");
                }

                const std::string bindIpKey = bindIpKeyForInterfaceName(selectedInterface);
                if (!bindIpKey.empty()) {
                    sendToServer("set " + bindIpKey + " 0.0.0.0");
                }

                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (head == "bind") {
                if (tokens.size() != 2 || !isSafeLinuxInterfaceName(tokens[1])) {
                    printLocalError("Usage: bind <linux-interface>");
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

                const int idx = resolveIface(selectedInterface);
                if (idx < 0) {
                    printLocalError("Unknown interface context: " + selectedInterface);
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

                std::string virtualIp = g_ifaces[idx].ip;
                int prefix = 32;
                std::string ipOnly;
                int parsedPrefix = 32;
                if (parseIpv4WithOptionalPrefix(virtualIp, ipOnly, parsedPrefix)) {
                    virtualIp = ipOnly;
                    prefix = parsedPrefix;
                }
                if (!parseIpv4Token(virtualIp)) {
                    printLocalError("Set virtual IP first: ip address <ip[/prefix]>");
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }

#ifndef _WIN32
                if (!setInterfaceIp(tokens[1], virtualIp + "/" + std::to_string(prefix))) {
                    printLocalError("Failed to assign virtual IP to Linux interface " + tokens[1]);
                    printPrompt(promptForMode(mode, selectedInterface));
                    continue;
                }
#endif

                const std::string bindIpKey = bindIpKeyForInterfaceName(selectedInterface);
                if (!bindIpKey.empty()) {
                    sendToServer("set " + bindIpKey + " " + virtualIp);
                }
                const std::string bindIfaceKey = bindInterfaceKeyForInterfaceName(selectedInterface);
                if (!bindIfaceKey.empty()) {
                    sendToServer("set " + bindIfaceKey + " " + tokens[1]);
                }

                printLocalInfo("Bound " + selectedInterface + " to Linux interface " + tokens[1] + " using virtual IP " + virtualIp + "/" + std::to_string(prefix));
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (head == "default") {
                sendToServer("iface_reset " + selectedInterface);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            if (isShowInterfaceCommand(tokens)) {
                handleIface(g_ifaces[resolveIface(selectedInterface)], "status");
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            printLocalWarning("Unsupported interface configuration command: " + line);
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }

        std::istringstream ss(line);
        std::string token, action;
        ss >> token >> action;
        // exit по номеру или по имени
        if (token == "exit" || token == "quit" || token == "0") break;
        bool isNum = !token.empty() && token.find_first_not_of("0123456789") == std::string::npos;
        if (isNum) {
            int n = std::stoi(token);
            if (n >= 0 && n < (int)cli_cmds.size()) {
                const std::string& cmd = cli_cmds[n].name;
                if (cmd == "exit") break;
                // help теперь только локально
                else if (cmd == "help") { printHelp(); if (interactiveSession) { printModeHint(); } printPrompt(promptForMode(mode, selectedInterface)); }
                else if (cmd == "show config") sendToServer("show config");
                else if (cmd == "show interface") sendToServer("show interface");
                else if (cmd == "restart") sendToServer("restart");
                else if (cmd == "log") sendToServer("log");
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
            // Поиск интерфейса по номеру через карту
            auto it = iface_num_to_idx.find(n);
            if (it != iface_num_to_idx.end()) {
                handleIface(g_ifaces[it->second], action);
                printPrompt(promptForMode(mode, selectedInterface));
                continue;
            }
        }
        int idx = resolveIface(token);
        if (idx >= 0) {
            handleIface(g_ifaces[idx], action);
            printPrompt(promptForMode(mode, selectedInterface));
            continue;
        }
        sendToServer(line);
        printPrompt(promptForMode(mode, selectedInterface));
    }
    std::cout << "\n" << BOLD << MAG << "Exiting vEPC CLI" << RST << "\n";
    return 0;
}

static int dottedMaskToPrefix(const std::string& mask) {
    std::istringstream ss(mask);
    std::string part;
    int octets[4] = {0, 0, 0, 0};
    int idx = 0;
    while (std::getline(ss, part, '.')) {
        if (idx >= 4 || part.empty()) {
            return -1;
        }
        for (char ch : part) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return -1;
            }
        }
        int value = 0;
        try {
            value = std::stoi(part);
        } catch (...) {
            return -1;
        }
        if (value < 0 || value > 255) {
            return -1;
        }
        octets[idx++] = value;
    }
    if (idx != 4) {
        return -1;
    }

    uint32_t maskValue = 0;
    for (int i = 0; i < 4; ++i) {
        maskValue = (maskValue << 8) | static_cast<uint32_t>(octets[i]);
    }

    int prefix = 0;
    bool seenZero = false;
    for (int bit = 31; bit >= 0; --bit) {
        const bool one = ((maskValue >> bit) & 1u) != 0;
        if (one) {
            if (seenZero) {
                return -1;
            }
            ++prefix;
        } else {
            seenZero = true;
        }
    }
    return prefix;
}

static bool parseIpv4Token(const std::string& value) {
    std::istringstream ss(value);
    std::string part;
    int octetCount = 0;
    while (std::getline(ss, part, '.')) {
        if (part.empty() || part.size() > 3) {
            return false;
        }
        for (char ch : part) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        int n = 0;
        try {
            n = std::stoi(part);
        } catch (...) {
            return false;
        }
        if (n < 0 || n > 255) {
            return false;
        }
        ++octetCount;
    }
    return octetCount == 4;
}

static bool parseIpv4WithOptionalPrefix(const std::string& token, std::string& ipOut, int& prefixOut) {
    const size_t slash = token.find('/');
    if (slash == std::string::npos) {
        if (!parseIpv4Token(token)) {
            return false;
        }
        ipOut = token;
        prefixOut = 32;
        return true;
    }

    const std::string ipPart = token.substr(0, slash);
    const std::string prefixPart = token.substr(slash + 1);
    if (!parseIpv4Token(ipPart) || prefixPart.empty()) {
        return false;
    }
    for (char ch : prefixPart) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    int prefix = -1;
    try {
        prefix = std::stoi(prefixPart);
    } catch (...) {
        return false;
    }
    if (prefix < 0 || prefix > 32) {
        return false;
    }
    ipOut = ipPart;
    prefixOut = prefix;
    return true;
}

static bool parseVirtualIpCommandValue(const std::vector<std::string>& tokens, std::string& ipOut, int& prefixOut) {
    if (tokens.size() == 3) {
        return parseIpv4WithOptionalPrefix(tokens[2], ipOut, prefixOut);
    }
    if (tokens.size() == 4) {
        if (!parseIpv4Token(tokens[2])) {
            return false;
        }
        const int prefix = dottedMaskToPrefix(tokens[3]);
        if (prefix < 0) {
            return false;
        }
        ipOut = tokens[2];
        prefixOut = prefix;
        return true;
    }
    return false;
}

static bool isSafeLinuxInterfaceName(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' || ch == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static std::string bindIpKeyForInterfaceName(const std::string& ifaceName) {
    const std::string lower = toLowerCopy(ifaceName);
    if (lower.rfind("s1", 0) == 0) {
        return "s1ap-bind-ip";
    }
    if (lower.rfind("s11", 0) == 0) {
        return "s11-bind-ip";
    }
    if (lower.rfind("gn", 0) == 0 || lower.rfind("gp", 0) == 0) {
        return "gn-gtp-u-bind-ip";
    }
    return "";
}

static std::string bindInterfaceKeyForInterfaceName(const std::string& ifaceName) {
    const std::string lower = toLowerCopy(ifaceName);
    if (lower.rfind("s1", 0) == 0) {
        return "s1ap-bind-iface";
    }
    if (lower.rfind("s11", 0) == 0) {
        return "s11-bind-iface";
    }
    if (lower.rfind("gn", 0) == 0 || lower.rfind("gp", 0) == 0) {
        return "gn-gtp-u-bind-iface";
    }
    return "";
}