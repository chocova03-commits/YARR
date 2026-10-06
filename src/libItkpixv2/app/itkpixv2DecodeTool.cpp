// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Visual bit-level decoder for ITkPixV2 (RD53C) raw data,
// #              reading from the RawDataArchiver binary format
// ################################

#include <bitset>
#include <cstdint>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "LUT_PlainHMapToColRow.h"
#include "RawData.h"
#include "RawDataArchiveFormat.h"

// Field categories used to colorize the terminal trace. Purely presentational;
// has no bearing on the bit-decode logic below.
enum class FieldColor { NONE, HEADER, CCOL, MARKER, FRAMING, QROW, HITMAP, TOT, PTOT, REMAIN };

static const char* ansiFor(FieldColor c)
{
    switch (c) {
        case FieldColor::HEADER:  return "\033[1;34m"; // bold blue: ES/Tag/ChipID
        case FieldColor::CCOL:    return "\033[1;36m"; // bold cyan: core column
        case FieldColor::MARKER:  return "\033[1;31m"; // bold red: end-of-stream/internal tag
        case FieldColor::FRAMING: return "\033[2;37m"; // dim white: islast/isnext
        case FieldColor::QROW:    return "\033[0;33m"; // yellow: qrow
        case FieldColor::HITMAP:  return "\033[0;32m"; // green: hit map bits
        case FieldColor::TOT:     return "\033[0;35m"; // magenta: ToT
        case FieldColor::PTOT:    return "\033[1;35m"; // bold magenta: PToT/PToA
        case FieldColor::REMAIN:  return "\033[2;90m"; // dim gray: carry-over remainder bits
        default:                  return "";
    }
}
static const char* ANSI_RESET = "\033[0m";

static FieldColor colorForName(const std::string& name)
{
    if (name.find("End of stream") != std::string::npos || name.find("Int tag") != std::string::npos)
        return FieldColor::MARKER;
    if (name.find("ES") != std::string::npos || name.find("Tag") != std::string::npos ||
        name.find("ChipID") != std::string::npos)
        return FieldColor::HEADER;
    if (name.find("ccol") != std::string::npos)
        return FieldColor::CCOL;
    if (name.find("islast") != std::string::npos || name.find("isnext") != std::string::npos)
        return FieldColor::FRAMING;
    if (name.find("qrow") != std::string::npos)
        return FieldColor::QROW;
    if (name.find("Hit map") != std::string::npos)
        return FieldColor::HITMAP;
    if (name.find("PToT") != std::string::npos || name.find("PToA") != std::string::npos)
        return FieldColor::PTOT;
    if (name.find("ToT") != std::string::npos)
        return FieldColor::TOT;
    if (name.find("Remain") != std::string::npos)
        return FieldColor::REMAIN;
    return FieldColor::NONE;
}

#define BLOCKSIZE 64
#define HALFBLOCKSIZE 32
#define BINARYTREE_DEPTH 4

static constexpr uint8_t _LUT_BinaryTreeMaskSize[3] = {4, 2, 1};
static constexpr uint8_t _LUT_BinaryTreeMask[3][8] = {
    {0xF0, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00},
    {0xCF, 0x00, 0x3F, 0x00, 0xFC, 0x00, 0xF3, 0x00},
    {0xBF, 0x7F, 0xEF, 0xDF, 0xFB, 0xF7, 0xFE, 0xFD},
};

struct option longopts[] = {
    {"input", required_argument, nullptr, 'i'},
    {"output", required_argument, nullptr, 'o'},
    {"ne", required_argument, nullptr, 'n'},
    {"compressed", required_argument, nullptr, 'c'},
    {"chipid", required_argument, nullptr, 'C'},
    {"print", no_argument, nullptr, 'p'},
    {"color", required_argument, nullptr, 1000},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}};

// All raw data words from the input record(s), concatenated across chunks
std::vector<uint32_t> _buffer;
int _blockIdx = 0;
unsigned _bitIdx = 0;
uint32_t *_data = nullptr;
std::unique_ptr<RawData> _curIn;

// ITkPixV2-specific stream framing options, loaded from command line since
// this tool has no chip config JSON to read them from
bool _enChipId = false;
unsigned _chipIdShift = 0;
unsigned _chipId = 0;
uint64_t _streamMask = 0x7FFFFFFF;

void printHelp(const std::string& exe)
{
    std::cout << "Usage: " << exe << " [options]" << std::endl;
    std::cout << "Allowed options:" << std::endl;
    std::cout << " -i [ --input ] arg                 Input RawDataArchiver .bin file (required)" << std::endl;
    std::cout << " -o [ --output ] arg                Output file name (default decoded.txt, ignored with -p)" << std::endl;
    std::cout << " -n [ --ne ] arg                     Number of records to decode (default: all)" << std::endl;
    std::cout << " -c [ --compressed ] arg             Hit map compression enable (default: 1)" << std::endl;
    std::cout << " -C [ --chipid ] arg                 Enable chip ID readout, warn on streams with a different chip ID (0-3)" << std::endl;
    std::cout << " -p [ --print ]                      Print decode trace to terminal instead of a file" << std::endl;
    std::cout << " --color arg                         Colorize -p output: always|auto|never (default: auto)" << std::endl;
    std::cout << " -h [ --help ]                       Produce help message" << std::endl;
    std::cout << "Note: all records and rx channels of the file are decoded as one continuous stream;" << std::endl;
    std::cout << "      use a file with a single FE and dumpRawDataArchive to see record boundaries." << std::endl;
}

// One decoded field, e.g. "ccol", "Hit map", "ToT3". Kept as a discrete
// struct (rather than pre-joined into strings) so the terminal printer can
// wrap each field individually in an ANSI color, while save() can still
// join them into the original plain-text layout unmodified.
struct DecodeCell {
    std::string input;
    std::string output;
    std::string name;
    FieldColor color;
};

class Itkpixv2DecodeHelper
{
public:
    Itkpixv2DecodeHelper() { _remain = ""; }
    void fill(std::string input, const std::string& output, const std::string& name, const bool startNewLine = false);
    void save(const std::string& outputFileName);
    void print(bool useColor);
    void block(const uint64_t blockStream);
    void remain(const std::string& input);

private:
    static std::string decorate(const std::string& str, const unsigned length, const char separator);
    std::vector<std::vector<DecodeCell>> blocks;
    std::vector<uint64_t> blockBits;
    std::string _remain;
};

void Itkpixv2DecodeHelper::fill(std::string input, const std::string& output, const std::string& name, const bool startNewLine)
{
    if (startNewLine)
        blocks.emplace_back();
    if (blocks.back().size() == 1 && blocks.size() > 1 && _remain != "")
    {
        input = "(" + _remain + ")" + input.substr(_remain.length());
        _remain = "";
    }
    blocks.back().push_back({input, output, name, colorForName(name)});
}

std::string Itkpixv2DecodeHelper::decorate(const std::string& str, const unsigned length, const char separator)
{
    unsigned left = (length - str.length()) / 2;
    unsigned right = length - str.length() - left;
    std::string output = "";
    for (unsigned i = 0; i < left; i++)
        output += separator;
    output += str;
    for (unsigned i = 0; i < right; i++)
        output += separator;
    return output;
}

void Itkpixv2DecodeHelper::save(const std::string& outputFileName)
{
    std::ofstream fout(outputFileName);
    for (unsigned is = 0; is < blocks.size(); is++)
    {
        std::stringstream inputLine, outputLine, nameLine, sepLine;
        for (const auto &cell : blocks[is])
        {
            const unsigned maxLength = std::max({cell.input.length(), cell.output.length(), cell.name.length()}) + 1;
            inputLine << decorate(cell.input, maxLength, ' ');
            outputLine << decorate(cell.output, maxLength, ' ');
            nameLine << decorate(cell.name, maxLength, ' ');
            sepLine << decorate("", maxLength, '_');
        }
        fout << std::bitset<64>(blockBits[is]) << std::endl;
        fout << inputLine.str() << std::endl;
        fout << nameLine.str() << std::endl;
        fout << outputLine.str() << std::endl;
        fout << sepLine.str() << std::endl;
        fout << std::endl;
    }
    fout.close();
}

void Itkpixv2DecodeHelper::print(bool useColor)
{
    for (unsigned is = 0; is < blocks.size(); is++)
    {
        std::stringstream inputLine, outputLine, nameLine, sepLine;
        for (const auto &cell : blocks[is])
        {
            const unsigned maxLength = std::max({cell.input.length(), cell.output.length(), cell.name.length()}) + 1;
            const char* col = useColor ? ansiFor(cell.color) : "";
            const char* rst = useColor ? ANSI_RESET : "";
            inputLine << col << decorate(cell.input, maxLength, ' ') << rst;
            outputLine << col << decorate(cell.output, maxLength, ' ') << rst;
            nameLine << col << decorate(cell.name, maxLength, ' ') << rst;
            sepLine << decorate("", maxLength, '_');
        }
        std::cout << std::bitset<64>(blockBits[is]) << std::endl;
        std::cout << inputLine.str() << std::endl;
        std::cout << nameLine.str() << std::endl;
        std::cout << outputLine.str() << std::endl;
        std::cout << sepLine.str() << std::endl;
        std::cout << std::endl;
    }
}

void Itkpixv2DecodeHelper::block(const uint64_t blockStream)
{
    blockBits.push_back(blockStream);
}

void Itkpixv2DecodeHelper::remain(const std::string& input)
{
    fill(input, "", "Remain");
    _remain = input;
}

void startNewStream(Itkpixv2DecodeHelper *h, unsigned &nEvents)
{
    /* Start a new stream */
    _data = &_curIn->get(2 * _blockIdx);

    uint8_t tag = (_data[0] >> (23 - _chipIdShift)) & 0xFF;
    _blockIdx++; // Increase block index
    _bitIdx = 9 + _chipIdShift; // Reset bit index = ES + (chip ID) + tag

    h->block((uint64_t(_data[0]) << 32) | _data[1]);
    h->fill(std::bitset<1>(_data[0] >> 31).to_string(), "", "ES", true);
    if (_enChipId) {
        const unsigned chipId = (_data[0] >> 29) & 0x3;
        h->fill(std::bitset<2>(chipId).to_string(), "", "ChipID");
        if (chipId != _chipId)
            std::cerr << "Stream in block " << _blockIdx - 1 << " has chip ID " << chipId << ", expected " << _chipId << std::endl;
    }
    h->fill(std::bitset<8>(tag).to_string(), std::to_string(tag), "Tag");
    nEvents++;
}

uint64_t retrieve(Itkpixv2DecodeHelper *h, const unsigned length, const bool checkEOS = false)
{
    if (length == 0)
        return 0;
    if (checkEOS && (_data[0] >> 31) && _bitIdx == (1 + _chipIdShift))
    { // Corner case where end of event mark (000000) is suppressed and there is no orphan bits
        _blockIdx--; // Roll back block index by 1. A new stream will start in the next loop iteration
        return 0;
    }
    uint64_t variable = 0;

    if (_bitIdx + length <= BLOCKSIZE)
    { // Fully contained in current 64-bit block
        variable = (((_bitIdx + length) <= HALFBLOCKSIZE) || (_bitIdx >= HALFBLOCKSIZE)) ? ((_data[_bitIdx / HALFBLOCKSIZE] & (0xFFFFFFFFUL >> (_bitIdx - ((_bitIdx >> 5) << 5)))) >> ((((_bitIdx >> 5) + 1) << 5) - length - _bitIdx))
                                                                                               : (((_data[0] & (0xFFFFFFFFUL >> _bitIdx)) << (length + _bitIdx - HALFBLOCKSIZE)) | (_data[1] >> (BLOCKSIZE - length - _bitIdx)));
        _bitIdx += length; // Move bit index
    }
    else
    {
        if (checkEOS && ((_data[0] >> 31) & 0x1))
        { // Check end of stream
            return 0;
        }

        variable = (((_bitIdx < HALFBLOCKSIZE) ? (((_data[0] & (0xFFFFFFFFUL >> _bitIdx)) << HALFBLOCKSIZE) | _data[1])
                                                  : (_bitIdx == BLOCKSIZE ? 0
                                                                             : (_data[1] & (0xFFFFFFFFUL >> (_bitIdx - HALFBLOCKSIZE)))))
                    << (length + _bitIdx - BLOCKSIZE)) |
                   (((_bitIdx + length) < (HALFBLOCKSIZE + BLOCKSIZE)) ? ((_data[2] & _streamMask) >> (95 - (_bitIdx + length) - _chipIdShift))
                                                                          : (((_data[2] & _streamMask) << (_bitIdx + length - 95 - _chipIdShift)) | (_data[3] >> (127 - (length + _bitIdx) - _chipIdShift))));

        if (_bitIdx < BLOCKSIZE)
            h->remain((std::bitset<64>((uint64_t(_data[0]) << 32) | _data[1]).to_string()).substr(_bitIdx));

        _blockIdx++; // Increase block index
        _data = &_data[2];
        _bitIdx -= (63 - length - _chipIdShift); // Reset bit index. Note the ES bit (and chip ID, if enabled) is also counted
        h->fill(std::bitset<1>(_data[0] >> 31).to_string(), "", "ES", true);
        if (_enChipId)
            h->fill(std::bitset<2>((_data[0] >> 29) & 0x3).to_string(), "", "ChipID");
        h->block((uint64_t(_data[0]) << 32) | _data[1]);
    }

    return variable;
}

void rollBack(const unsigned length)
{ // Roll back bit index
    if (length == 0)
        return;
    if (_bitIdx >= (length + 1 + _chipIdShift))
        _bitIdx -= length; // Keep in mind there is one extra bit from ES (and chip ID, if enabled)
    else
    { // Across block, roll back by length
        _bitIdx += (63 - length - _chipIdShift);
        _data = &_curIn->get(2 * (--_blockIdx - 1));
    }
}

uint8_t getBitPair(Itkpixv2DecodeHelper *h)
{
    const uint8_t bitpair = retrieve(h, 2);
    // There is only 11 and 10, and 01 is replaced with 0
    // Whenever 00 or 01 show up, replace the first 0 with 01, and move 1-bit back
    if (bitpair == 0x0 || bitpair == 0x1)
    {
        rollBack(1); // Moving bit index back by 1
        h->fill("0", "0(1)", "");
        return 0x1;
    }
    else
    {
        h->fill(std::bitset<2>(bitpair).to_string(), std::bitset<2>(bitpair).to_string(), "");
        return bitpair;
    }
}

uint16_t readRow(Itkpixv2DecodeHelper *h)
{
    uint16_t hitmap = 0XFF; // Start from all 1. Pixels not fired will be masked by 0
    uint8_t nRead[BINARYTREE_DEPTH] = {1, 0, 0, 0};
    uint8_t nShift[BINARYTREE_DEPTH][16] = {{0}};
    for (uint8_t lv = 0; lv < BINARYTREE_DEPTH - 1; lv++)
    { // For each row there
        for (uint8_t ir = 0; ir < nRead[lv]; ir++)
        {
            uint8_t bitpair = getBitPair(h);
            switch (bitpair)
            {
            case 0x1: // 01
                hitmap &= _LUT_BinaryTreeMask[lv][nShift[lv][ir]];
                nShift[lv + 1][nRead[lv + 1]++] = nShift[lv][ir];
                break;
            case 0x2: // 10
                hitmap &= _LUT_BinaryTreeMask[lv][nShift[lv][ir] + _LUT_BinaryTreeMaskSize[lv]];
                nShift[lv + 1][nRead[lv + 1]++] = nShift[lv][ir] + _LUT_BinaryTreeMaskSize[lv];
                break;
            case 0x3: // 11
                hitmap &= (_LUT_BinaryTreeMask[lv][nShift[lv][ir]] | _LUT_BinaryTreeMask[lv][nShift[lv][ir] + _LUT_BinaryTreeMaskSize[lv]]);
                nShift[lv + 1][nRead[lv + 1]++] = nShift[lv][ir] + _LUT_BinaryTreeMaskSize[lv];
                nShift[lv + 1][nRead[lv + 1]++] = nShift[lv][ir];
                break;
            default: break;
            }
        }
    }
    return hitmap;
}

// Read every chunk of every record from a RawDataArchiver .bin file into a
// flat word buffer, mirroring what the real Itkpixv2DataProcessor sees on
// its clipRawData input (minus the LoopStatus bookkeeping, irrelevant here).
int readInData(const std::string& inputFileName, int nRecords = -1)
{
    std::cout << "Reading raw data archive...";
    std::ifstream fstream(inputFileName, std::ios::binary);
    if (!fstream.good()) {
        std::cerr << "Could not open input file: " << inputFileName << std::endl;
        return 0;
    }

    RawDataArchiveHeader header;
    fstream.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!fstream || !header.valid()) {
        std::cerr << "Not a RawDataArchive file (format version " << RawDataArchiveHeader::currentVersion << "): " << inputFileName << std::endl;
        return 0;
    }

    unsigned words = 0;
    int recordCnt = 0;
    while (true) {
        RawDataArchiveRecordHeader recHeader;
        fstream.read(reinterpret_cast<char*>(&recHeader), sizeof(recHeader));
        if (!fstream) break;

        for (uint32_t c = 0; c < recHeader.nChunks; c++) {
            RawDataArchiveChunkHeader chunkHeader;
            fstream.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));
            if (!fstream) break;

            for (uint32_t w = 0; w < chunkHeader.nWords; w++) {
                uint32_t word = 0;
                fstream.read(reinterpret_cast<char*>(&word), sizeof(word));
                _buffer.push_back(word);
                words++;
            }
        }

        recordCnt++;
        if (nRecords > 0 && recordCnt >= nRecords)
            break;
    }
    fstream.close();

    // Pad with two empty 32-bit words so the bit-retrieval logic can safely
    // over-read at the very end of the buffer, matching rd53bDecodeTool.
    _buffer.push_back(0);
    _buffer.push_back(0);
    words += 2;

    std::unique_ptr<RawData> data(new RawData(0, words));
    std::copy(_buffer.begin(), _buffer.end(), data->getBuf());
    _curIn = std::move(data);
    std::cout << "Done. (" << recordCnt << " records, " << (words - 2) << " words)" << std::endl;
    return words;
}

int main(int argc, char **argv)
{
    std::string inputFileName = "";
    std::string outputFileName = "decoded.txt";
    bool compressed = true;
    int nRecords = -1;
    bool printToTerminal = false;
    std::string colorMode = "auto";
    int oc;
    while ((oc = getopt_long(argc, argv, ":i:o:c:n:C:ph", longopts, nullptr)) != -1)
    {
        switch (oc)
        {
        case 'i':
            inputFileName = optarg;
            break;
        case 'o':
            outputFileName = optarg;
            std::cout << "Set output file name to: " << outputFileName << std::endl;
            break;
        case 'c':
            compressed = atoi(optarg);
            std::cout << "Set hit map compression to: " << compressed << std::endl;
            break;
        case 'n':
            nRecords = atoi(optarg);
            std::cout << "Set number of records to decode to: " << nRecords << std::endl;
            break;
        case 'C':
            _enChipId = true;
            _chipIdShift = 2;
            _chipId = atoi(optarg) & 0x3;
            _streamMask = 0x1FFFFFFF;
            std::cout << "Enabled chip ID readout, expecting chip ID: " << _chipId << std::endl;
            break;
        case 'p':
            printToTerminal = true;
            break;
        case 1000: // --color
            colorMode = optarg;
            break;
        case 'h':
            printHelp(argv[0]);
            return 0;
        case ':': /* missing option argument */
            (void)fprintf(stderr, "%s: option `-%c' requires an argument\n",
                    argv[0], optopt);
            printHelp(argv[0]);
            return 1;
        case '?':
        default:
            (void)fprintf(stderr, "%s: option `-%c' is invalid: ignored\n",
                    argv[0], optopt);
            printHelp(argv[0]);
            return 1;
        }
    }

    if (inputFileName == "")
    {
        std::cerr << "Input file not provided" << std::endl;
        return 1;
    }

    if (colorMode != "always" && colorMode != "auto" && colorMode != "never")
    {
        std::cerr << "Invalid --color value `" << colorMode << "', expected always|auto|never" << std::endl;
        return 1;
    }
    const bool useColor = (colorMode == "always") || (colorMode == "auto" && isatty(fileno(stdout)));

    auto helper = std::make_unique<Itkpixv2DecodeHelper>();
    Itkpixv2DecodeHelper *h = helper.get();

    if (readInData(inputFileName, nRecords) == 0)
        return 1;
    const unsigned blocks = _curIn->getSize() / 2;
    uint8_t qrow = 0, ccol = 0;
    uint8_t islast = 0, isneighbor = 0;

    unsigned nHits = 0, nEvents = 0;
    startNewStream(h, nEvents);
    while (_blockIdx <= (int)blocks)
    {
        // Start from getting core column
        ccol = retrieve(h, 6, true);
        // End of stream marked with 000000 in current stream
        if (ccol == 0)
        {
            h->fill(std::bitset<6>(ccol).to_string(), "", "End of stream");
            if (_blockIdx >= (int)blocks)
                break; // End of data processing
            startNewStream(h, nEvents);
            continue;
        }
        else if (ccol >= 0x38)
        { // Internal tag
            const uint16_t intTag = (ccol << 5) | retrieve(h, 5);
            h->fill(std::bitset<11>(intTag).to_string(), "", "Int tag");
            nEvents++;
            continue;
        }
        h->fill(std::bitset<6>(ccol).to_string(), std::to_string(ccol), "ccol");
        // Loop over all the hits
        do
        {
            if (_blockIdx > (int)blocks)
                break; // End of data processing
            islast = retrieve(h, 1);
            isneighbor = retrieve(h, 1);
            h->fill(std::bitset<1>(islast).to_string(), "", "islast");
            h->fill(std::bitset<1>(isneighbor).to_string(), "", "isnext");

            if (isneighbor)
                ++qrow;
            else
            {
                qrow = retrieve(h, 8);
                h->fill(std::bitset<8>(qrow).to_string(), std::to_string(qrow), "qrow");
            }

            uint16_t hitmap = 0x0;
            if (compressed){
                uint8_t root = getBitPair(h);
                if (root & 0x2)
                { // Read the first row
                    hitmap = (readRow(h) & 0x00FF);
                }
                if (root & 0x1)
                { // Read the second row
                    hitmap |= (readRow(h) << 8);
                }
                h->fill("", std::bitset<16>(hitmap).to_string(), "Hit map");
            }
            else
            {
                hitmap = retrieve(h, 16);
                h->fill(std::bitset<16>(hitmap).to_string(), "", "Hit map");
            }

            // Precision ToT data
            if (qrow == 196) {
                for (int ibus = 0; ibus < 4; ibus++) {
                    uint8_t hitsub = (hitmap >> (ibus << 2)) & 0xF;
                    if (hitsub) {
                        uint16_t ptot_ptoa_buf = 0xFFFF;
                        for(int iread = 0; iread < 4; iread++){
                            if ((hitsub >> iread) & 0x1){
                                uint8_t fourbit = retrieve(h, 4);
                                ptot_ptoa_buf &= ~((~fourbit & 0xF) << (iread << 2));
                                h->fill(std::bitset<4>(fourbit).to_string(), "", "");
                            }
                            else
                                h->fill("(1111)", "", "");
                        }

                        uint16_t PToT = ptot_ptoa_buf & 0x7FF;
                        uint8_t PToA = ptot_ptoa_buf >> 11;
                        h->fill("", std::to_string(PToT), "PToT" + std::to_string(ibus));
                        h->fill("", std::to_string(PToA), "PToA" + std::to_string(ibus));
                    }
                }
            }
            else{
                nHits += RD53BDecoding::_LUT_PlainHMap_To_ColRow_ArrSize[hitmap];
                for (int ihit = 0; ihit < RD53BDecoding::_LUT_PlainHMap_To_ColRow_ArrSize[hitmap]; ++ihit)
                {
                    uint8_t pix_tot = retrieve(h, 4);
                    h->fill(std::bitset<4>(pix_tot).to_string(), std::to_string(pix_tot), "ToT" + std::to_string(ihit));
                }
            }
        } while (!islast);
    }

    if (printToTerminal)
    {
        h->print(useColor);
    }
    else
    {
        std::cout << "Output decoded file saved in " << outputFileName << std::endl;
        h->save(outputFileName);
    }
    std::cout << "Number of events processed: " << nEvents
          << ", number of hits processed: " << nHits
          << std::endl;
}
