#ifndef RAWDATAARCHIVEFORMAT_H
#define RAWDATAARCHIVEFORMAT_H

// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Binary format for pre-decode raw data archives
// ################################

#include <cstdint>

/// Written once at the start of a *_rawdata.bin file
struct RawDataArchiveHeader {
    char     magic[8] = {'Y','A','R','R','R','A','W', 0};
    uint32_t formatVersion = 1;
};

/// Written once per RawDataContainer (i.e. per LoopStatus record)
struct RawDataArchiveRecordHeader {
    uint8_t  isEndOfIteration = 0;
    uint8_t  loopStatCount = 0;     // <= 8
    uint32_t loopStatVec[8] = {};
    uint8_t  loopStyleVec[8] = {};
    uint32_t nChunks = 0;
};

/// Written once per RawData chunk within a record, followed by nWords raw uint32_t words
struct RawDataArchiveChunkHeader {
    uint32_t rxChannel = 0;
    uint32_t nWords = 0;
};

#endif
