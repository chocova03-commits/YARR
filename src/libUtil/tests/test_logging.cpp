#include "catch.hpp"

#include "LoggingConfig.h"

// Check that converting json string to object and back
// results in the same data
void check_ping_pong_top(std::string jstr) {
  auto input = json::parse(jstr);

  auto obj = input.get<logging::LoggingConfig>();

  json output = obj;

  CHECK (input == output);
}

void check_ping_pong_sink(std::string jstr) {
  auto input = json::parse(jstr);

  auto obj = input.get<logging::LoggingSinkConfig>();

  json output = obj;

  CHECK (input == output);
}

void check_ping_pong_logger(std::string jstr) {
  auto input = json::parse(jstr);

  auto obj = input.get<logging::LoggingLoggersConfig>();

  json output = obj;

  CHECK (input == output);
}

TEST_CASE("Check top-level LoggingConfig", "[logging][serialization]") {
  check_ping_pong_top(R"(
    {}
  )");

  check_ping_pong_top(R"(
    {
      "level": "info",
      "pattern": "TEST",
      "report_loggers": true,
      "default_sink": "other_name",
      "ringbuffer_size": 100,
      "simple": true
    }
  )");

  check_ping_pong_top(R"(
    {
      "log_config": [
        {"name": "test_log_name"}
      ]
    }
  )");
  check_ping_pong_top(R"(
    {
      "sinks": [
        {"name": "test_sink_name", "file_name": "test_output"}
      ]
    }
  )");
}

TEST_CASE("Check sinks LoggingSinkConfig", "[logging][serialization]") {
  // Minimal
  check_ping_pong_sink(R"(
    {"name": "test_sink_name", "file_name": "test_output"}
  )");

  check_ping_pong_sink(R"(
    {
      "name": "test_sink_name",
      "file_name": "test_output",
      "level": "info",
      "pattern": "TEST"
    }
  )");
}

TEST_CASE("Check loggers LoggingLoggersConfig", "[logging][serialization]") {
  // Minimal
  check_ping_pong_logger(R"(
    {"name": "test_sink_name"}
  )");

  check_ping_pong_logger(R"(
    {
      "name": "test_sink_name",
      "level": "info",
      "sink": "TEST_sink"
    }
  )");
}
