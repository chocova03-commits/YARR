#ifndef YARR_LOGGING_CONFIG_H
#define YARR_LOGGING_CONFIG_H

#include <string>
#include "storage.hpp"

namespace logging {

struct LoggingLoggersConfig {
  std::string name;
  std::string level; // Optional
  std::string sink; // Optional
};

struct LoggingSinkConfig {
  std::string name;
  std::string file_name;
  std::string level; // Optional
  std::string pattern; // Optional
};

/**
 * Top-level configuration.
 */
struct LoggingConfig {
  /// Level for default sink output (optional)
  std::string level{};
  /// Configuration selecting loggers (optional)
  std::vector<LoggingLoggersConfig> log_config{};
  /// Configuration of default pattern (optional)
  std::string pattern{};
  /// If set, report logger configuration at the end
  bool report_loggers{false};
  /// Configuration of logging sinks
  std::vector<LoggingSinkConfig> sinks{};
  /// Name type for default sink
  std::string default_sink{};
  /// If default_sink is 'ringbuffer', set depth
  size_t ringbuffer_size{};
  /// If true, report only log contents
  bool simple{};
};

constexpr const char *defaultLogPattern = "[%T:%e]%^[%=8l][%=15n][%t]:%$ %v";

/// Build default logging config.
/**
 * If configs/logging/default.json exists, this is used.
 * Otherwise create simple config.
 */
LoggingConfig defaultConfig();

/// Setup loggers according to configuration in json file
void setupLoggers(const json &j, const std::string &path="");

/// Add a stdout.log file sink to every logger, using the same level and
/// pattern as the terminal sink, writing into outputDir (must already
/// exist and end in '/'). Effectively a file copy of the terminal log.
void addOutputFileSink(const std::string &outputDir);

/// List loggers to std::cout, with details of sinks and levels
void listLoggers(bool print_details = false);

// get messages from ringbuffer sink
std::vector<std::string> getLog(size_t lim);

// Serialisation

void to_json(json& j, const LoggingLoggersConfig& c);
void from_json(const json& j, LoggingLoggersConfig& c);
void to_json(json& j, const LoggingSinkConfig& c);
void from_json(const json& j, LoggingSinkConfig& c);
void to_json(json& j, const LoggingConfig& c);
void from_json(const json& j, LoggingConfig& c);

}

#endif
