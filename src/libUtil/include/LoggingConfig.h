#ifndef YARR_LOGGING_CONFIG_H
#define YARR_LOGGING_CONFIG_H

#include <string>
#include "storage.hpp"

namespace logging {

constexpr const char *defaultLogPattern = "[%T:%e]%^[%=8l][%=15n][%t]:%$ %v";

/// Build default logging config.
/**
 * If configs/logging/default.json exists, this is used.
 * Otherwise create simple config.
 */
json defaultConfig();

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
}

#endif
