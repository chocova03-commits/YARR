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
#include <memory>
#include <string>
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
}  // namespace

int main(int argc, char *argv[]) {
    unsigned spec_num = 0;
    if (argc == 2)
        spec_num = atoi(argv[1]);
    logging::setupLoggers(logging::defaultConfig());
    SpecCom mySpec (spec_num);

    std::fstream file_write("benchmarkDma_write.out", std::ios::out);
    std::fstream file_read("benchmarkDma_read.out", std::ios::out);

    constexpr int maxCycles = 400;
    constexpr int maxLoops = 100;

    double overall_time = 0;
    double overall_data = 0;

    timeval start = {}, end = {};
    
    constexpr size_t MAX_WORDS = 256 * (maxCycles+1);
    // With SPEC_BENCH_HUGEPAGE set the buffer is 2 MB aligned and backed by a transparent hugepage, i.e.
    // physically contiguous. Together with SPEC_DMA_MERGE_SG_READ the read phase then uses descriptors
    // larger than 4 KB (see SpecCom::readDma).
    constexpr size_t kHugePage = 2 * 1024 * 1024;
    const size_t buf_bytes = ((MAX_WORDS * 4 + kHugePage - 1) / kHugePage) * kHugePage;
    const bool use_huge = std::getenv("SPEC_BENCH_HUGEPAGE") != nullptr;
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
    return 0;
}
