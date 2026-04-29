// imsi_module.h - simple IMSI range/series manager
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace vepc {

enum class ImsiKind { Range, Series };

struct ImsiSpec {
    std::vector<std::string> plmns; // one or more MCC+MNC values (5 or 6 digits)
    ImsiKind kind;
    // Range
    uint64_t start_msin = 0;
    uint64_t end_msin = 0;
    // Series
    std::string prefix;
    uint64_t count = 0;

    bool valid() const;
};

class ImsiModule {
public:
    ImsiModule() = default;

    // plmnList: single PLMN or comma-separated list of PLMNs
    bool addRange(const std::string& plmnList, uint64_t start_msin, uint64_t end_msin, std::string& err);
    bool addSeries(const std::string& plmnList, const std::string& prefix, uint64_t count, std::string& err);

    // If plmn empty -> all; otherwise returns specs that include this plmn
    std::vector<ImsiSpec> list(const std::string& plmn = "") const;

    void generateForSpec(const ImsiSpec& spec, std::function<void(const std::string&)> cb) const;

    static bool validatePlmn(const std::string& plmn);
    static size_t msinLengthForPlmn(const std::string& plmn);

private:
    std::vector<ImsiSpec> specs_;
};

void registerImsiCliCommands();

} // namespace vepc
