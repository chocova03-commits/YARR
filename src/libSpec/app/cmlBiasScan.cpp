#include <cstdint>
#include <fstream>
#include <string>
#include <iomanip>
#include <iostream>

#include <filesystem>
namespace fs = std::filesystem;

#include <unistd.h>

#include "SpecCom.h"
#include "logging.h"
#include "LoggingConfig.h"
#include "storage.hpp"
// YARR
#include "HwController.h"
#include "FrontEndCfg.h"
#include "AllChips.h"
#include "ScanHelper.h"
#include "Utils.h"
#include "Histo2d.h"


auto logger = logging::make_log("cmlBiasScan");

// Bias the chosen delay towards the low edge of the eye rather than dead
// center: the high edge has been observed to degrade faster than the low
// edge, so centering leaves less margin than intended.
constexpr double EYE_LOW_EDGE_FRACTION = 0.25;

void printHelp() {
    std::cout << "Usage: ./bin/cmlBiasScan [-h] [-r <hw_controller_file>] [-c <connectivity_file>] [-t <test_size>] [-s] [-o <output_dir>] [-p] [-a <min>] [-A <max>] [-b <min>] [-B <max>] [-i <step>] [-u]\n\n"
        << "Options:\n"
        << "  -h                   Display this help message.\n"
        << "  -r <hw_controller_file>   Specify hardware controller JSON path.\n"
        << "  -c <connectivity_file>    Specify connectivity config JSON path.\n"
        << "  -t <test_size>            Specify the error counter test size. Default 1 x 10^6\n"
        << "  -s                   Skip chip configuration.\n"
        << "  -v                   Print out and store raw error counter values.\n"
        << "  -o <output_dir>           Base directory for results; a run-numbered subdirectory (e.g. 000001_cmlBiasScan/) is created within it. Default ./data/\n"
        << "  -p                   Also produce a PNG plot for each chip's result.\n"
        << "  -a <min>                  CmlBias0 minimum. Default 300\n"
        << "  -A <max>                  CmlBias0 maximum. Default 1000\n"
        << "  -b <min>                  CmlBias1 minimum. Default 0\n"
        << "  -B <max>                  CmlBias1 maximum. Default 500\n"
        << "  -i <step>                 Increment/step size for both CmlBias0 and CmlBias1. Default 100\n"
        << "  -u                   Save the optimal CmlBias0/CmlBias1 (largest eye width) to each chip's config and the corresponding delay setting to the controller config. Default false\n";
}

std::unique_ptr<FrontEnd> init_fe(std::unique_ptr<HwController>& hw, json &jconn, int fe_num) {
    std::string chip_type = jconn["chipType"];
    auto fe = StdDict::getFrontEnd(chip_type);
    auto cfg = dynamic_cast<FrontEndCfg*>(fe.get());
    auto chip_configs = jconn["chips"];
    if(fe_num >= chip_configs.size()) {
        std::stringstream e;
        e << "Invalid FE index (" << fe_num << ") for connectivity file with " << chip_configs.size() << " chips";
        throw std::runtime_error(e.str());
    }
    auto chip_config = chip_configs[fe_num];
    unsigned regRx = chip_config["rx"];
    if(chip_config.contains("regRx"))
        regRx = chip_config["regRx"];
    fe->init(&*hw, FrontEndConnectivity(chip_config["tx"], chip_config["rx"], regRx));
    auto chip_register_file_path = chip_config["__config_path__"];
    fs::path pconfig{chip_register_file_path};
    if(!fs::exists(pconfig)) {
        std::cerr << "WARNING: Chip config \"" << chip_register_file_path << "\" not found" << std::endl;
        fe.reset();
        return fe;
    }
    auto chip_register_json = ScanHelper::openJsonFile(chip_register_file_path);
    cfg->loadConfig(chip_register_json);
    hw->setCmdEnable(cfg->getTxChannel());
    return fe;
}

// Run one full 32-position delay sweep and return, per lane, the width (in
// consecutive delay steps) of the widest run where 100% of packets were
// received (a "0" is returned for lanes with no valid eye), along with the
// delay setting biased towards the low edge of that widest run.
std::vector<std::pair<double,int>> measureEyeWidths(SpecCom &mySpec, uint32_t n_lanes, uint32_t test_size,
                                      double min, double max, int wait, bool print_raw_value) {
    std::vector<std::vector<double> > resultVec;
    resultVec.resize(n_lanes);
    for (uint32_t j=0; j<n_lanes; j++)
        resultVec[j].resize(32);

    mySpec.writeSingle(0x2 << 14 | 0x6, 0xffff);
    mySpec.writeSingle(0x2 << 14 | 0x8, test_size);
    mySpec.writeSingle(0x2 << 14 | 0x9, 0);

    for (uint32_t i = 0; i<32; i++) {
        for (uint32_t j = 0 ; j<n_lanes; j++) {
            mySpec.writeSingle(0x2 << 14 | 0x4, j);
            mySpec.writeSingle(0x2 << 14 | 0x5, i);
        }

        // Reset and restart error counter
        mySpec.writeSingle(0x2 << 14 | 0xb, 1);
        mySpec.writeSingle(0x2 << 14 | 0xb, 0);

        std::this_thread::sleep_for(std::chrono::microseconds(wait));

        for (uint32_t j = 0 ; j<n_lanes; j++) {
            mySpec.writeSingle(0x2 << 14 | 0xa, j);
            uint32_t errors = 0;
            errors = mySpec.readSingle(0x2<<14 | 0xb);
            double value=-1;
            if (((errors>>31)&0x1)) {
                double error_count = (0x7FFFFFFF & errors);
                if (error_count>=min && error_count<=max+1 ){
                    value = 1;
                } else {
                    value = 0;
                }
            }
            resultVec[j][i] = value;
        }
    }

    std::vector<std::pair<double,int>> widths;
    widths.resize(n_lanes);
    for (uint32_t i = 0 ; i<n_lanes; i++) {
        double start_val=0;
        double width=0;
        double last_width=0;
        double best_val=0;
        double best_width=0;
        for (uint32_t j = 0 ; j<32; j++) {
            if (resultVec[i][j]==1){
                if (width==0) start_val=j;
                width+=1;
                if (j==31){
                    if(width>last_width){
                        best_val=start_val;
                        best_width=width;
                    }
                }
            } else {
                if(width>last_width){
                    best_val=start_val;
                    best_width=width;
                }
                last_width=best_width;
                width=0;
            }
        }
        int delay = best_width!=0 ? (int)(best_val+(best_width*EYE_LOW_EDGE_FRACTION)) : 0;
        widths[i] = {best_width, delay};
    }
    return widths;
}

int main(int argc, char **argv) {
    // Setup logger with some defaults
    logging::setupLoggers(logging::defaultConfig());

    // Init spec
    logger->info("Init spec");
    int c;
    int specNum = 0;
    uint32_t n_lanes= 16;
    std::string hw_controller_filename = "";
    std::string connectivity_filename = "";
    std::string output_dir = "./data/";
    uint32_t test_size = 1000000;
    bool skip_config=false;
    bool print_raw_value=false;
    bool doPlots=false;
    bool save_optimal=false;

    int cmlBias0Min = 300;
    int cmlBias0Max = 1000;
    int cmlBias1Min = 0;
    int cmlBias1Max = 500;
    int step = 100;

    uint16_t cdrclksel = 0;
    uint16_t serblckperiod = 50;

    while ((c = getopt(argc, argv, "hr:c:t:svo:pa:A:b:B:i:u")) != -1) {
        switch (c) {
            case 'h':
                printHelp();
                return 0;
            case 'r':
                hw_controller_filename = optarg;
                break;
            case 'c' :
                connectivity_filename = optarg;
                break;
            case 't' :
                test_size = std::stoi(optarg);
                break;
            case 's' :
                skip_config = true;
                break;
            case 'v' :
                print_raw_value = true;
                break;
            case 'o' :
                output_dir = optarg;
                if (output_dir.back() != '/')
                    output_dir = output_dir + "/";
                break;
            case 'p' :
                doPlots = true;
                break;
            case 'a' :
                cmlBias0Min = std::stoi(optarg);
                break;
            case 'A' :
                cmlBias0Max = std::stoi(optarg);
                break;
            case 'b' :
                cmlBias1Min = std::stoi(optarg);
                break;
            case 'B' :
                cmlBias1Max = std::stoi(optarg);
                break;
            case 'i' :
                step = std::stoi(optarg);
                break;
            case 'u' :
                save_optimal = true;
                break;
            default:
                logger->critical("Invalid command line parameter(s) given!");
                return -1;
        }
    }

    fs::path hw_controller_path{hw_controller_filename};
    if(!fs::exists(hw_controller_path)) {
        std::cerr << "ERROR: Provided hw controller file (=" << hw_controller_filename << ") does not exist" << std::endl;
        return 1;
    }

    fs::path connectivity_path{connectivity_filename};
    if(!fs::exists(connectivity_path)) {
        std::cerr << "ERROR: Provided connectivity file (=" << connectivity_filename << ") does not exist" << std::endl;
        return 1;
    }

    std::string dataDir = output_dir;
    unsigned runCounter = ScanHelper::newRunCounter();

    json jcontroller;
    jcontroller = ScanHelper::openJsonFile(hw_controller_filename);
    specNum=jcontroller["ctrlCfg"]["cfg"]["specNum"];

    SpecCom mySpec(specNum);
    logger->info("Scanning link quality against CmlBias0/CmlBias1 on Spec Card {}", specNum);

    // instantiate the hw controller
    std::unique_ptr<HwController> hw;
    try {
        jcontroller = ScanHelper::openJsonFile(hw_controller_filename);
        hw = ScanHelper::loadController(jcontroller);
    } catch (std::exception& e) {
        std::cerr << "ERROR: Unable to load controller from provided config, exception caught: " << e.what() << std::endl;
        return 1;
    }
    hw->setupMode();
    hw->setTrigEnable(0);
    hw->disableRx(); // needed?

    auto jconn = ScanHelper::openJsonFile(connectivity_filename);

    std::string chipType = ScanHelper::loadChipConfigs(jconn, false, Utils::dirFromPath(connectivity_filename));
    auto chip_configs = jconn["chips"];
    size_t n_chips = chip_configs.size();

    std::vector<std::unique_ptr<FrontEnd>> fes;
    std::vector<uint16_t> origCmlBias0;
    std::vector<uint16_t> origCmlBias1;
    std::vector<std::string> chipConfigPaths;

    for (size_t ichip = 0; ichip < n_chips; ichip++) {
        if (chip_configs[ichip]["enable"] == 0)
            continue;
        fs::path chip_register_file_path{chip_configs[ichip]["__config_path__"]};
        if(!fs::exists(chip_register_file_path)) {
            std::cerr << "WARNING: Chip config for chip at index " << ichip << " in connectivity file does not exist, skipping (" << chip_register_file_path << ")" << std::endl;
            continue;
        }

        auto fe = init_fe(hw, jconn, ichip);
        if(!fe) {
            std::cerr << "WARNING: Skipping chip at index " << ichip << " in connectivity file" << std::endl;
            continue;
        }
        auto cfg = dynamic_cast<FrontEndCfg*>(fe.get());

        if (!skip_config){
            fe->configure();
        } else {
            logger->info("Skipping configuration!");
        }
        fe->getNamedRegister("CdrClkSel", cdrclksel);
        fe->getNamedRegister("ServiceBlockPeriod", serblckperiod);
        logger->info("Read \"CdrClkSel\" {} and \"ServiceBlockPeriod\" {} from virtual register read", cdrclksel, serblckperiod);

        // Wait for fifo to be empty
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        while(!hw->isCmdEmpty());

        uint16_t cmlBias0=0, cmlBias1=0;
        fe->getNamedRegister("CmlBias0", cmlBias0);
        fe->getNamedRegister("CmlBias1", cmlBias1);
        origCmlBias0.push_back(cmlBias0);
        origCmlBias1.push_back(cmlBias1);
        chipConfigPaths.push_back(chip_register_file_path.string());

        fes.push_back(std::move(fe));
    }

    if (fes.empty()) {
        std::cerr << "ERROR: No enabled chips could be initialized, aborting" << std::endl;
        return 1;
    }

    double clk_speed=37.5;
    double readout_speed=clk_speed*32*1e6/(cdrclksel+1);

    double time=0.0;
    time=1/readout_speed*66*test_size;
    double clkcycles = 1/(clk_speed*1e6);
    double count=time/clkcycles/(serblckperiod*2+1);
    double min=std::floor(count);
    double max=std::ceil(count);
    int wait = time*1e6*1.1;

    if (!skip_config){
        // Wait for sync
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    unsigned nBins0 = (cmlBias0Max - cmlBias0Min)/step + 1;
    unsigned nBins1 = (cmlBias1Max - cmlBias1Min)/step + 1;

    std::vector<std::unique_ptr<Histo2d>> laneHistos;
    for (uint32_t lane=0; lane<n_lanes; lane++) {
        auto h = std::make_unique<Histo2d>("CmlBiasScan",
                nBins0, cmlBias0Min-step/2.0, cmlBias0Max+step/2.0,
                nBins1, cmlBias1Min-step/2.0, cmlBias1Max+step/2.0);
        h->setXaxisTitle("CmlBias0");
        h->setYaxisTitle("CmlBias1");
        h->setZaxisTitle("EyeWidth");
        laneHistos.push_back(std::move(h));
    }

    std::vector<double> bestWidth(n_lanes, 0);
    std::vector<int> bestDelay(n_lanes, 0);
    std::vector<int> bestCmlBias0(n_lanes, 0);
    std::vector<int> bestCmlBias1(n_lanes, 0);

    for (int cmlBias0 = cmlBias0Min; cmlBias0 <= cmlBias0Max; cmlBias0 += step) {
        for (int cmlBias1 = cmlBias1Min; cmlBias1 <= cmlBias1Max; cmlBias1 += step) {
            if (cmlBias1 > (cmlBias0 - 100)) {
                logger->info("Skipping measurement at CmlBias0={}, CmlBias1={}, since (CmlBias1 > (CmlBias0 - 100)", cmlBias0, cmlBias1);
                for (uint32_t lane=0; lane<n_lanes; lane++) {
                    laneHistos[lane]->fill(cmlBias0, cmlBias1, 0);
                }
                continue;
            }
            for (auto &fe : fes) {
                auto cfg = dynamic_cast<FrontEndCfg*>(fe.get());
                hw->setCmdEnable(cfg->getTxChannel());
                fe->writeNamedRegister("CmlBias0", cmlBias0);
                fe->writeNamedRegister("CmlBias1", cmlBias1);
                while(!hw->isCmdEmpty());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            logger->info("Measuring eye widths at CmlBias0={}, CmlBias1={}", cmlBias0, cmlBias1);
            auto widths = measureEyeWidths(mySpec, n_lanes, test_size, min, max, wait, print_raw_value);
            for (uint32_t lane=0; lane<n_lanes; lane++) {
                laneHistos[lane]->fill(cmlBias0, cmlBias1, widths[lane].first);
                if (widths[lane].first > bestWidth[lane]) {
                    bestWidth[lane] = widths[lane].first;
                    bestDelay[lane] = widths[lane].second;
                    bestCmlBias0[lane] = cmlBias0;
                    bestCmlBias1[lane] = cmlBias1;
                }
            }
        }
    }

    // Restore original bias values
    for (size_t i=0; i<fes.size(); i++) {
        auto cfg = dynamic_cast<FrontEndCfg*>(fes[i].get());
        hw->setCmdEnable(cfg->getTxChannel());
        fes[i]->writeNamedRegister("CmlBias0", origCmlBias0[i]);
        fes[i]->writeNamedRegister("CmlBias1", origCmlBias1[i]);
        while(!hw->isCmdEmpty());
    }

    if (save_optimal) {
        for (size_t i=0; i<fes.size(); i++) {
            auto cfg = dynamic_cast<FrontEndCfg*>(fes[i].get());
            uint32_t lane = cfg->getRxChannel();
            if (bestWidth[lane] > 0) {
                fes[i]->setNamedRegister("CmlBias0", bestCmlBias0[lane]);
                fes[i]->setNamedRegister("CmlBias1", bestCmlBias1[lane]);
                jcontroller["ctrlCfg"]["cfg"]["delay"][lane] = bestDelay[lane];
                logger->info("Chip {} (lane {}): saving optimal CmlBias0={}, CmlBias1={}, delay={} (eye width {})",
                             cfg->getName(), lane, bestCmlBias0[lane], bestCmlBias1[lane], bestDelay[lane], bestWidth[lane]);
            } else {
                jcontroller["ctrlCfg"]["cfg"]["delay"][lane] = 0;
                logger->warn("Chip {} (lane {}): no valid eye found, leaving CmlBias0/CmlBias1 unchanged and setting delay=0", cfg->getName(), lane);
            }

            json chipJson;
            cfg->writeConfig(chipJson);
            std::ofstream chipConfigFile(chipConfigPaths[i]);
            if (!chipConfigFile.is_open()) {
                logger->error("Failed to open chip config file for writing: {}", chipConfigPaths[i]);
                continue;
            }
            chipConfigFile << std::setw(4) << chipJson;
        }

        logger->info("Writing updated delay settings to controller config {}", hw_controller_filename);
        std::ofstream ctrlFile(hw_controller_filename);
        if (!ctrlFile.is_open()) {
            logger->error("Failed to open controller config file for writing: {}", hw_controller_filename);
        } else {
            ctrlFile << std::setw(4) << jcontroller;
        }
    }

    std::string strippedScan = ScanHelper::createOutputDir("cmlBiasScan", runCounter, output_dir);
    ScanHelper::createSymlink(dataDir, strippedScan, runCounter);

    for (auto &fe : fes) {
        auto cfg = dynamic_cast<FrontEndCfg*>(fe.get());
        uint32_t lane = cfg->getRxChannel();
        std::string prefix = "lane" + std::to_string(lane) + "_" + cfg->getName();
        laneHistos[lane]->toFile(prefix, output_dir);
        if (doPlots) {
            laneHistos[lane]->plot(prefix, output_dir);
        }
    }

    logger->info("Done scanning! Results written to {}", output_dir);

    return 0;
}
