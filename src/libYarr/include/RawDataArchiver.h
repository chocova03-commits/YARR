#ifndef RAWDATAARCHIVER_H
#define RAWDATAARCHIVER_H

// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Archives pre-decode raw hardware data to disk
// ################################

#include <fstream>
#include <memory>
#include <thread>

#include "ClipBoard.h"
#include "DataProcessor.h"
#include "RawData.h"

/**
 * Chip-agnostic consumer of clipRawDataArchive.
 *
 * Writes out RawDataContainer objects (raw hardware words, pre-decode) in
 * the binary format defined in RawDataArchiveFormat.h. Runs on its own
 * thread so it can never slow down or race with the real decode path.
 */
class RawDataArchiver : public DataProcessor {
    public:
        RawDataArchiver() = default;
        ~RawDataArchiver() override;

        bool open(const std::string &filename);

        void connect(ClipBoard<RawDataContainer> *arg_input) {
            input = arg_input;
        }

        void init() override {}
        void run() override;
        void join() override;
        void process() override;
        void process_core();

    private:
        ClipBoard<RawDataContainer> *input = nullptr;
        std::fstream fileHandle;
        std::unique_ptr<std::thread> thread_ptr;
};

#endif
