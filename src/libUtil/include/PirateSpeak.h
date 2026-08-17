#ifndef YARR_PIRATE_SPEAK_H
#define YARR_PIRATE_SPEAK_H

#include <string>

#include "spdlog/pattern_formatter.h"

namespace logging {

/// Enable/disable the pirate-speak log translation easter egg.
/// Off by default; only turned on by an exact secret pass-phrase match.
void setPirateMode(bool enabled);
bool pirateModeEnabled();

/// Translate a log message into pirate-speak via word substitution.
/// Unknown words, numbers, and identifiers pass through unchanged.
std::string piratify(const std::string &msg);

/// Insert the '%j' quip flag immediately before every '%v' in a spdlog
/// pattern string, so the quip formatter renders just before the message.
std::string insertQuipFlag(const std::string &pattern);

/// spdlog custom flag formatter: renders the '%v' (message payload) flag
/// through piratify() instead of verbatim, when pirate mode is enabled.
class PirateVFormatter : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &msg, const std::tm &tm_time,
                spdlog::memory_buf_t &dest) override;
    std::unique_ptr<custom_flag_formatter> clone() const override;
};

/// spdlog custom flag formatter bound to '%j': prepends a short random
/// pirate interjection ("Arrr!", "Avast!", ...) to most (not all) lines.
class PirateQuipFormatter : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &msg, const std::tm &tm_time,
                spdlog::memory_buf_t &dest) override;
    std::unique_ptr<custom_flag_formatter> clone() const override;
};

} // namespace logging

#endif
