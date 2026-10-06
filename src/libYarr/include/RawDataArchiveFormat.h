#ifndef RAWDATAARCHIVEFORMAT_H
#define RAWDATAARCHIVEFORMAT_H

// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Binary format for pre-decode raw data archives
// ################################

#include <cstdint>

// All structs are written to disk as-is (host byte order). Fields are ordered so
// that there is no implicit padding; the static_asserts guard the on-disk layout.

/// Written once at the start of a *_rawdata.bin file
struct RawDataArchiveHeader {
    static constexpr uint32_t currentVersion = 2;
    char     magic[8] = {'Y','A','R','R','R','A','W', 0};
    uint32_t formatVersion = currentVersion;

    bool valid() const {
        return magic[0] == 'Y' && magic[1] == 'A' && magic[2] == 'R' && magic[3] == 'R' &&
               magic[4] == 'R' && magic[5] == 'A' && magic[6] == 'W' && magic[7] == 0 &&
               formatVersion == currentVersion;
    }
};

/// Written once per RawDataContainer (i.e. per LoopStatus record)
struct RawDataArchiveRecordHeader {
    uint32_t loopStatVec[8] = {};
    uint32_t nChunks = 0;
    uint8_t  loopStyleVec[8] = {};
    uint8_t  isEndOfIteration = 0;
    uint8_t  loopStatCount = 0;     // <= 8
    uint8_t  reserved[2] = {};
};

/// Written once per RawData chunk within a record, followed by nWords raw uint32_t words
struct RawDataArchiveChunkHeader {
    uint32_t rxChannel = 0;
    uint32_t nWords = 0;
};

static_assert(sizeof(RawDataArchiveHeader) == 12, "RawDataArchiveHeader layout changed");
static_assert(sizeof(RawDataArchiveRecordHeader) == 48, "RawDataArchiveRecordHeader layout changed");
static_assert(sizeof(RawDataArchiveChunkHeader) == 8, "RawDataArchiveChunkHeader layout changed");

#endif
