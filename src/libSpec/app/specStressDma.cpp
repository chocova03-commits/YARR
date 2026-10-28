#include <SpecCom.h>
#include "logging.h"
#include "LoggingConfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <sys/mman.h>
#include <thread>
#include <vector>

// Randomised stress test of the DMA paths between host and card, with the data of every transfer checked.
//
// Each iteration writes a pattern in which every 32-bit word is unique to the card (host to device), reads
// it back (device to host) into a poisoned buffer and compares every word. The parameters are drawn from a
// seeded generator, so a failure can be repeated: size (8 bytes to 1 MB, with a bias to sizes around a TLP
// or a page), host buffer (hugepage or normal pages), start offset inside the buffer, device address, merged
// descriptors on or off, and the largest merged descriptor (4 to 32 KB).
//
// Optional load while the DMA runs: threads that read the card's registers all the time (the register reads
// share the PCIe transmit path with the DMA data and are served between its TLPs), and threads that load
// the memory system of the host (to make the PCIe side stall). The firmware's DMA counters tell whether the
// load had that effect: the share of "back-pressured" cycles is printed.
//
// The test stops at the first failure and prints everything needed to repeat it.
// Exit code: 0 passed, 1 data differs, 2 DMA error or card does not respond, 3 register reads differ.
namespace {

std::atomic<bool> g_stop{false};
std::atomic<bool> g_dead{false};
std::atomic<uint64_t> g_reg_reads{0};
std::atomic<uint64_t> g_reg_errors{0};

void onSignal(int) { g_stop = true; }

constexpr uint32_t kMul = 2654435761u;   // odd, so word i -> i * kMul is unique for every index
constexpr uint32_t kPoison = 0xDEADBEEF;
constexpr size_t kPad = 64;              // words in front of the transfer that must stay untouched
constexpr size_t kGuard = 64;            // words behind the transfer that must stay untouched
constexpr size_t kHugePage = 2 * 1024 * 1024;

struct Buffer {
    std::unique_ptr<uint32_t, decltype(&std::free)> mem{nullptr, &std::free};
    size_t words = 0;
    uint32_t *data() const { return mem.get(); }
};

Buffer allocBuffer(size_t words, bool huge) {
    Buffer b;
    const size_t bytes = ((words * 4 + kHugePage - 1) / kHugePage) * kHugePage;
    b.mem.reset(static_cast<uint32_t*>(std::aligned_alloc(huge ? kHugePage : 4096, bytes)));
    b.words = bytes / 4;
    if (b.mem) {
        madvise(b.mem.get(), bytes, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
        std::memset(b.mem.get(), 0, bytes);   // touch the pages
    }
    return b;
}

struct Params {
    size_t words;          // transfer size in 32-bit words (even: lengths are multiples of 8 bytes)
    bool huge;             // hugepage or normal host buffers
    size_t src_off, dst_off;   // start offset in the host buffers, in words
    uint32_t dev_off;      // device address in bytes
    bool merge;            // merged device-to-host descriptors
    uint32_t cap;          // largest merged descriptor in bytes
    uint32_t salt;
};

// Restrictions of the random parameters, to find out which of them makes a failure appear
struct Restrict {
    size_t max_off = 2047;   // largest start offset in the host buffers, in words (0: page-aligned buffers)
    int buffers = 2;         // 0 normal pages only, 1 hugepages only, 2 both
    bool dev_off = true;     // device addresses other than 0
};

Params drawParams(std::mt19937_64 &rng, size_t max_words, bool unaligned, const Restrict &rs) {
    auto uni = [&rng](uint64_t lo, uint64_t hi) { return lo + rng() % (hi - lo + 1); };
    Params p;
    const unsigned cat = rng() % 100;
    if (cat < 30) p.words = uni(2, 4096);
    else if (cat < 70) p.words = uni(4096, 32768);
    else p.words = uni(32768, max_words);
    if (rng() % 100 < 35) {   // around a multiple of a 256 byte TLP (64 words)
        p.words = std::max<size_t>(64, p.words / 64 * 64);
        const int delta[] = {-4, -2, 0, 0, 2, 4};
        p.words = size_t(int64_t(p.words) + delta[rng() % 6]);
    }
    p.words = std::min(std::max<size_t>(p.words & ~size_t(1), 2), max_words);
    p.huge = rs.buffers == 2 ? bool(rng() % 2) : rs.buffers == 1;
    auto off = [&]() {
        if (rs.max_off == 0 || rng() % 2) return size_t(0);
        size_t o = uni(0, std::min<size_t>(rs.max_off, 2047));
        return unaligned ? o : (o & ~size_t(1));
    };
    p.src_off = off();
    p.dst_off = off();
    p.dev_off = (!rs.dev_off || rng() % 2) ? 0 : uint32_t(uni(0, 4095) * 8);
    p.merge = rng() % 4 != 0;
    const uint32_t caps[] = {4096, 8192, 16384, 32768};
    p.cap = caps[rng() % 4];
    p.salt = uint32_t(rng());
    return p;
}

std::string describe(const Params &p) {
    std::ostringstream o;
    o << p.words << " words (" << p.words * 4 << " B), " << (p.huge ? "hugepage" : "normal") << " buffers, host offsets src "
      << p.src_off << " dst " << p.dst_off << " words, device address 0x" << std::hex << p.dev_off << std::dec
      << ", merged " << (p.merge ? "on" : "off") << ", descriptors up to " << p.cap << " B";
    return o.str();
}

std::string cardState(SpecCom &spec) {
    const uint32_t v = spec.readSingle(SPEC_GREG | SPEC_GREG_FWVERS);
    std::ostringstream o;
    if (v == 0xFFFFFFFFu) o << "the card does not respond (register reads all ones)";
    else o << "the card still responds (firmware register 0x" << std::hex << v << ")";
    return o.str();
}

/// Keeps reading registers that must never change; a difference means that a register read was corrupted.
void regThread(SpecCom *spec, int id) {
    const uint32_t regs[] = {SPEC_GREG | SPEC_GREG_FWVERS, SPEC_GREG | SPEC_GREG_FWIDENT, SPEC_GREG | 0x8};
    uint32_t ref[3];
    for (int i = 0; i < 3; i++) ref[i] = spec->readSingle(regs[i]);
    uint64_t since = 0;
    while (!g_stop) {
        for (int i = 0; i < 3; i++) {
            const uint32_t v = spec->readSingle(regs[i]);
            if (v == ref[i]) continue;
            if (v == 0xFFFFFFFFu) {   // the card does not respond any more
                g_dead = true;
                g_stop = true;
            } else if (g_reg_errors++ < 5) {
                std::cerr << "REGISTER MISMATCH (thread " << id << "): register 0x" << std::hex << regs[i] << " read 0x" << v
                          << " instead of 0x" << ref[i] << std::dec << std::endl;
            }
        }
        since += 3;
        if (since >= 4096) { g_reg_reads += since; since = 0; }
    }
    g_reg_reads += since;
}

/// Loads the memory system of the host
void hogThread(size_t bytes) {
    std::unique_ptr<uint8_t, decltype(&std::free)> a(static_cast<uint8_t*>(std::aligned_alloc(64, bytes)), &std::free);
    if (!a) return;
    std::memset(a.get(), 0x55, bytes);
    volatile uint8_t sink = 0;
    while (!g_stop) {
        std::memcpy(a.get(), a.get() + bytes / 2, bytes / 2);
        std::memset(a.get() + bytes / 2, sink++, bytes / 2);
    }
}

void usage(const char *prog) {
    std::cout << "usage: " << prog << " [-s spec id] [-t seconds, 0 = forever] [-r seed] [-k iterations to skip] [-j register threads]\n"
              << "       [-m memory load threads] [-x max words] [-a] [-o max offset] [-b 0|1] [-D] [-n iterations] [-q]\n"
              << "  -s  card number (default 0)\n"
              << "  -t  duration (default 60 s)\n"
              << "  -r  seed of the random generator (default: from the clock, printed)\n"
              << "  -k  skip this many iterations (same seed: repeat a failure at that iteration)\n"
              << "  -j  threads that read registers during the DMA (default 1)\n"
              << "  -m  threads that load the host memory with 256 MB each (default 0)\n"
              << "  -x  largest transfer in 32-bit words (default 262144 = 1 MB)\n"
              << "  -a  also use host buffer offsets that are not a multiple of 8 bytes\n"
              << "  -o  largest start offset in the host buffers in words (default 2047, 0 = page-aligned buffers)\n"
              << "  -b  host buffers: 0 normal pages only, 1 hugepages only (default: both)\n"
              << "  -D  device address 0 only\n"
              << "  -n  stop after this many iterations (with -k: repeat exactly one transfer)\n"
              << "  -q  no progress lines" << std::endl;
}

}  // namespace

int main(int argc, char *argv[]) {
    unsigned spec_num = 0;
    double seconds = 60;
    uint64_t seed = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t skip = 0;
    int reg_threads = 1, hog_threads = 0;
    size_t max_words = 262144;
    bool unaligned = false, quiet = false;
    Restrict rs;
    uint64_t max_iter = 0;
    int c;
    while ((c = getopt(argc, argv, "s:t:r:k:j:m:x:o:b:n:Daqh")) != -1) {
        switch (c) {
            case 's': spec_num = atoi(optarg); break;
            case 't': seconds = atof(optarg); break;
            case 'r': seed = strtoull(optarg, nullptr, 0); break;
            case 'k': skip = strtoull(optarg, nullptr, 0); break;
            case 'j': reg_threads = atoi(optarg); break;
            case 'm': hog_threads = atoi(optarg); break;
            case 'x': max_words = strtoul(optarg, nullptr, 0) & ~size_t(1); break;
            case 'a': unaligned = true; break;
            case 'o': rs.max_off = strtoul(optarg, nullptr, 0); break;
            case 'b': rs.buffers = atoi(optarg); break;
            case 'D': rs.dev_off = false; break;
            case 'n': max_iter = strtoull(optarg, nullptr, 0); break;
            case 'q': quiet = true; break;
            default: usage(argv[0]); return c == 'h' ? 0 : 2;
        }
    }
    if (max_words < 2) max_words = 2;

    logging::setupLoggers(logging::defaultConfig());
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    SpecCom spec(spec_num);

    const size_t buf_words = max_words + 2 * 2048 + kPad + kGuard + 16;
    Buffer src[2], dst[2];   // [0] normal pages, [1] hugepage
    for (int h = 0; h < 2; h++) {
        src[h] = allocBuffer(buf_words, h == 1);
        dst[h] = allocBuffer(buf_words, h == 1);
        if (!src[h].mem || !dst[h].mem) {
            std::cerr << "could not allocate the buffers" << std::endl;
            return 2;
        }
    }

    std::cout << "specStressDma: card " << spec_num << ", seed " << seed << ", " << (seconds > 0 ? std::to_string(int(seconds)) + " s" : "no time limit")
              << ", " << reg_threads << " register thread(s), " << hog_threads << " memory load thread(s), transfers up to " << max_words * 4
              << " bytes" << (unaligned ? ", unaligned offsets" : "") << std::endl
              << "repeat with: specStressDma -s " << spec_num << " -r " << seed << " -x " << max_words << (unaligned ? " -a" : "") << (rs.max_off != 2047 ? " -o " + std::to_string(rs.max_off) : std::string()) << (rs.buffers != 2 ? " -b " + std::to_string(rs.buffers) : std::string()) << (rs.dev_off ? "" : " -D")
              << " -k <iteration>" << std::endl;

    std::mt19937_64 rng(seed);
    for (uint64_t i = 0; i < skip; i++) drawParams(rng, max_words, unaligned, rs);

    std::vector<std::thread> threads;
    for (int i = 0; i < reg_threads; i++) threads.emplace_back(regThread, &spec, i);
    for (int i = 0; i < hog_threads; i++) threads.emplace_back(hogThread, size_t(256) << 20);

    uint32_t kInv = kMul;   // modular inverse of kMul (Newton iteration), finds the source word of a received word
    for (int i = 0; i < 5; i++) kInv *= 2 - kMul * kInv;

    const auto t0 = std::chrono::steady_clock::now();
    auto last_print = t0;
    const DmaStats stats0 = spec.getDmaStats();
    DmaStats stats_last = stats0;
    uint64_t iteration = skip, done = 0, words_total = 0;
    int rc = 0;

    auto elapsed = [&t0]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    auto fail = [&](int code, const std::string &what) {
        std::cout << std::endl << "FAILED at iteration " << iteration << ": " << what << std::endl
                  << "  repeat this iteration with: -s " << spec_num << " -r " << seed << " -x " << max_words << (unaligned ? " -a" : "") << (rs.max_off != 2047 ? " -o " + std::to_string(rs.max_off) : std::string()) << (rs.buffers != 2 ? " -b " + std::to_string(rs.buffers) : std::string()) << (rs.dev_off ? "" : " -D")
                  << " -k " << iteration << std::endl;
        rc = code;
        g_stop = true;
    };

    while (!g_stop && (seconds <= 0 || elapsed() < seconds) && (max_iter == 0 || done < max_iter)) {
        const Params p = drawParams(rng, max_words, unaligned, rs);
        uint32_t *s = src[p.huge].data() + kPad + p.src_off;
        uint32_t *d = dst[p.huge].data() + kPad + p.dst_off;
        for (size_t i = 0; i < p.words; i++) s[i] = uint32_t(i) * kMul ^ p.salt;
        uint32_t *d_front = d - kPad;
        for (size_t i = 0; i < kPad; i++) d_front[i] = kPoison;               // in front of the transfer
        for (size_t i = 0; i < p.words + kGuard; i++) d[i] = kPoison;         // the transfer and behind it

        spec.setMergeDmaRead(p.merge);
        spec.setMergedDescBytes(p.cap);
        if (spec.writeDma(p.dev_off, s, p.words)) {
            fail(2, "writeDma failed, " + describe(p) + "; " + cardState(spec));
            break;
        }
        if (spec.readDma(p.dev_off, d, p.words)) {
            fail(2, "readDma failed, " + describe(p) + "; " + cardState(spec));
            break;
        }

        size_t errors = 0, first = p.words, before = 0, behind = 0;
        for (size_t i = 0; i < p.words; i++) {
            if (d[i] != s[i]) {
                if (errors == 0) first = i;
                errors++;
            }
        }
        for (size_t i = 0; i < kPad; i++) before += (d_front[i] != kPoison);
        for (size_t i = p.words; i < p.words + kGuard; i++) behind += (d[i] != kPoison);
        if (errors || before || behind) {
            std::ostringstream o;
            o << errors << " wrong words of " << p.words << ", " << describe(p);
            if (errors) {
                const uint32_t src_idx = (d[first] ^ p.salt) * kInv;
                o << "; first at word " << first << " (byte " << first * 4 << ", 4 KB page " << first / 1024 << ", offset " << first % 1024
                  << " words), got " << std::hex << d[first] << " expected " << s[first] << std::dec;
                if (d[first] == kPoison) o << " = not written (data missing)";
                else if (src_idx < p.words) o << " = source word " << src_idx << " (" << int64_t(src_idx) - int64_t(first) << " words away)";
                // length of the damaged region: where the data is correct again
                size_t last_bad = first;
                for (size_t i = first; i < p.words; i++) if (d[i] != s[i]) last_bad = i;
                o << "; last wrong word " << last_bad << (last_bad == p.words - 1 ? " (up to the end)" : "");
            }
            if (before) o << "; " << before << " words written in front of the buffer";
            if (behind) o << "; " << behind << " words written behind the buffer";
            o << "; " << cardState(spec);
            fail(1, o.str());
            break;
        }

        iteration++;
        done++;
        words_total += p.words;
        const auto now = std::chrono::steady_clock::now();
        if (!quiet && std::chrono::duration<double>(now - last_print).count() >= 10.0) {
            const DmaStats d_all = spec.getDmaStats() - stats0;
            const DmaStats d_new = spec.getDmaStats() - stats_last;
            stats_last = spec.getDmaStats();
            std::cout << std::fixed << std::setprecision(1) << "[" << elapsed() << " s] " << done << " transfers ok, "
                      << double(words_total) * 8 / 1e9 << " GB moved (both directions), " << g_reg_reads.load() << " register reads"
                      << (g_reg_errors ? " ERRORS" : "");
            if (d_new.has_cycles && d_new.busy > 0)
                std::cout << ", back-pressure " << 100.0 * d_new.backpressure / d_new.busy << " % of active cycles (run: "
                          << 100.0 * d_all.backpressure / std::max<uint32_t>(d_all.busy, 1) << " %)";
            std::cout << std::endl;
            last_print = now;
        }
    }
    g_stop = true;
    for (auto &t : threads) t.join();

    if (g_dead && rc == 0) { std::cout << "FAILED: a register read returned all ones: the card does not respond" << std::endl; rc = 2; }
    if (g_reg_errors && rc == 0) { std::cout << "FAILED: " << g_reg_errors.load() << " register reads returned a wrong value" << std::endl; rc = 3; }

    const DmaStats total = spec.getDmaStats() - stats0;
    std::cout << std::fixed << std::setprecision(1) << std::endl << "Summary: " << done << " transfers (" << double(words_total) * 8 / 1e9
              << " GB in both directions) in " << elapsed() << " s, " << g_reg_reads.load() << " register reads, seed " << seed << std::endl;
    if (total.has_cycles && total.busy > 0) {
        const double bp = 100.0 * total.backpressure / total.busy;
        std::cout << "DMA counters of the run: " << bp << " % of the active cycles back-pressured by the PCIe side, "
                  << 100.0 * total.starved / total.busy << " % waiting for data, " << 100.0 * total.header_wait / total.busy
                  << " % waiting at a TLP header" << std::endl;
        if (bp < 0.05) std::cout << "  (the PCIe side hardly stalled: the credit logic for stalls was barely exercised, try -m for load)" << std::endl;
    }
    std::cout << (rc == 0 ? "PASSED" : "FAILED") << std::endl;
    return rc;
}
