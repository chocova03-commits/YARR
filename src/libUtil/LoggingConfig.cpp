#include "LoggingConfig.h"

#include "LoadJson.h"
#include "PirateSpeak.h"

#include <filesystem>
#include <optional>
#include <iostream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/ringbuffer_sink.h"
#include "spdlog/pattern_formatter.h"

#include "ProgressAwareSink.h"

namespace {
  // Drop ANSI CSI escape sequences (e.g. "\033[1;33m" ... "\033[0m" SGR color
  // codes) from formatted output. Lets messages carry hand-placed terminal
  // highlighting (which real color sinks pass straight through) without that
  // highlighting leaking as raw escape bytes into plain sinks such as files.
  void stripAnsiCodes(const spdlog::memory_buf_t &in, spdlog::memory_buf_t &out) {
    for(size_t i = 0; i < in.size(); i++) {
      if(in[i] == '\033' && i+1 < in.size() && in[i+1] == '[') {
        size_t j = i + 2;
        while(j < in.size() && (in[j] == ';' || (in[j] >= '0' && in[j] <= '9'))) {
          j++;
        }
        // Final byte of a CSI sequence is in the range 0x40-0x7E
        if(j < in.size() && in[j] >= 0x40 && in[j] <= 0x7E) {
          i = j; // Skip the whole escape sequence
          continue;
        }
      }
      out.push_back(in[i]);
    }
  }

  class AnsiStrippingFormatter : public spdlog::formatter {
  public:
    explicit AnsiStrippingFormatter(std::unique_ptr<spdlog::formatter> inner)
      : m_inner(std::move(inner)) {}

    void format(const spdlog::details::log_msg &msg, spdlog::memory_buf_t &dest) override {
      spdlog::memory_buf_t formatted;
      m_inner->format(msg, formatted);
      stripAnsiCodes(formatted, dest);
    }

    std::unique_ptr<spdlog::formatter> clone() const override {
      return std::make_unique<AnsiStrippingFormatter>(m_inner->clone());
    }

  private:
    std::unique_ptr<spdlog::formatter> m_inner;
  };
}

namespace logging {
void to_json(json& j, const LoggingLoggersConfig& c) {
  j = json{{"name", c.name}};
  if(!c.level.empty()) {
    j["level"] = c.level;
  }
  if(!c.sink.empty()) {
    j["sink"] = c.sink;
  }
}

void from_json(const json& j, LoggingLoggersConfig& c) {
  if(!j.contains("name")) {
    spdlog::error("Log json file: 'log_config' list item must have 'name");
    throw std::runtime_error("Missing 'log_config'");
  }
  j.at("name").get_to(c.name);
  if(j.contains("level")) {
    j.at("level").get_to(c.level);
  }
  if(j.contains("sink")) {
    j.at("sink").get_to(c.sink);
  }
}

void to_json(json& j, const LoggingSinkConfig& c) {
  j = json{{"name", c.name},
           {"file_name", c.file_name}};
  if(!c.level.empty()) {
    j["level"] = c.level;
  }
  if(!c.pattern.empty()) {
    j["pattern"] = c.pattern;
  }
}

void from_json(const json& j, LoggingSinkConfig& c) {
  if(!j.contains("name")) {
    spdlog::warn("Log json file: missing 'name' field for additional sink configuration");
    throw std::runtime_error("Missing 'name'");
  }
  if(!j.contains("file_name")) {
    spdlog::warn("Log json file: missing 'file_name' field for additional sink configuration");
    throw std::runtime_error("Missing 'file_name'");
  }
  j.at("name").get_to(c.name);
  j.at("file_name").get_to(c.file_name);
  if(j.contains("level")) {
    j.at("level").get_to(c.level);
  }
  if(j.contains("pattern")) {
    j.at("pattern").get_to(c.pattern);
  }
}

void to_json(json& j, const LoggingConfig& c) {
  // All optional
  j = json::object();
  if(!c.level.empty()) {
    j["level"] = c.level;
  }
  if(!c.log_config.empty()) {
    j["log_config"] = c.log_config;
  }
  if(!c.pattern.empty()) {
    j["pattern"] = c.pattern;
  }
  if(c.report_loggers) {
    j["report_loggers"] = c.report_loggers;
  }
  if(!c.sinks.empty()) {
    j["sinks"] = c.sinks;
  }
  if(!c.default_sink.empty()) {
    j["default_sink"] = c.default_sink;
  }
  if(c.ringbuffer_size != 0) {
    j["ringbuffer_size"] = c.ringbuffer_size;
  }
  if(c.simple) {
    j["simple"] = c.simple;
  }
}

void from_json(const json& j, LoggingConfig& c) {
  if(j.contains("level")) {
    j.at("level").get_to(c.level);
  }
  if(j.contains("log_config")) {
    j.at("log_config").get_to(c.log_config);
  }
  if(j.contains("pattern")) {
    j.at("pattern").get_to(c.pattern);
  }
  if(j.contains("report_loggers")) {
    j.at("report_loggers").get_to(c.report_loggers);
  }
  if(j.contains("sinks")) {
    j.at("sinks").get_to(c.sinks);
  }
  if(j.contains("default_sink")) {
    j.at("default_sink").get_to(c.default_sink);
  }
  if(j.contains("ringbuffer_size")) {
    j.at("ringbuffer_size").get_to(c.ringbuffer_size);
  }
  if(j.contains("simple")) {
    j.at("simple").get_to(c.simple);
  }
}

} // end namespace logging

// spdlog::level::level_enum, but then construction doesn't work?
static const std::map<std::string, int> level_map = {
  {"off", SPDLOG_LEVEL_OFF},
  {"critical", SPDLOG_LEVEL_CRITICAL},
  {"err", SPDLOG_LEVEL_ERROR},
  {"error", SPDLOG_LEVEL_ERROR},
  {"warn", SPDLOG_LEVEL_WARN},
  {"warning", SPDLOG_LEVEL_WARN},
  {"info", SPDLOG_LEVEL_INFO},
  {"debug", SPDLOG_LEVEL_DEBUG},
  {"trace", SPDLOG_LEVEL_TRACE},
};

// Build a formatter for `pattern`, routing the message payload ('%v')
// through piratify() and prepending a random quip ('%j'), when pirate mode
// has been enabled via the secret pass-phrase.
static std::unique_ptr<spdlog::formatter> makeFormatter(const std::string &pattern) {
    if (logging::pirateModeEnabled()) {
        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<logging::PirateVFormatter>('v');
        formatter->add_flag<logging::PirateQuipFormatter>('j');
        formatter->set_pattern(logging::insertQuipFlag(pattern));
        return formatter;
    }
    return std::make_unique<spdlog::pattern_formatter>(pattern);
}

static void applyPattern(spdlog::sink_ptr &sink, const std::string &pattern) {
    sink->set_formatter(makeFormatter(pattern));
}

static std::string_view level_string(int lvl) {
  auto i = std::find_if(level_map.begin(), level_map.end(),
                        [lvl](const auto &it)
                        { return it.second == lvl;}
                        );
  if(i == level_map.end()) {
    return "";
  }
  return i->first;
}

namespace logging {

json defaultConfig() {
    std::string default_name = "configs/logging/default.json";
    if(std::filesystem::exists(default_name)) {
      return JsonHelper::openJsonFile(default_name);
    }
    json j;
    j["pattern"] = defaultLogPattern;
    j["log_config"][0]["name"] = "all";
    j["log_config"][0]["level"] = "info";
    return j;
}

spdlog::sink_ptr default_sink;

void setupLoggers(const LoggingConfig &j, const std::string &path) {
    static bool initialized = false;

    // initialized logger only once
    if(initialized) return;

    if(j.default_sink == "ringbuffer") {
        std::size_t ringbuffer_size = 1000;
        if(j.ringbuffer_size) ringbuffer_size = j.ringbuffer_size;
        default_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(ringbuffer_size);
    }
    else default_sink = std::make_shared<ProgressAwareSink>(
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    std::map<std::string, spdlog::sink_ptr> other_sinks;

    std::string default_pattern = "";

    // Don't print log level and timestamp
    if(j.simple) {
        default_pattern = "%v";
    }

    if(!j.level.empty()) {
        std::string level_name = j.level;
        default_sink->set_level((spdlog::level::level_enum)level_map.at(level_name));
    }

    // NB this sets things at the sink level, so not specifying a particular logger...
    // Also doing it last means it applies to all registered sinks
    if(!j.pattern.empty()) {
        default_pattern = j.pattern;
    }

    // This should be overriden by what's done below, but here just in case
    spdlog::set_pattern(default_pattern);

    for(auto &jl: j.sinks) {
        std::string key = jl.name;
        std::string fname = path + std::string(jl.file_name);

        auto new_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fname);

        if(!jl.level.empty()) {
            std::string level_name = jl.level;
            new_sink->set_level((spdlog::level::level_enum)level_map.at(level_name));
        }

        std::string pattern = default_pattern;

        if(!jl.pattern.empty()) {
          pattern = jl.pattern;
        }

        // This is a file sink, so never let raw terminal color escapes
        // (hand-placed at call sites) leak into it, on top of the usual
        // pirate-mode-aware pattern.
        new_sink->set_formatter(
            std::make_unique<AnsiStrippingFormatter>(makeFormatter(pattern)));
        other_sinks[key] = new_sink;
    }

    if(!default_pattern.empty()) {
      applyPattern(default_sink, default_pattern);
    }

    for(auto &jl: j.log_config) {
      std::string name = jl.name;

      std::optional<spdlog::level::level_enum> opt_level;
      if(!jl.level.empty()) {
        std::string level_name = jl.level;

        opt_level = (spdlog::level::level_enum)level_map.at(level_name);
      }

      auto sink = default_sink;
      if(!jl.sink.empty()) {
        std::string sink_name = jl.sink;
        sink = other_sinks[sink_name];
      }

      auto logger_apply = [&](const std::shared_ptr<spdlog::logger>& l) {
        // Only if this sink is not already in list
        bool already_present = false;
        for(const auto& ll: l->sinks()) {
          if(ll == sink) {
            already_present = true;
          }
        }

        if(!already_present) {
          l->sinks().push_back(sink);
        }
        if(opt_level) {
          l->set_level(*opt_level);
        }
      };

      if(name == "all") {
        spdlog::apply_all(logger_apply);
      } else {
        auto l = spdlog::get(name);
        if(!l) {
          spdlog::warn("Log json file: logger '{}' doesn't match any known loggers", name);
        } else {
          logger_apply(l);
        }
      }
    }

    // Remove the initial sink from the default logger
    auto default_logger = spdlog::default_logger();
    auto &old_sinks = default_logger->sinks();
    std::remove_reference<decltype(old_sinks)>::type new_sinks;
    for(size_t i=1; i<old_sinks.size(); i++) {
      new_sinks.push_back(old_sinks[i]);
    }
    old_sinks = new_sinks;    

    // By this point the default logger should have been configured appropriately
    //  so this won't appear unless requested
    spdlog::trace("Log json file: A quick message using the default logger");

    if(j.report_loggers) {
      listLoggers(true);
    }

    initialized =  true;
}

void setupLoggers(const json &j, const std::string &path) {
    try {
      LoggingConfig ll = j.get<LoggingConfig>();
      setupLoggers(ll, path);
    } catch(std::exception &ex) {
      std::cerr << "Failed to load configuration from json\n";
      std::cerr << " -> " << ex.what() << "\n";
      return;
    }
}

void listLoggers(bool print_details) {
    std::string_view def_name = "(default)";
    std::vector<std::string> log_list;
    spdlog::apply_all([&](const std::shared_ptr<spdlog::logger>& l) {
        if(l->name().empty()) {
            log_list.push_back("(default)");
        } else {
            log_list.push_back(l->name());
        }
    });

    std::sort(log_list.begin(), log_list.end());

    for(const auto& l: log_list) {
        std::cout << "  " << l << "\n";

        if(!print_details)
          continue;

        std::string ll = l;
        if(l == def_name) {
          ll = "";
        }

        auto curr = spdlog::get(ll);
        std::cout << "    Sends to sinks at level " << curr->level();
        auto lvl = level_string(curr->level());
        if(!lvl.empty()) {
          std::cout << " (" << lvl << ")\n";
        } else {
          std::cout << "\n";
        }
        std::cout << "    Has " << curr->sinks().size() << " sinks\n";

        for(const auto& s: curr->sinks()) {
          auto lvl = level_string(s->level());
          std::cout << "      Reports at level " << s->level();
          if(!lvl.empty()) {
            std::cout << " (" << lvl << ")\n";
          } else {
            std::cout << "\n";
          }
        }
    }
}

std::vector<std::string> getLog(size_t lim) {
  std::vector<std::string> result;
  auto *sink=dynamic_cast<spdlog::sinks::ringbuffer_sink_mt *>(default_sink.get());
  if(sink) result=sink->last_formatted(lim);
  return result;
}

} // End namespace logging
