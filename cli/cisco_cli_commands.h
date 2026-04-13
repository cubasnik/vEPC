#ifndef CISCO_CLI_COMMANDS_H
#define CISCO_CLI_COMMANDS_H

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>

/**
 * @brief Расширенные Cisco-стиль команды для vEPC CLI
 */

enum class CiscoCliMode {
    Exec,                    // Пользовательский режим (#)
    Config,                  // Режим конфигурации (config)#
    InterfaceConfig,         // Режим конфигурации интерфейса (config-if)#
    ContextConfig,           // Режим конфигурации контекста (config-ctx)#
    CardConfig,              // Режим конфигурации карты (config-card)#
};

struct CiscoCommand {
    std::string name;
    std::string description;
    std::string usage;
    CiscoCliMode min_mode;  // минимальный режим для выполнения команды
};

// Базовые Cisco-стиль команды в режиме Exec
static const std::vector<CiscoCommand> EXEC_MODE_COMMANDS = {
    {"show running-config", "Display the operating configuration", "show running-config", CiscoCliMode::Exec},
    {"show config", "Display the operating configuration (alias)", "show config", CiscoCliMode::Exec},
    {"show interface", "Display interface information", "show interface [<name>]", CiscoCliMode::Exec},
    {"show context", "Display context information", "show context [<name>]", CiscoCliMode::Exec},
    {"show card", "Display card/board information", "show card [<slot>]", CiscoCliMode::Exec},
    {"show status", "Display system status", "show status", CiscoCliMode::Exec},
    {"show logging", "Display system logs", "show logging", CiscoCliMode::Exec},
    {"show version", "Display system version", "show version", CiscoCliMode::Exec},
    {"clear logging", "Clear system logs", "clear logging", CiscoCliMode::Exec},
    {"configure terminal", "Enter configuration mode", "configure terminal", CiscoCliMode::Exec},
    {"exit", "Exit the CLI", "exit", CiscoCliMode::Exec},
    {"quit", "Exit the CLI (alias)", "quit", CiscoCliMode::Exec},
    {"help", "Display help information", "help", CiscoCliMode::Exec},
    {"?", "Display help information (alias)", "?", CiscoCliMode::Exec},
};

// Команды доступные в режиме Config и выше
static const std::vector<CiscoCommand> CONFIG_MODE_COMMANDS = {
    {"interface", "Enter interface configuration mode", "interface <name>", CiscoCliMode::Config},
    {"context", "Enter context configuration mode", "context <name>", CiscoCliMode::Config},
    {"card", "Enter card configuration mode", "card <slot>", CiscoCliMode::Config},
    {"ntp", "NTP configuration", "ntp <subcommand>", CiscoCliMode::Config},
    {"ssh", "SSH configuration", "ssh <subcommand>", CiscoCliMode::Config},
    {"aaa", "AAA configuration", "aaa <subcommand>", CiscoCliMode::Config},
    {"hostname", "Set system hostname", "hostname <name>", CiscoCliMode::Config},
    {"clock", "Clock configuration", "clock <subcommand>", CiscoCliMode::Config},
    {"exit", "Exit configuration mode", "exit", CiscoCliMode::Config},
    {"end", "Exit to exec mode", "end", CiscoCliMode::Config},
    {"no", "Negate a command", "no <command>", CiscoCliMode::Config},
};

// Команды интерфейса
static const std::vector<CiscoCommand> INTERFACE_CONFIG_COMMANDS = {
    {"shutdown", "Disable the interface", "shutdown", CiscoCliMode::InterfaceConfig},
    {"no shutdown", "Enable the interface", "no shutdown", CiscoCliMode::InterfaceConfig},
    {"description", "Set interface description", "description <text>", CiscoCliMode::InterfaceConfig},
    {"ip address", "Set interface IP address", "ip address <ip> <netmask>", CiscoCliMode::InterfaceConfig},
    {"no ip address", "Remove IP address", "no ip address", CiscoCliMode::InterfaceConfig},
    {"duplex", "Set duplex mode", "duplex [auto|full|half]", CiscoCliMode::InterfaceConfig},
    {"speed", "Set interface speed", "speed <speed>", CiscoCliMode::InterfaceConfig},
    {"mtu", "Set MTU size", "mtu <size>", CiscoCliMode::InterfaceConfig},
    {"enable-acl", "Enable ACL on interface", "enable-acl <acl-name>", CiscoCliMode::InterfaceConfig},
    {"bind", "Bind to physical interface", "bind <parent-if>", CiscoCliMode::InterfaceConfig},
    {"exit", "Exit interface configuration", "exit", CiscoCliMode::InterfaceConfig},
    {"end", "Exit to exec mode", "end", CiscoCliMode::InterfaceConfig},
};

// Функции для парсинга и проверки команд

static inline std::string toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

static inline std::vector<std::string> tokenize(const std::string& str) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if ((c == ' ' || c == '\t') && !inQuotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

/**
 * Получить приглашение (prompt) для текущего режима
 */
static inline std::string getPrompt(CiscoCliMode mode, const std::string& context = "") {
    if (context.empty()) {
        switch (mode) {
            case CiscoCliMode::Exec: return "vepc# ";
            case CiscoCliMode::Config: return "vepc(config)# ";
            case CiscoCliMode::InterfaceConfig: return "vepc(config-if)# ";
            case CiscoCliMode::ContextConfig: return "vepc(config-ctx)# ";
            case CiscoCliMode::CardConfig: return "vepc(config-card)# ";
        }
    } else {
        switch (mode) {
            case CiscoCliMode::Exec: return "vepc# ";
            case CiscoCliMode::Config: return "vepc(config)# ";
            case CiscoCliMode::InterfaceConfig: return "vepc(config-if-" + context + ")# ";
            case CiscoCliMode::ContextConfig: return "vepc(config-ctx-" + context + ")# ";
            case CiscoCliMode::CardConfig: return "vepc(config-card-" + context + ")# ";
        }
    }
    return "vepc# ";
}

/**
 * Проверить, является ли команда командой "configure terminal"
 */
static inline bool isConfigureTerminal(const std::vector<std::string>& tokens) {
    if (tokens.size() == 2) {
        return toLowerCase(tokens[0]) == "configure" && toLowerCase(tokens[1]) == "terminal";
    }
    return false;
}

/**
 * Проверить, является ли команда командой "show running-config" / "show config"
 */
static inline bool isShowRunningConfig(const std::vector<std::string>& tokens) {
    if (tokens.size() == 2) {
        std::string cmd0 = toLowerCase(tokens[0]);
        std::string cmd1 = toLowerCase(tokens[1]);
        return cmd0 == "show" && (cmd1 == "running-config" || cmd1 == "config");
    }
    return false;
}

/**
 * Проверить, является ли команда командой "show interface"
 */
static inline bool isShowInterface(const std::vector<std::string>& tokens) {
    if (!tokens.empty()) {
        return toLowerCase(tokens[0]) == "show" && 
               (tokens.size() == 1 || 
                (tokens.size() >= 2 && toLowerCase(tokens[1]) == "interface") ||
                (tokens.size() >= 2 && toLowerCase(tokens[1]) == "iface"));
    }
    return false;
}

/**
 * Проверить, является ли команда командой выхода (exit/end)
 */
static inline bool isExitCommand(const std::vector<std::string>& tokens) {
    if (!tokens.empty()) {
        std::string cmd = toLowerCase(tokens[0]);
        return cmd == "exit" || cmd == "end" || cmd == "quit";
    }
    return false;
}

/**
 * Проверить, является ли команда "interface"
 */
static inline bool isInterfaceCommand(const std::vector<std::string>& tokens) {
    return tokens.size() >= 2 && toLowerCase(tokens[0]) == "interface";
}

/**
 * Проверить, является ли команда "shutdown" / "no shutdown"
 */
static inline bool isShutdownCommand(const std::vector<std::string>& tokens) {
    if (tokens.size() == 1 && toLowerCase(tokens[0]) == "shutdown") {
        return true;
    }
    if (tokens.size() == 2 && toLowerCase(tokens[0]) == "no" && toLowerCase(tokens[1]) == "shutdown") {
        return true;
    }
    return false;
}

/**
 * Проверить, является ли команда "no"  
 */
static inline bool isNoCommand(const std::vector<std::string>& tokens) {
    return !tokens.empty() && toLowerCase(tokens[0]) == "no";
}

#endif // CISCO_CLI_COMMANDS_H
