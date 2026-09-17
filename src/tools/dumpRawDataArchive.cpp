#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>

#include "RawDataArchiveFormat.h"

namespace {
    std::string loopStyleName(uint8_t style) {
        switch (style) {
            case 0: return "DATA";
            case 1: return "TRIGGER";
            case 2: return "MASK";
            case 3: return "PARAMETER";
            case 4: return "PIXEL_FEEDBACK";
            case 5: return "GLOBAL_FEEDBACK";
            case 6: return "TRIGGER_FEEDBACK";
            case 7: return "NOP";
            default: return "UNKNOWN";
        }
    }
}

void usage(char* argv[])
{
    std::cout << "Usage: " << argv[0] << " [options] inputFile1 inputFile2 ...\n"
        " -n <int>: max number of words to print per chunk (default 16, 0 = all)\n"
        " -w <int>: words per line in hex dump (default 8)\n"
        " -q : quiet, only print running totals at the end" << std::endl;
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv);
        return -1;
    }

    int c;
    unsigned n_words = 16;
    unsigned words_per_line = 8;
    bool quiet = false;

    while ((c = getopt(argc, argv, "hn:w:q")) != -1) {
        switch (c) {
        case 'h': usage(argv); return 0;
        case 'n': n_words = std::stoul(optarg); break;
        case 'w': words_per_line = std::stoul(optarg); break;
        case 'q': quiet = true; break;
        default: usage(argv); return -1;
        }
    }

    unsigned long totalRecords = 0, totalChunks = 0, totalWords = 0;
    std::map<uint32_t, unsigned long> wordsPerChannel;

    for (int n = optind; n < argc; n++) {
        std::fstream inputFile(argv[n], std::fstream::in | std::fstream::binary);
        if (!inputFile.good()) {
            std::cout << "Could not open file: " << argv[n] << std::endl;
            continue;
        }

        RawDataArchiveHeader header;
        inputFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!inputFile || std::string(header.magic, 7) != "YARRRAW") {
            std::cout << "Not a RawDataArchive file (bad magic): " << argv[n] << std::endl;
            continue;
        }

        if (!quiet) {
            std::cout << "File: " << argv[n]
                      << " (format version " << header.formatVersion << ")" << std::endl;
        }

        unsigned long iteration = 0;
        while (true) {
            RawDataArchiveRecordHeader recHeader;
            inputFile.read(reinterpret_cast<char*>(&recHeader), sizeof(recHeader));
            if (!inputFile) break;

            totalRecords++;

            if (!quiet) {
                std::cout << "Record " << iteration
                          << " isEndOfIteration=" << (int)recHeader.isEndOfIteration
                          << " nChunks=" << recHeader.nChunks
                          << " loopStatus=[";
                for (unsigned i = 0; i < recHeader.loopStatCount; i++) {
                    if (i) std::cout << ", ";
                    std::cout << (int)i << ":" << recHeader.loopStatVec[i]
                              << "(" << loopStyleName(recHeader.loopStyleVec[i]) << ")";
                }
                std::cout << "]" << std::endl;
            }

            for (uint32_t ch = 0; ch < recHeader.nChunks; ch++) {
                RawDataArchiveChunkHeader chunkHeader;
                inputFile.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));
                if (!inputFile) break;

                totalChunks++;
                totalWords += chunkHeader.nWords;
                wordsPerChannel[chunkHeader.rxChannel] += chunkHeader.nWords;

                if (!quiet) {
                    std::cout << "  Chunk rx=" << chunkHeader.rxChannel
                              << " nWords=" << chunkHeader.nWords << std::endl;
                }

                unsigned toPrint = (n_words == 0) ? chunkHeader.nWords
                                                   : std::min(n_words, chunkHeader.nWords);
                for (uint32_t w = 0; w < chunkHeader.nWords; w++) {
                    uint32_t word = 0;
                    inputFile.read(reinterpret_cast<char*>(&word), sizeof(word));
                    if (!quiet && w < toPrint) {
                        if (w % words_per_line == 0) std::cout << "    ";
                        std::cout << std::hex << std::setw(8) << std::setfill('0')
                                  << word << std::dec << " ";
                        if (w % words_per_line == words_per_line - 1) std::cout << std::endl;
                    }
                }
                if (!quiet && toPrint % words_per_line != 0) std::cout << std::endl;
                if (!quiet && toPrint < chunkHeader.nWords) {
                    std::cout << "    ... (" << (chunkHeader.nWords - toPrint) << " more words)" << std::endl;
                }
            }

            if (recHeader.isEndOfIteration) iteration++;
        }
    }

    std::cout << "--- Totals ---" << std::endl;
    std::cout << "Records: " << totalRecords << std::endl;
    std::cout << "Chunks:  " << totalChunks << std::endl;
    std::cout << "Words:   " << totalWords << std::endl;
    for (const auto &[ch, words] : wordsPerChannel) {
        std::cout << "  rx " << ch << ": " << words << " words" << std::endl;
    }
    std::cout << "Done, bye!" << std::endl;
    return 0;
}
