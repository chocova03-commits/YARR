#include "PirateSpeak.h"

#include <array>
#include <cctype>
#include <random>
#include <unordered_map>

namespace logging {

namespace {
bool g_pirateMode = false;

const std::unordered_map<std::string, std::string> &wordMap() {
    static const std::unordered_map<std::string, std::string> map = {
        {"my", "me"},
        {"is", "be"},
        {"are", "be"},
        {"you", "ye"},
        {"your", "yer"},
        {"the", "th'"},
        {"hello", "ahoy"},
        {"hi", "ahoy"},
        {"welcome", "avast"},
        {"yes", "aye"},
        {"ok", "aye aye"},
        {"okay", "aye aye"},
        {"error", "blunder"},
        {"errors", "blunders"},
        {"failed", "scuttled"},
        {"failure", "scuttlin'"},
        {"warning", "shiver me timbers"},
        {"success", "treasure"},
        {"successfully", "shipshape"},
        {"stop", "belay"},
        {"stopping", "belayin'"},
        {"stopped", "belayed"},
        {"start", "weigh anchor"},
        {"starting", "settin' sail"},
        {"started", "set sail"},
        {"scan", "voyage"},
        {"scanning", "voyagin'"},
        {"finished", "made port"},
        {"finish", "make port"},
        {"done", "made port"},
        {"friend", "matey"},
        {"friends", "mateys"},
        {"chip", "plank"},
        {"chips", "planks"},
        {"data", "booty"},
        {"file", "chart"},
        {"files", "charts"},
        {"config", "logbook"},
        {"configuration", "logbook"},
        {"here", "'ere"},
        {"here's", "'ere be"},
        {"there", "thar"},
        {"money", "doubloons"},
        {"good", "grand"},
        // Frequent words observed in real scan logs (loop/mask/reset chatter,
        // controller and file-loading boilerplate), mapped to nautical puns.
        {"pixel", "barnacle"},
        {"pixels", "barnacles"},
        {"stage", "watch"},
        {"stages", "watches"},
        {"mask", "eyepatch"},
        {"masking", "eyepatchin'"},
        {"core", "keel"},
        {"column", "mast"},
        {"columns", "masts"},
        {"activated", "hoisted"},
        {"activate", "hoist"},
        {"reset", "keelhaul"},
        {"resetting", "keelhaulin'"},
        {"received", "landed"},
        {"implemented", "rigged"},
        {"command", "holler"},
        {"commands", "hollers"},
        {"clear", "swab"},
        {"loading", "haulin' aboard"},
        {"load", "haul aboard"},
        {"loaded", "hauled aboard"},
        {"controller", "helm"},
        {"processor", "quartermaster"},
        {"processors", "quartermasters"},
        {"obtained", "plundered"},
        {"analysis", "reckonin'"},
        {"output", "cargo"},
        {"count", "tally"},
        {"number", "tally"},
        {"waiting", "becalmed"},
        {"target", "prize"},
        {"thread", "line"},
        {"threads", "lines"},
        {"loop", "bight"},
        {"loops", "bights"},
        {"connectivity", "riggin'"},
        {"channel", "strait"},
        {"channels", "straits"},
        {"found", "spied"},
        {"streams", "currents"},
        {"run", "sail"},
        {"running", "sailin'"},
        {"setup", "rig"},
        {"configure", "rig"},
        {"configuring", "riggin'"},
        {"timing", "tide"},
        {"init", "hoist"},
        {"type", "breed"},
        {"cleanup", "swab th' deck"},
        {"enabling", "hoistin'"},
        {"enabled", "hoisted"},
    };
    return map;
}

// Short interjections randomly prepended to log lines in pirate mode.
constexpr std::array<const char *, 10> kQuips = {
    "Arrr!", "Avast!", "Yarrr!", "Ahoy!", "Blimey!",
    "Yo ho ho!", "Har har!", "Shiver me timbers!",
    "Batten down the hatches!", "Blow me down!",
};

// Roughly 1 in 3 eligible lines get a quip, so it reads as seasoning
// rather than noise on every single line.
constexpr int kQuipChancePercent = 33;

std::mt19937 &quipRng() {
    thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

// Skip quips on lines that are really part of a drawn frame (the banner's
// "####" border/body, or a "~~~"-prefixed raw JSON dump) so the box drawing
// and dumped config text aren't garbled with extra words.
bool eligibleForQuip(const std::string &msg) {
    if (msg.empty()) return false;
    if (msg.find("##") != std::string::npos) return false;
    if (msg.rfind("~~~", 0) == 0) return false;
    return true;
}

enum class Case { Lower, Upper, Title };

Case detectCase(const std::string &word) {
    bool anyUpper = false;
    bool allUpper = true;
    for (char c : word) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            if (std::isupper(static_cast<unsigned char>(c))) {
                anyUpper = true;
            } else {
                allUpper = false;
            }
        }
    }
    if (!anyUpper) return Case::Lower;
    if (allUpper) return Case::Upper;
    if (!word.empty() && std::isupper(static_cast<unsigned char>(word.front()))) {
        return Case::Title;
    }
    return Case::Lower;
}

std::string applyCase(std::string word, Case c) {
    switch (c) {
        case Case::Upper:
            for (char &ch : word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            break;
        case Case::Title:
            if (!word.empty()) {
                word.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(word.front())));
            }
            break;
        case Case::Lower:
        default:
            break;
    }
    return word;
}

std::string toLower(const std::string &word) {
    std::string out = word;
    for (char &ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool isWordChar(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}
} // namespace

void setPirateMode(bool enabled) {
    g_pirateMode = enabled;
}

bool pirateModeEnabled() {
    return g_pirateMode;
}

std::string piratify(const std::string &msg) {
    std::string result;
    result.reserve(msg.size());

    std::size_t i = 0;
    while (i < msg.size()) {
        if (isWordChar(msg[i])) {
            std::size_t start = i;
            while (i < msg.size() && isWordChar(msg[i])) i++;
            std::string word = msg.substr(start, i - start);

            auto it = wordMap().find(toLower(word));
            if (it != wordMap().end()) {
                result += applyCase(it->second, detectCase(word));
            } else {
                result += word;
            }
        } else {
            result += msg[i];
            i++;
        }
    }
    return result;
}

void PirateVFormatter::format(const spdlog::details::log_msg &msg, const std::tm & /*tm_time*/,
                               spdlog::memory_buf_t &dest) {
    std::string translated = piratify(std::string(msg.payload.data(), msg.payload.size()));
    dest.append(translated.data(), translated.data() + translated.size());
}

std::unique_ptr<spdlog::custom_flag_formatter> PirateVFormatter::clone() const {
    return spdlog::details::make_unique<PirateVFormatter>();
}

std::string insertQuipFlag(const std::string &pattern) {
    std::string result;
    result.reserve(pattern.size() + 4);
    for (std::size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 'v') {
            result += "%j";
        }
        result += pattern[i];
    }
    return result;
}

void PirateQuipFormatter::format(const spdlog::details::log_msg &msg, const std::tm & /*tm_time*/,
                                  spdlog::memory_buf_t &dest) {
    std::string payload(msg.payload.data(), msg.payload.size());
    if (!eligibleForQuip(payload)) return;

    std::uniform_int_distribution<int> chance(1, 100);
    if (chance(quipRng()) > kQuipChancePercent) return;

    std::uniform_int_distribution<std::size_t> pick(0, kQuips.size() - 1);
    const char *quip = kQuips[pick(quipRng())];
    std::string prefix = std::string(quip) + " ";
    dest.append(prefix.data(), prefix.data() + prefix.size());
}

std::unique_ptr<spdlog::custom_flag_formatter> PirateQuipFormatter::clone() const {
    return spdlog::details::make_unique<PirateQuipFormatter>();
}

} // namespace logging
