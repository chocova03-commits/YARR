#include <SpecCom.h>
#include "logging.h"
#include "LoggingConfig.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/time.h>

namespace {
// Clock of the firmware's DMA logic, used to convert the cycle counters to time
constexpr double kWbClkHz = 320e6;

/// Print what the firmware's DMA counters saw during the read (device-to-host) phase.
/// bytes is the amount of data the benchmark transferred in that time.
void printDmaStats(const DmaStats &d, double seconds, double bytes) {
    std::cout << "Device-to-host DMA statistics of the read phase:" << std::endl;
    if (!d.has_tlp) {
        std::cout << "  not available (firmware without DMA counters)" << std::endl;
        return;
    }
    const uint64_t full = uint64_t(d.tlp_128) + d.tlp_256 + d.tlp_512;
    std::cout << "  TLPs: " << d.tlp_128 << " x 128 B, " << d.tlp_256 << " x 256 B, " << d.tlp_512
              << " x 512 B, " << d.tlp_other << " shorter";
    if (full > 0)
        std::cout << " (average payload of the full-size TLPs: "
                  << double(uint64_t(d.tlp_128) * 128 + uint64_t(d.tlp_256) * 256 + uint64_t(d.tlp_512) * 512) / double(full)
                  << " B)";
    std::cout << std::endl;
    if (!d.has_cycles || d.busy == 0) {
        std::cout << "  cycle counters not available" << std::endl;
        return;
    }
    const double active = double(d.busy) / kWbClkHz;
    const double busy = double(d.busy);
    std::cout << std::fixed << std::setprecision(1)
              << "  DMA master active " << active << " s of " << seconds << " s (" << 100.0 * active / seconds
              << " %); the rest is the gap between descriptors and calls" << std::endl
              << "  payload rate while active: " << bytes / active / 1e6 << " MB/s ("
              << std::setprecision(2) << bytes / 8.0 / busy << std::setprecision(1) << " payload beats per active cycle)" << std::endl
              << "  of the active cycles: " << 100.0 * d.starved / busy << " % waiting for data, "
              << 100.0 * d.backpressure / busy << " % back-pressured by the PCIe side, "
              << 100.0 * d.header_wait / busy << " % waiting at a TLP header" << std::endl;
}

/// Data check of the device-to-host path: writes a pattern in which every word is unique, reads it back
/// into a poisoned buffer with the configured (merged or not) descriptors and compares every word, for sizes
/// that are and are not multiples of a TLP, a 4 KB page and a descriptor. A lost, duplicated, shifted or
/// short transfer shows up as a mismatch; the source word that arrived at the first bad position is printed.
/// Returns the number of transfers with errors.
int verifyDma(SpecCom &spec, bool use_huge, size_t max_words, size_t only_words, int repeats) {
    constexpr uint32_t kMul = 2654435761u;   // odd: word i -> i * kMul is unique for every index
    constexpr uint32_t kPoison = 0xDEADBEEF;
    constexpr size_t kGuard = 16;            // poisoned words behind the transfer, detect an overrun
    constexpr size_t kHugePage = 2 * 1024 * 1024;
    if (only_words > max_words) max_words = only_words;   // a single size may be larger than the sizes of the benchmark
    uint32_t kInv = kMul;                    // modular inverse of kMul (Newton iteration), to find the source index
    for (int i = 0; i < 5; i++) kInv *= 2 - kMul * kInv;

    const size_t buf_bytes = (((max_words + kGuard) * 4 + kHugePage - 1) / kHugePage) * kHugePage;
    std::unique_ptr<uint32_t, decltype(&std::free)> src(
        static_cast<uint32_t*>(std::aligned_alloc(use_huge ? kHugePage : 4096, buf_bytes)), &std::free);
    std::unique_ptr<uint32_t, decltype(&std::free)> dst(
        static_cast<uint32_t*>(std::aligned_alloc(use_huge ? kHugePage : 4096, buf_bytes)), &std::free);
    if (!src || !dst) {
        std::cerr << "verify: could not allocate the buffers" << std::endl;
        return 1;
    }
    if (use_huge) {
        madvise(src.get(), buf_bytes, MADV_HUGEPAGE);
        madvise(dst.get(), buf_bytes, MADV_HUGEPAGE);
    }
    std::memset(src.get(), 0, buf_bytes);
    std::memset(dst.get(), 0, buf_bytes);

    // Sizes in 32-bit words (the length of a DMA is a multiple of 8 bytes)
    std::vector<size_t> sizes = {2, 6, 62, 64, 66, 254, 256, 258, 1022, 1024, 1026, 2046, 2048, 2050,
                                 8190, 8192, 8194, 16382, 16384, 16386, 25600, 65534, 65536, 65538};
    for (int c = 0; c <= 400; c += 7) sizes.push_back(256 * (c + 1));
    if (only_words > 0) {   // a single size, repeated: to find out whether a failure depends on the size
        sizes.assign(std::max(repeats, 1), only_words);
    }

    std::cout << std::endl << "===========================================" << std::endl;
    std::cout << "Starting DMA data verification (merged descriptors "
              << (spec.getMergeDmaRead() ? "on" : "off") << ", up to " << spec.getMergedDescBytes() << " bytes, "
              << (use_huge ? "hugepage" : "normal") << " buffer):" << std::endl;
    int failed = 0, run = 0;
    size_t words_total = 0;
    for (size_t n : sizes) {
        if (n > max_words || n % 2 != 0) continue;
        const uint32_t salt = 0x5EED0000u + uint32_t(run++) * 0x01000193u;
        uint32_t *s = src.get(), *d = dst.get();
        for (size_t i = 0; i < n; i++) s[i] = uint32_t(i) * kMul ^ salt;
        for (size_t i = 0; i < n + kGuard; i++) d[i] = kPoison;

        // After an error say whether the card still answers (a dead PCIe link reads all ones)
        auto cardState = [&spec]() {
            const uint32_t v = spec.readSingle(SPEC_GREG | 0x6);
            std::ostringstream o;
            if (v == 0xFFFFFFFFu) o << "the card does not respond (register reads all ones)";
            else o << "the card still responds (firmware hash register 0x" << std::hex << v << ")";
            return o.str();
        };
        if (spec.writeDma(0x0, s, n)) {
            std::cerr << "verify: writeDma failed (" << n << " words, transfer " << run << "); " << cardState() << std::endl;
            return failed + 1;
        }
        if (spec.readDma(0x0, d, n)) {
            std::cerr << "verify: readDma failed (" << n << " words, transfer " << run << "); " << cardState() << std::endl;
            return failed + 1;
        }

        size_t errors = 0, first = n;
        for (size_t i = 0; i < n; i++) {
            if (d[i] != s[i]) {
                if (errors == 0) first = i;
                errors++;
            }
        }
        size_t overrun = 0;
        for (size_t i = n; i < n + kGuard; i++) overrun += (d[i] != kPoison);
        words_total += n;
        if (errors == 0 && overrun == 0) {
            std::cout << "  ok   " << n << " words" << std::endl;
            continue;
        }

        failed++;
        std::cout << "  FAIL " << n << " words (" << n * 4 << " bytes): " << errors << " wrong words";
        if (errors > 0) {
            const uint32_t src_idx = (d[first] ^ salt) * kInv;   // which source word is at the first bad position
            std::cout << ", first at word " << first << " (4 KB page " << first / 1024 << ", offset " << first % 1024
                      << "), got " << std::hex << d[first] << " expected " << s[first] << std::dec;
            if (src_idx < n)
                std::cout << " = source word " << src_idx << " (" << int64_t(src_idx) - int64_t(first) << " words away)";
            else if (d[first] == kPoison)
                std::cout << " = not written (data missing)";
        }
        if (overrun > 0) std::cout << ", " << overrun << " words written beyond the end";
        std::cout << std::endl << "       " << cardState() << std::endl;
        return failed;   // everything after a failure is not meaningful
    }
    std::cout << "Verification " << (failed == 0 ? "PASSED" : "FAILED") << ": " << run << " transfers, "
              << words_total * 4 / 1024 << " KB read back, " << failed << " with errors" << std::endl;
    std::cout << "===========================================" << std::endl;
    return failed;
}
}  // namespace

// usage: specBenchmarkDma [spec number] [merge 0|1] [hugepage 0|1] [merged descriptor bytes] [verify 0|1|2] [words] [repeats]
//   merge:    device-to-host descriptors of contiguous pages are merged (default: the SpecCom default, on)
//   hugepage: use a 2 MB aligned transparent-hugepage buffer, i.e. physically contiguous, so that merging
//             has an effect (default 0, a normal buffer)
//   merged descriptor bytes: largest merged descriptor, 4096 to 32768 (default 32768)
//   verify:   1 = also write a pattern, read it back and compare it (data check), 2 = only the data check,
//             without the speed benchmark (default 0); exit code 1 if the data differs
//   words, repeats: with verify, check only this transfer size (32-bit words), repeats times
int main(int argc, char *argv[]) {
    unsigned spec_num = 0;
    if (argc > 1)
        spec_num = atoi(argv[1]);
    logging::setupLoggers(logging::defaultConfig());
    SpecCom mySpec (spec_num);
    if (argc > 2)
        mySpec.setMergeDmaRead(atoi(argv[2]) != 0);
    const bool use_huge = argc > 3 && atoi(argv[3]) != 0;
    if (argc > 4)
        mySpec.setMergedDescBytes(strtoul(argv[4], nullptr, 0));
    const int verify = argc > 5 ? atoi(argv[5]) : 0;
    const size_t verify_words = argc > 6 ? strtoul(argv[6], nullptr, 0) : 0;
    const int verify_repeats = argc > 7 ? atoi(argv[7]) : 1;

    std::fstream file_write("benchmarkDma_write.out", std::ios::out);
    std::fstream file_read("benchmarkDma_read.out", std::ios::out);

    constexpr int maxCycles = 400;
    constexpr int maxLoops = 100;

    double overall_time = 0;
    double overall_data = 0;

    timeval start = {}, end = {};
    
    constexpr size_t MAX_WORDS = 256 * (maxCycles+1);
    // With the hugepage option the buffer is 2 MB aligned and backed by a transparent hugepage, i.e.
    // physically contiguous, so merged lists (see SpecCom::readDma) can use descriptors larger than 4 KB.
    constexpr size_t kHugePage = 2 * 1024 * 1024;
    const size_t buf_bytes = ((MAX_WORDS * 4 + kHugePage - 1) / kHugePage) * kHugePage;
    std::unique_ptr<uint32_t, decltype(&std::free)> buf_owner(
        static_cast<uint32_t*>(std::aligned_alloc(use_huge ? kHugePage : 4096, buf_bytes)), &std::free);
    if (!buf_owner) {
        std::cerr << "Could not allocate the buffer" << std::endl;
        return 1;
    }
    uint32_t *data_buf = buf_owner.get();
    if (use_huge && madvise(data_buf, buf_bytes, MADV_HUGEPAGE) != 0)
        std::cerr << "madvise(MADV_HUGEPAGE) failed, the buffer may not be contiguous" << std::endl;
    std::memset(data_buf, 0, buf_bytes);  // touch the pages before the first transfer
    std::cout << "Settings: merged device-to-host descriptors " << (mySpec.getMergeDmaRead() ? "on" : "off")
              << " (up to " << mySpec.getMergedDescBytes() << " bytes)"
              << ", " << (use_huge ? "hugepage" : "normal") << " buffer" << std::endl;
    if (verify == 2)
        return verifyDma(mySpec, use_huge, MAX_WORDS, verify_words, verify_repeats) == 0 ? 0 : 1;

    std::cout << std::endl << "==========================================" << std::endl;
    std::cout << "Starting DMA Write Benchmark:" << std::endl;
    for (int cycles=0; cycles<maxCycles; cycles++) {
        const size_t size = 256 * (cycles + 1);

        // Prepare data pattern
        memset(data_buf, 0x5A, size*4);
        
        // Write to Spec
        gettimeofday(&start, nullptr);
        for (int loops=0; loops<maxLoops; loops++)
           if (mySpec.writeDma(0x0, data_buf, size)) return 1;
        gettimeofday(&end, nullptr);

        // Analyze time
        double total_data = size*4*maxLoops/1024.0/1024.0;
        overall_data += total_data;
        double time = (end.tv_sec - start.tv_sec) * 1000.0; //msecs
        time += (end.tv_usec - start.tv_usec) / 1000.0; //usecs
        overall_time += time;
        std::cout << "Transferred " << total_data << "MB in " << time << " ms: " << total_data/time*1000.0 << " MB/s" << std::endl;
        file_write << size << "\t" << total_data << "\t" << time << "\t" << total_data/time*1000.0 << std::endl;
    }
    std::cout << "===========================================" << std::endl;
    std::cout << "---> Mean Transfer Speed: " << overall_data/overall_time*1000.0 << " MB/s"  << std::endl;
    std::cout << "===========================================" << std::endl << std::endl;

    overall_data = 0;
    overall_time = 0;
    const DmaStats stats_before = mySpec.getDmaStats();


    std::cout << "===========================================" << std::endl;
    std::cout << "Starting DMA Read Benchmark:" << std::endl;
    for (int cycles=0; cycles<maxCycles; cycles++) {
        const size_t size = 256 * (cycles + 1);

        // Read from Spec
        gettimeofday(&start, nullptr);
        for (int loops=0; loops<maxLoops; loops++)
            if(mySpec.readDma(0x0, data_buf, size)) return 1;
        gettimeofday(&end, nullptr);
        
        // Analyze time
        double total_data = size*4*maxLoops/1024.0/1024.0;
        overall_data += total_data;
        double time = (end.tv_sec - start.tv_sec) * 1000.0; //msecs
        time += (end.tv_usec - start.tv_usec) / 1000.0; //usecs
        overall_time += time;
        std::cout << "Transferred " << total_data << "MB in " << time << " ms: " << total_data/time*1000.0 << " MB/s" << std::endl;
        file_read << size << "\t" << total_data << "\t" << time << "\t" << total_data/time*1000.0 << std::endl;
    }
    std::cout << "===========================================" << std::endl;
    std::cout << "---> Mean Transfer Speed: " << overall_data/overall_time*1000.0 << " MB/s"  << std::endl;
    std::cout << "===========================================" << std::endl << std::endl;

    printDmaStats(mySpec.getDmaStats() - stats_before, overall_time / 1000.0, overall_data * 1024.0 * 1024.0);

    file_write.close();
    file_read.close();
    if (verify == 1 && verifyDma(mySpec, use_huge, MAX_WORDS, verify_words, verify_repeats) != 0)
        return 1;
    return 0;
}
