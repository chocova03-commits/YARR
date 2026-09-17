#include "catch.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

#include "ClipBoard.h"
#include "LoopStatus.h"
#include "RawData.h"
#include "RawDataArchiver.h"
#include "RawDataArchiveFormat.h"

TEST_CASE("RawDataArchiverRoundTrip", "[raw_data_archiver]") {
    const std::string filename = "test_raw_data_archiver_output.bin";

    LoopStatus stat({3, 7}, {LOOP_STYLE_MASK, LOOP_STYLE_DATA});
    stat.is_end_of_iteration = false;

    auto rdc = std::make_unique<RawDataContainer>(stat);
    std::vector<uint32_t> words0{0x12345678, 0xdeadbeef, 0x00000000};
    std::vector<uint32_t> words1{0xaaaaaaaa};
    rdc->add(std::make_shared<RawData>(0, words0));
    rdc->add(std::make_shared<RawData>(1, words1));

    LoopStatus eoiStat = stat;
    eoiStat.is_end_of_iteration = true;
    auto eoiRdc = std::make_unique<RawDataContainer>(eoiStat);

    ClipBoard<RawDataContainer> clip;

    RawDataArchiver archiver;
    REQUIRE(archiver.open(filename));
    archiver.connect(&clip);
    archiver.init();
    archiver.run();

    clip.pushData(std::move(rdc));
    clip.pushData(std::move(eoiRdc));
    clip.finish();
    archiver.join();

    std::fstream in(filename, std::fstream::in | std::fstream::binary);
    REQUIRE(in.good());

    RawDataArchiveHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    REQUIRE(in.good());
    CHECK(std::string(header.magic, 7) == "YARRRAW");
    CHECK(header.formatVersion == 1);

    RawDataArchiveRecordHeader rec0;
    in.read(reinterpret_cast<char*>(&rec0), sizeof(rec0));
    REQUIRE(in.good());
    CHECK(rec0.isEndOfIteration == 0);
    CHECK(rec0.loopStatCount == 2);
    CHECK(rec0.loopStatVec[0] == 3);
    CHECK(rec0.loopStatVec[1] == 7);
    CHECK(rec0.loopStyleVec[0] == LOOP_STYLE_MASK);
    CHECK(rec0.loopStyleVec[1] == LOOP_STYLE_DATA);
    CHECK(rec0.nChunks == 2);

    RawDataArchiveChunkHeader chunk0;
    in.read(reinterpret_cast<char*>(&chunk0), sizeof(chunk0));
    REQUIRE(in.good());
    CHECK(chunk0.rxChannel == 0);
    CHECK(chunk0.nWords == words0.size());
    std::vector<uint32_t> readWords0(chunk0.nWords);
    in.read(reinterpret_cast<char*>(readWords0.data()), chunk0.nWords * sizeof(uint32_t));
    REQUIRE(in.good());
    CHECK(readWords0 == words0);

    RawDataArchiveChunkHeader chunk1;
    in.read(reinterpret_cast<char*>(&chunk1), sizeof(chunk1));
    REQUIRE(in.good());
    CHECK(chunk1.rxChannel == 1);
    CHECK(chunk1.nWords == words1.size());
    std::vector<uint32_t> readWords1(chunk1.nWords);
    in.read(reinterpret_cast<char*>(readWords1.data()), chunk1.nWords * sizeof(uint32_t));
    REQUIRE(in.good());
    CHECK(readWords1 == words1);

    RawDataArchiveRecordHeader rec1;
    in.read(reinterpret_cast<char*>(&rec1), sizeof(rec1));
    REQUIRE(in.good());
    CHECK(rec1.isEndOfIteration == 1);
    CHECK(rec1.nChunks == 0);

    // No further records
    RawDataArchiveRecordHeader recEnd;
    in.read(reinterpret_cast<char*>(&recEnd), sizeof(recEnd));
    CHECK(in.eof());

    in.close();
    std::remove(filename.c_str());
}
