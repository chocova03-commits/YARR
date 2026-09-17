// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Archives pre-decode raw hardware data to disk
// ################################

#include "RawDataArchiver.h"

#include "RawDataArchiveFormat.h"
#include "logging.h"

namespace {
    auto rdalog = logging::make_log("RawDataArchiver");
}

RawDataArchiver::~RawDataArchiver() {
    if (fileHandle.is_open()) fileHandle.close();
}

bool RawDataArchiver::open(const std::string &filename) {
    fileHandle.open(filename.c_str(), std::fstream::out | std::fstream::binary | std::fstream::trunc);
    if (!fileHandle.good()) return false;

    RawDataArchiveHeader header;
    fileHandle.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return fileHandle.good();
}

void RawDataArchiver::run() {
    thread_ptr = std::make_unique<std::thread>(&RawDataArchiver::process, this);
}

void RawDataArchiver::join() {
    if (thread_ptr && thread_ptr->joinable()) thread_ptr->join();
    if (fileHandle.is_open()) fileHandle.close();
}

void RawDataArchiver::process() {
    while (true) {
        input->waitNotEmptyOrDone();

        process_core();

        if (input->isDone()) {
            process_core(); // catch anything pushed just before finish()
            rdalog->info("RawDataArchiver done!");
            break;
        }
    }
}

void RawDataArchiver::process_core() {
    while (!input->empty()) {
        auto rdc = input->popData();
        if (!rdc) continue;
        if (!fileHandle.is_open()) continue;

        RawDataArchiveRecordHeader recHeader;
        recHeader.isEndOfIteration = rdc->stat.is_end_of_iteration ? 1 : 0;
        recHeader.loopStatCount = static_cast<uint8_t>(rdc->stat.size());
        for (unsigned i = 0; i < rdc->stat.size(); i++) {
            recHeader.loopStatVec[i] = rdc->stat.get(i);
            recHeader.loopStyleVec[i] = static_cast<uint8_t>(rdc->stat.getStyle(i));
        }
        recHeader.nChunks = static_cast<uint32_t>(rdc->data.size());

        fileHandle.write(reinterpret_cast<const char*>(&recHeader), sizeof(recHeader));

        for (const RawDataPtr &raw : rdc->data) {
            RawDataArchiveChunkHeader chunkHeader;
            chunkHeader.rxChannel = raw->getAdr();
            chunkHeader.nWords = raw->getSize();
            fileHandle.write(reinterpret_cast<const char*>(&chunkHeader), sizeof(chunkHeader));

            for (unsigned i = 0; i < chunkHeader.nWords; i++) {
                uint32_t word = raw->get(i);
                fileHandle.write(reinterpret_cast<const char*>(&word), sizeof(word));
            }
        }
    }
}
