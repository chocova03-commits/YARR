// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: Analysis Base class
// ################################

#include "StdAnalysis.h"

// NB if we don't include this, it compiles, but we get a linker error,
// presumably because it picks up names from C rather than C++
#include <cmath>

#include "AllAnalyses.h"
#include "FrontEndCfg.h"
#include "Histograms.h"
#include "StdHistogrammer.h"
#include "StdTriggerAction.h"
#include "StdParameterAction.h"

#include "lmcurve.h"
#include "scurvegauss.h"
#include "logging.h"
#include "spdlog/fmt/bundled/color.h"

namespace {
    auto alog = logging::make_log("StdAnalysis");
}

namespace {

    bool oa_registered =
        StdDict::registerAnalysis("OccupancyAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new OccupancyAnalysis());});

    bool l1_registered =
        StdDict::registerAnalysis("L1Analysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new L1Analysis());});

    bool tag_registered =
        StdDict::registerAnalysis("TagAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new TagAnalysis());});

    bool tot_registered =
        StdDict::registerAnalysis("TotAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new TotAnalysis());});

    bool no_registered =
        StdDict::registerAnalysis("NoiseAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new NoiseAnalysis());});

    bool no_tune_registered =
        StdDict::registerAnalysis("NoiseTuning",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new NoiseTuning());});

    bool sf_registered =
        StdDict::registerAnalysis("ScurveFitter",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new ScurveFitter());});

    bool gbl_thr_registered =
        StdDict::registerAnalysis("OccGlobalThresholdTune",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new OccGlobalThresholdTune());});

    bool pix_thr_registered =
        StdDict::registerAnalysis("OccPixelThresholdTune",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new OccPixelThresholdTune());});

    bool del_registered =
        StdDict::registerAnalysis("DelayAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new DelayAnalysis());});

    bool inj_del_registered =
        StdDict::registerAnalysis("InjDelayTune",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new InjDelayTune());});

    bool np_registered =
        StdDict::registerAnalysis("NPointGain",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new NPointGain());});

    bool param_registered =
        StdDict::registerAnalysis("ParameterAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new ParameterAnalysis());});

    bool archiver_registered =
        StdDict::registerAnalysis("HistogramArchiver",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new HistogramArchiver());});

    bool throt_registered = 
        StdDict::registerAnalysis("TriggerThrottleAnalysis",
                []() { return std::unique_ptr<AnalysisAlgorithm>(new TriggerThrottleAnalysis());});

} // End of anonymous namespace

void HistogramArchiver::init(const ScanLoopInfo *s) {
}

void HistogramArchiver::processHistogram(HistogramBase *histo) {
    std::string name = feCfg->getName();

    histo->toFile(name, output_dir);
}

void HistogramArchiver::loadConfig(const json &j){
}

void HistogramArchiver::setOutputDirectory(std::string dir) {
    output_dir = std::move(dir);
}

void OccupancyAnalysis::init(const ScanLoopInfo *s) {
    n_count = 1;
    injections = 0;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isMaskLoop() || l->isTriggerLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }
        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("OccupancyAnalysis: loop declared as trigger loop, does not have a trigger count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
    }
    if(LowThr == 0.0 && HighThr == 0.0) {
      LowThr = injections;
      HighThr = injections;
    }
}



void OccupancyAnalysis::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot names
    std::string name = "OccupancyMap";
    std::string name2 = "EnMask";

    // Check if Histogram exists
    // if (occMaps[uid] == nullptr) {
    //     std::unique_ptr<Histo2d> hh(new Histo2d("OccupancyMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     occMaps[uid] = std::move(hh);

    if (occMaps[uid] == nullptr) {
        std::unique_ptr<Histo2d> hh{createHistoMap(name, "Hits", nCol, nRow, loopStatus)};
        occMaps[uid] = std::move(hh);
    }

    // Add up Histograms
    occMaps[uid]->add(*(Histo2d*)h);
    innerCnt[uid]++;
    // Got all data, finish up Analysis
    // if (innerCnt[uid] == n_count) {
    //     std::unique_ptr<Histo2d> mask(new Histo2d("EnMask", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     mask->setXaxisTitle("Column");
    //     mask->setYaxisTitle("Rows");
    //     mask->setZaxisTitle("Enable");

    if (innerCnt[uid] == n_count) {
        std::unique_ptr<Histo2d> mask{createHistoMap(name2, "Enable", nCol, nRow, loopStatus)};

        unsigned failed_cnt = 0;
        for(unsigned col=1; col<=nCol; col++) {
            for (unsigned row=1; row<=nRow; row++) {
                unsigned i = occMaps[uid]->binNum(col, row);
		if (occMaps[uid]->getBin(i) >= LowThr && occMaps[uid]->getBin(i) <= HighThr) {
                    mask->setBin(i, 1);
                } else {
                    failed_cnt++;
                    if (make_mask&&createMask) {
                        // maskPixel starts at 0,0
                        feCfg->maskPixel(col-1, row-1);
                    }
                }
            }
        }

        // Core Column Mask:
        const unsigned nColsInCCol = 8;
        const unsigned totalPixelsPerCCol = nColsInCCol * fe_geom.nRow;
        if (coreColMask){
            feCfg->enableAll();

            struct CColStat { unsigned coreCol; unsigned nBad; bool masked; };
            std::vector<CColStat> ccolStats;

            unsigned nRow = fe_geom.nRow;
            for (unsigned coreCol = 0; coreCol < 50; coreCol++){
                unsigned nBad = 0;
                for (unsigned col = 1; col <= nColsInCCol; col++){
                    for (unsigned row = 1; row <= nRow; row++){
                        unsigned i = occMaps[uid]->binNum(coreCol*nColsInCCol+col, row);
                        float occ = occMaps[uid]->getBin(i);
                        if (occ < LowThr || occ > HighThr)
                            nBad++;
                    }
                }

                alog->debug("In core column {} there are {} bad pixels", coreCol+1, nBad);
                const bool masked = (nBad > coreColMaskThr * totalPixelsPerCCol);
                if (masked){
                    for (unsigned iPixel = 0; iPixel < nRow * nColsInCCol; iPixel++){
                        unsigned col = coreCol * nColsInCCol + iPixel % 8;
                        unsigned row = iPixel / 8;
                        feCfg->maskPixel(col, row);
                    }
                    alog->warn("[{}][{}] Turned core Column {} off in config, because it had {} bad pixels",
                               id, feCfg->getName(), coreCol+1, nBad);
                }
                if (nBad > 0)
                    ccolStats.push_back({coreCol, nBad, masked});
            }

            if (!ccolStats.empty()) {
                alog->info("[{}][{}] Core column bad pixel summary (threshold {:.0f}%):",
                           id, feCfg->getName(), coreColMaskThr * 100.0);
                for (const auto& s : ccolStats) {
                    const double pct = 100.0 * s.nBad / totalPixelsPerCCol;
                    if (s.masked) {
                        alog->info("[{}][{}]   CoreCol {:2d}: {:4d}/{} bad ({:5.1f}%) -- MASKED",
                                   id, feCfg->getName(), s.coreCol+1, s.nBad, totalPixelsPerCCol,
                                   pct);
                    } else {
                        const unsigned regIdx = s.coreCol / 16;
                        const unsigned bitVal = 1u << (s.coreCol % 16);
                        alog->info("[{}][{}]   CoreCol {:2d}: {:4d}/{} bad ({:5.1f}%){}{}",
                                   id, feCfg->getName(), s.coreCol+1, s.nBad, totalPixelsPerCCol,
                                   pct, (pct >= 50.0 * coreColMaskThr) ? " -- near threshold" : "",
                                   (pct >= 50.0 * coreColMaskThr) ? " [config hint: subtract " + std::to_string(bitVal) 
                                   + " from EnCoreCol" + std::to_string(regIdx) + "]" : "");
                    }
                }
            }
        }

        alog->info(fmt::format(fmt::fg(fmt::terminal_color::red) | fmt::emphasis::bold,
            "[{}][{}] Total number of failing pixels: {}", id, feCfg->getName(), failed_cnt));
        output->pushData(std::move(mask)); // TODO push this mask to the specific configuration
        output->pushData(std::move(occMaps[uid]));


        //delete occMaps[ident];
        //occMaps[ident] = nullptr;
    }
}
void OccupancyAnalysis::loadConfig(const json &j){
    if (j.contains("coreColMask")){
        coreColMask=j["coreColMask"];
    }
    if (j.contains("createMask")){
        createMask=j["createMask"];
    }
    if (j.contains("LowThr")){
        LowThr=j["LowThr"];
    }
    if (j.contains("HighThr")){
        HighThr=j["HighThr"];
    }
    if (j.contains("coreColMaskThr")){
        coreColMaskThr=j["coreColMaskThr"];
    }
}

void TotAnalysis::loadConfig(const json &config) {

    // check for valid ToT histogram bin configuration
    if (config.contains("tot_bins")) {
        auto &j_bins = config["tot_bins"];
        if(j_bins.contains("n_bins") && j_bins.contains("x_lo") && j_bins.contains("x_hi")) {
            tot_bins_n = static_cast<unsigned>(j_bins["n_bins"]);
            tot_bins_x_lo = static_cast<float>(j_bins["x_lo"]);
            tot_bins_x_hi = static_cast<float>(j_bins["x_hi"]);
        } // has all required bin specifications
    }

    // ToT unit
    if (config.contains("tot_unit")) {
        tot_unit = static_cast<std::string>(config["tot_unit"]);
    }

    // check for valid ToT sigma histogram bin configuration
    if (config.contains("tot_sigma_bins")) {
        auto &j_bins = config["tot_sigma_bins"];
        if(j_bins.contains("n_bins") && j_bins.contains("x_lo") && j_bins.contains("x_hi")) {
            tot_sigma_bins_n = static_cast<unsigned>(j_bins["n_bins"]);
            tot_sigma_bins_x_lo = static_cast<float>(j_bins["x_lo"]);
            tot_sigma_bins_x_hi = static_cast<float>(j_bins["x_hi"]);
        } // has all required bin specification
    }
}

void TotAnalysis::init(const ScanLoopInfo *s) {
    useScap = true;
    useLcap = true;
    n_count = 1;
    injections = 1;
    pixelFb = nullptr;
    globalFb = nullptr;
    hasVcalLoop = false;
    vcalLoopIndex = 0;

    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("TotAnalysis: loop declared as trigger loop, does not have a trigger count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }

        if (l->isGlobalFeedbackLoop()) {
            alog->debug("Found global feedback loop");
            globalFb = std::make_unique<GlobalFeedbackSender>(feedback);
            alog->debug("Connect global feedback");
        }

        if (l->isPixelFeedbackLoop()) {
            alog->debug("Found pixel feedback loop");
            pixelFb = std::make_unique<PixelFeedbackSender>(feedback);
        }

        // Vcal Loop
        if (l->isParameterLoop()) {
            vcalMax = l->getMax();
            vcalMin = l->getMin();
            vcalStep = l->getStep();
            vcalBins = (vcalMax-vcalMin)/vcalStep;
            hasVcalLoop = true;
            vcalLoopIndex = n;
        }
    }
}


void TotAnalysis::processHistogram(HistogramBase *h) {

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot names
    std::string name = "OccMap";
    std::string name2 = "TotMap";
    std::string name3 = "Tot2Map";


    // Check if Histogram exists
    // if (occMaps[uid] == nullptr) {
    //     Histo2d *hh = new Histo2d("OccMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     occMaps[uid].reset(hh);
    //     occInnerCnt[uid] = 0;
    //     hh = new Histo2d("TotMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("{/Symbol S}(ToT)");
    //     totMaps[uid].reset(hh);
    //     totInnerCnt[uid] = 0;
    //     hh = new Histo2d("Tot2Map", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("{/Symbol S}(ToT^2)");
    //     tot2Maps[uid].reset(hh);
    //     tot2InnerCnt[uid] = 0;

    if (occMaps[uid] == nullptr) {
        occMaps[uid] = createHistoMap(name, "Hits", nCol, nRow, loopStatus);
        occInnerCnt[uid] = 0;

        totMaps[uid] = createHistoMap(name2, "{/Symbol S}(ToT)", nCol, nRow, loopStatus);
        totInnerCnt[uid] = 0;

        tot2Maps[uid] = createHistoMap(name2, "{/Symbol S}(ToT^2)", nCol, nRow, loopStatus);
        tot2InnerCnt[uid] = 0;
    }

    if (chargeVsTotMap == nullptr && hasVcalLoop) {
        double chargeMin = feCfg->toCharge(vcalMin, useScap, useLcap);
        double chargeMax = feCfg->toCharge(vcalMax, useScap, useLcap);
        double chargeStep = feCfg->toCharge(vcalStep, useScap, useLcap);

        auto hh = createHisto2d("ChargeVsTotMap",
                                "Injected Charge [e]", vcalBins+1, chargeMin-chargeStep/2, chargeMax+chargeStep/2,
                                "ToT", tot_bins_n * 10, tot_bins_x_lo + 0.5, tot_bins_x_hi + 0.5,
                                "Pixels");
        chargeVsTotMap = std::move(hh);
    }

    if (pixelTotMap == nullptr && hasVcalLoop) {
        double chargeMinp = feCfg->toCharge(vcalMin, useScap, useLcap);
        double chargeMaxp = feCfg->toCharge(vcalMax, useScap, useLcap);
        double chargeStepp = feCfg->toCharge(vcalStep, useScap, useLcap);

        auto pp2 = createHisto2d("PixelTotMap",
                                 "Pixels", nCol*nRow, 0, nCol*nRow,
                                 "Injected Charge [e]", vcalBins+1, chargeMinp-chargeStepp/2, chargeMaxp+chargeStepp/2,
                                 "avg ToT");
        pixelTotMap = std::move(pp2);
    }

    // Gather Histogram
    if (h->getName() == OccupancyMap::outputName()) {
        occMaps[uid]->add(*(Histo2d*)h);
        occInnerCnt[uid]++;
    } else if (h->getName() == TotMap::outputName()) {
        totMaps[uid]->add(*(Histo2d*)h);
        totInnerCnt[uid]++;
    } else if (h->getName() == Tot2Map::outputName()) {
        tot2Maps[uid]->add(*(Histo2d*)h);
        tot2InnerCnt[uid]++;
    } else {
        return;
    }

    // Got all data, finish up Analysis
    // if (occInnerCnt[uid] == n_count &&
    //         totInnerCnt[uid] == n_count &&
    //         tot2InnerCnt[uid] == n_count) {
    //     std::unique_ptr<Histo2d> meanTotMap(new Histo2d("MeanTotMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     meanTotMap->setXaxisTitle("Column");
    //     meanTotMap->setYaxisTitle("Row");
    //     meanTotMap->setZaxisTitle("Mean ToT ["+tot_unit+"]");
    //     std::unique_ptr<Histo2d> sumTotMap(new Histo2d("SumTotMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     sumTotMap->setXaxisTitle("Column");
    //     sumTotMap->setYaxisTitle("Row");
    //     sumTotMap->setZaxisTitle("Mean ToT ["+tot_unit+"]");
    //     std::unique_ptr<Histo2d> sumTot2Map(new Histo2d("MeanTot2Map", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     sumTot2Map->setXaxisTitle("Column");
    //     sumTot2Map->setYaxisTitle("Row");
    //     sumTot2Map->setZaxisTitle("Mean ToT^2 ["+tot_unit+"^2]");
    //     std::unique_ptr<Histo2d> sigmaTotMap(new Histo2d("SigmaTotMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus));
    //     sigmaTotMap->setXaxisTitle("Column");
    //     sigmaTotMap->setYaxisTitle("Row");
    //     sigmaTotMap->setZaxisTitle("Sigma ToT ["+tot_unit+"]");
    //     std::unique_ptr<Histo1d> meanTotDist(new Histo1d("MeanTotDist", tot_bins_n, tot_bins_x_lo + 0.5, tot_bins_x_hi + 0.5, loopStatus));
    //     meanTotDist->setXaxisTitle("Mean ToT ["+tot_unit+"]");
    //     meanTotDist->setYaxisTitle("Number of Pixels");
    //     std::unique_ptr<Histo1d> sigmaTotDist(new Histo1d("SigmaTotDist", tot_sigma_bins_n, tot_sigma_bins_x_lo, tot_sigma_bins_x_hi, loopStatus));
    //     sigmaTotDist->setXaxisTitle("Sigma ToT ["+tot_unit+"]");
    //     sigmaTotDist->setYaxisTitle("Number of Pixels");
    //     std::unique_ptr<Histo1d> tempMeanTotDist(new Histo1d("MeanTotDistFine", tot_bins_n*10, tot_bins_x_lo + 0.05, tot_bins_x_hi + 0.05, loopStatus));

    if (occInnerCnt[uid] == n_count &&
            totInnerCnt[uid] == n_count &&
            tot2InnerCnt[uid] == n_count) {
        std::unique_ptr<Histo2d> meanTotMap = createHistoMap("MeanTotMap", "Mean ToT ["+tot_unit+"]", nCol, nRow, loopStatus);
        std::unique_ptr<Histo2d> sumTotMap = createHistoMap("SumTotMap", "Mean ToT ["+tot_unit+"]", nCol, nRow, loopStatus);
        std::unique_ptr<Histo2d> sumTot2Map = createHistoMap("MeanTot2Map", "Mean ToT^2 ["+tot_unit+"^2]", nCol, nRow, loopStatus);
        std::unique_ptr<Histo2d> sigmaTotMap = createHistoMap("SigmaTotMap", "Sigma ToT ["+tot_unit+"]", nCol, nRow, loopStatus);

        auto meanTotDist = createHisto1d("MeanTotDist",
                        "Mean ToT ["+tot_unit+"]", tot_bins_n, tot_bins_x_lo + 0.5, tot_bins_x_hi + 0.5,
                        "Number of Pixels", loopStatus);
        auto sigmaTotDist = createHisto1d("SigmaTotDist",
                        "Sigma ToT ["+tot_unit+"]", tot_sigma_bins_n, tot_sigma_bins_x_lo, tot_sigma_bins_x_hi,
                        "Number of Pixels", loopStatus);
        auto tempMeanTotDist = createHisto1d("MeanTotDistFine",
                         "x", tot_bins_n*10, tot_bins_x_lo + 0.05, tot_bins_x_hi + 0.05, "y", loopStatus);

        meanTotMap->add(*totMaps[uid]);
        meanTotMap->divide(*occMaps[uid]);
        sumTotMap->add(*totMaps[uid]);
        sumTot2Map->add(*tot2Maps[uid]);
        for(unsigned i=0; i<meanTotMap->size(); i++) {
            double sigma = sqrt(fabs((sumTot2Map->getBin(i) - ((sumTotMap->getBin(i)*sumTotMap->getBin(i))/injections))/(injections-1)));
            sigmaTotMap->setBin(i, sigma);
            meanTotDist->fill(meanTotMap->getBin(i));
            tempMeanTotDist->fill(meanTotMap->getBin(i));
            sigmaTotDist->fill(sigma);
        }
        if (hasVcalLoop) {
            unsigned vcal = loopStatus.get(vcalLoopIndex);
            double currentCharge = feCfg->toCharge(vcal, useScap, useLcap); // This needs to be checked
            for (unsigned i=0; i<tempMeanTotDist->size(); i++) {
                chargeVsTotMap->fill(currentCharge, (i+1)*0.1, tempMeanTotDist->getBin(i));
            }
            for (unsigned n=0; n<meanTotMap->size(); n++) {
                pixelTotMap->fill(n, currentCharge, meanTotMap->getBin(n));
            }
        }

        alog->info(fmt::format(fmt::fg(fmt::terminal_color::yellow) | fmt::emphasis::bold,
            "[{}][{}][{}] ToT Mean = {} +- {}", id, feCfg->getName(), loopStatus.toString(),  meanTotDist->getMean(), meanTotDist->getStdDev()));

        if (globalFb != nullptr) {
            double mean = 0;
            double entries = 0;
            for (unsigned i=0; i<meanTotMap->size(); i++) {
                if (occMaps[uid]->getBin(i) == injections) {
                    mean += meanTotMap->getBin(i);
                    entries++;
                }
            }
            if (entries > 0) {
                mean = mean/entries;
            }
            alog->info("Mean is: {}", mean);

            // Pull target selected via setParams
            double targetTot = target_tot;
            int sign = 0;
            bool last = false;
            if (mean < (targetTot-0.1)) {
                sign = -1;
            } else if (mean > (targetTot+0.1)) {
                sign = +1;
            } else {
                sign = 0;
                last = true;
            }
            globalFb->feedbackBinary(id, sign, last);
        }

        if (pixelFb != nullptr) {
            double targetTot = target_tot;
            auto fbHisto = createHistoMap("feedback", "z", nCol, nRow);
            for (unsigned i=0; i<meanTotMap->size(); i++) {
                int sign = 0;
                double mean = meanTotMap->getBin(i);
                if (mean < (targetTot-0.05)) {
                    sign = -1;
                } else if (mean > (targetTot+0.05)) {
                    sign = +1;
                } else {
                    sign = 0;
                }
                fbHisto->setBin(i, sign);
            }

            pixelFb->feedback(id, std::move(fbHisto));
        }

        output->pushData(std::move(meanTotMap));
        output->pushData(std::move(sigmaTotMap));
        output->pushData(std::move(meanTotDist));
        output->pushData(std::move(sigmaTotDist));
        occInnerCnt[uid] = 0;
        totInnerCnt[uid] = 0;
        tot2InnerCnt[uid] = 0;
    }
}

void TotAnalysis::end() {
    if (hasVcalLoop) {
        //replace these (the following?) with more dynamic conversions
        double injQMin = feCfg->toCharge(vcalMin, useScap, useLcap);
        double injQMax = feCfg->toCharge(vcalMax, useScap, useLcap);
        double injQStep = feCfg->toCharge(vcalStep, useScap, useLcap);
        auto avgTotVsCharge = createHisto1d("avgTotVsCharge",
                                            "Injected Charge [e]", vcalBins+1, injQMin-injQStep/2.0, injQMax+injQStep/2.0,
                                            "avg ToT");

        for (unsigned k=0; k<avgTotVsCharge->size(); k++) {
            double injQ = feCfg->toCharge(vcalMin+k*vcalStep, useScap, useLcap);
            double sum = 0;
            double entries = 0;
            for (unsigned iToT=0; iToT<=160; iToT++) {
                double measToT = iToT * 0.1;
                int n = chargeVsTotMap->binNum(injQ, measToT);
                sum += (chargeVsTotMap->getBin(n))*(measToT);
                entries += chargeVsTotMap->getBin(n);
            }
            double averageToT = sum/entries;
            avgTotVsCharge->fill(injQ, averageToT);
        }

        //extracting ToT-to-charge data now
        auto measQtemp = createHisto3d<uint16_t>("measQtemp", "x", nRow*nCol, 0, nRow*nCol, "y", 15, 0.5, 15.5, "z", vcalBins+1,  injQMin-injQStep/2.0, injQMax+injQStep/2.0);

        int Nmessage = 0;  //boolean to be used for a message to user; in presence of middle holes, user may wish to use finer injQ steps.

        auto measQOut = createHisto2d("measQOut", "x", nRow*nCol, 0, nRow*nCol, "y", 15, 0.5, 15.5, "z");
        auto measQRMSOut = createHisto2d("measQRMSOut", "x", nRow*nCol, 0, nRow*nCol, "y", 15, 0.5, 15.5, "z");
        for (unsigned n=0; n<nCol*nRow; n++) {
            if (feCfg->getPixelEn((n/nRow), (n%nRow)) == 0) { //if pixel isn't masked
                // int anyzero = 0;
                for (unsigned k=0; k<avgTotVsCharge->size(); k++) {
                    double q = feCfg->toCharge(vcalMin+k*vcalStep, useScap, useLcap);
                    double avgTot = pixelTotMap->getBin(pixelTotMap->binNum(n, q));
                    double frac = fmod(avgTot,1);
                    double tot = avgTot - fmod(avgTot,1);
                    measQtemp->fill(n, tot, q, 100*(1-frac));
                    if (frac != 0.0) { measQtemp->fill(n, tot+1, q, 100*(frac)); }
                }
                for (unsigned tot = 0; tot < 16; tot++) {
                    double mean = 0;
                    double count = 0;
                    double meanSq = 0;
                    for(unsigned k=0; k<avgTotVsCharge->size(); k++) {
                        double q = feCfg->toCharge(vcalMin+k*vcalStep, useScap, useLcap);
                        int binNum = measQtemp->binNum(n, tot, q);
                        mean += measQtemp->getBin(binNum)*q;
                        meanSq += measQtemp->getBin(binNum)*q*q;
                        count += measQtemp->getBin(binNum);
                    }
                    if (count!=0) {
                        mean = mean/count;
                        meanSq = meanSq/count;
                        meanSq = std::sqrt(std::fabs(mean*mean - meanSq));
                    }
                    measQOut->fill(n, tot, mean);
                    measQRMSOut->fill(n,tot, meanSq);
                }

                bool message = false;  //boolean to be used for a message to user; in presence of middle holes, user may wish to use finer injQ steps.
                for (unsigned tot = 1; tot < 16; tot++) {  //Extrapolation starts here
                    int binNum = measQOut->binNum(n, tot);
                    double measQ = measQOut->getBin(binNum);
                    unsigned tmaxIndex=tot;
                    unsigned tminIndex=tot;
                    double tmaxMeasQ = 0.0;
                    double tminMeasQ = 0.0;
                    if (measQ == 0.0) {
                        while (tmaxMeasQ == 0.0 && tmaxIndex < 15) {
                            tmaxIndex += 1;
                            int tempBinNum = measQOut->binNum(n, tmaxIndex);
                            tmaxMeasQ = measQOut->getBin(tempBinNum);
                        }
                        while (tminMeasQ == 0.0 && tminIndex > 1) {
                            tminIndex -= 1;
                            int tempBinNum = measQOut->binNum(n, tminIndex);
                            tminMeasQ = measQOut->getBin(tempBinNum);
                        }
                        if (tmaxIndex == 15 && tmaxMeasQ == 0.0) {
                            measQOut->fill(n, tot, tminMeasQ);
                            measQRMSOut->fill(n, tot, -1);
                        } else {
                            double stepMeasQ = (tmaxMeasQ - tminMeasQ)/(tmaxIndex - tminIndex);
                            measQOut->fill(n, tot, tminMeasQ + stepMeasQ);
                            if (!message  && tminIndex != 1) {message = true;} //if there are middle holes, add message to log for users.
                        }
                    }
                } //end of extrapolation code
                if (message) {Nmessage += 1;}
            } //end of that specific pixel loop
        } //end of VCal = true test.
        if (Nmessage != 0) {
            alog->info("Used linear extrapolation to fill missing measured charge values for middle ToT values.");
            alog->info("User may wish to use finer injection charge steps");
            alog->info("{} pixels needed extrapolation for middle ToT cases (not edge cases)", Nmessage);
        }

        output->pushData(std::move(chargeVsTotMap));
        output->pushData(std::move(pixelTotMap));
        output->pushData(std::move(avgTotVsCharge));
        output->pushData(std::move(measQOut));
        output->pushData(std::move(measQRMSOut));
    }
}

void ScurveFitter::init(const ScanLoopInfo *s) {
    fb = nullptr;
    n_count = 1;
    vcalLoop = 0;
    injections = 50;
    useScap = true;
    useLcap = true;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop() || isPOILoop(l))) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (l->isParameterLoop()) {
                cnt++; // Parameter loop interval is inclusive
            }
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }
        // Vcal Loop
        if (isPOILoop(l)) {
            vcalLoop = n;
            vcalMax = l->getMax();
            vcalMin = l->getMin();
            vcalStep = l->getStep();
            vcalBins = (vcalMax-vcalMin)/vcalStep;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("ScurveFitter: loop declared as trigger loop, does not have a trigger count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }

        // find potential pixel feedback
        if (l->isPixelFeedbackLoop()) {
            pixelFeedbackLoop = n;
            fb = std::make_unique<PixelFeedbackSender>(feedback);
            if(fb == nullptr) {
                alog->error("ScurveFitter: loop declared as pixel feedback, does not implement feedback");
            }
        }
        prevUid[n] = l->getMin(); // Is it ok to initialize to the min? 
    }

    for (unsigned i=vcalMin; i<=vcalMax; i+=vcalStep) {
        x.push_back(i);
    }
    // Match the last value StdParameterLoop actually injects, which can be
    // < vcalMax when the range isn't an exact multiple of vcalStep.
    vcalMax = x.back();
    vcalBins = x.size() - 1;
    cnt = 0;
    n_failedfit =0;
}

void ScurveFitter::loadConfig(const json &j) {
    if (j.contains("reverse")) {
        reverse = j["reverse"];
    }
    if (j.contains("scurvegauss")) {
        use_scurvegauss = j["scurvegauss"];
    }
    if (j.contains("dumpDebugScurvePlots")) {
        m_dumpDebugScurvePlots = j["dumpDebugScurvePlots"];
    }
    if (j.contains("parametersOfInterest")) {
        for (unsigned i=0; i<j["parametersOfInterest"].size(); i++) {
            m_parametersOfInterest.push_back(j["parametersOfInterest"][i]);
        }
    }
    if (j.contains("fitParameterCuts")) {
        if (j["fitParameterCuts"].contains("chi2Min")) {
            chi2Min = j["fitParameterCuts"]["chi2Min"];
        }
        if (j["fitParameterCuts"].contains("chi2Max")) {
            chi2Max = j["fitParameterCuts"]["chi2Max"];
        }
        if (j["fitParameterCuts"].contains("maxBaselineDifference")) {
            maxBaselineDifference = j["fitParameterCuts"]["maxBaselineDifference"];
        }
    }
}

// Errorfunction
// par[0] = Mean
// par[1] = Sigma
// par[2] = Normlization
// par[3] = Offset
#define SQRT2 1.414213562
double scurveFct(double x, const double *par) {
    return par[3] + 0.5*( 2-erfc( (x-par[0])/(par[1]*SQRT2) ) )*par[2];
}

double reverseScurveFct(double x, const double *par) {
    return par[3] + 0.5*( erfc( (x-par[0])/(par[1]*SQRT2) ) )*par[2];
}

bool ScurveFitter::fitSuccess(const double (&fit_params)[n_fit_params], const double &chi2) const {
    return (vcalMin < fit_params[0]) && (fit_params[0] < vcalMax) && \
           // mean should be within vcal bounds
           (0 <= fit_params[1]) && (fit_params[1] < (vcalMax-vcalMin)) && \
           // width should be between 0 and the vcal range
           (reverse || fit_params[1] > 0) && \
           // reverse s-curve fit can touch 0, but not the regular s-curve fit
           (chi2Min < chi2) && (chi2 < chi2Max) && \
           // chi2 should be within specified range
           (fabs((fit_params[2] - fit_params[3])/injections - 1) < maxBaselineDifference);
           // difference between 100% baseline and 0% baseline compared to number of injections
           // should be within specified range
}

// void ScurveFitter::createFitResultHistograms(LoopStatus::UID uid, const LoopStatus& loopStatus) {
//     auto hh2 = std::make_unique<Histo2d>("ThresholdMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
//     hh2->setXaxisTitle("Column");
//     hh2->setYaxisTitle("Row");
//     hh2->setZaxisTitle("Threshold [e]");
//     thrMap[uid] = std::move(hh2);

//     hh2 = std::make_unique<Histo2d>("NoiseMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
//     hh2->setXaxisTitle("Column");
//     hh2->setYaxisTitle("Row");
//     hh2->setZaxisTitle("Noise [e]");
//     sigMap[uid] = std::move(hh2);

//     auto hh1 = std::make_unique<Histo1d>("Chi2Dist", 51, chi2Min-0.025, chi2Max+0.025, loopStatus);
//     hh1->setXaxisTitle("Fit Chi/ndf");
//     hh1->setYaxisTitle("Number of Pixels");
//     chiDist[uid] = std::move(hh1);

//     hh2 = std::make_unique<Histo2d>("Chi2Map", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
//     hh2->setXaxisTitle("Column");
//     hh2->setYaxisTitle("Row");
//     hh2->setZaxisTitle("Chi2");
//     chi2Map[uid] = std::move(hh2);

//     hh2 = std::make_unique<Histo2d>("StatusMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
//     hh2->setXaxisTitle("Column");
//     hh2->setYaxisTitle("Row");
//     hh2->setZaxisTitle("Fit Status");
//     statusMap[uid] = std::move(hh2);

//     hh1 = std::make_unique<Histo1d>("StatusDist", 11, -0.5, 10.5, loopStatus);
//     hh1->setXaxisTitle("Fit Status ");
//     hh1->setYaxisTitle("Number of Pixels");
//     statusDist[uid] = std::move(hh1);

//     hh1 = std::make_unique<Histo1d>("TimePerFitDist", 201, -1, 401, loopStatus);
//     hh1->setXaxisTitle("Fit Time [us]");
//     hh1->setYaxisTitle("Number of Pixels");
//     timeDist[uid] = std::move(hh1);

void ScurveFitter::createFitResultHistograms(LoopStatus::UID uid, const LoopStatus& loopStatus) {
    thrMap[uid] = createHistoMap("ThresholdMap", "Threshold [e]", nCol, nRow, loopStatus);
    sigMap[uid] = createHistoMap("NoiseMap", "Noise [e]", nCol, nRow, loopStatus);

    auto hh1 = createHisto1d("Chi2Dist", "Fit Chi/ndf", 51, chi2Min-0.025, chi2Max+0.025, "Number of Pixels", loopStatus);
    chiDist[uid] = std::move(hh1);

    chi2Map[uid] = createHistoMap("Chi2Map", "Chi2", nCol, nRow, loopStatus);

    statusMap[uid] = createHistoMap("StatusMap", "Fit Status", nCol, nRow, loopStatus);

    hh1 = createHisto1d("StatusDist", "Fit Status", 11, -0.5, 10.5, "Number of Pixels", loopStatus);
    statusDist[uid] = std::move(hh1);

    hh1 = createHisto1d("TimePerFitDist", "Fit Time [us]", 201, -1, 401, "Number of Pixels", loopStatus);
    timeDist[uid] = std::move(hh1);
}

void ScurveFitter::processHistogram(HistogramBase *h) {
    cnt++;
    // Check if right Histogram
    if (h->getName().find(OccupancyMap::outputName()) != 0)
        return;

    auto hh = dynamic_cast<Histo2d*>(h);
    LoopStatus loopStatus = hh->getStat();

    // Get unique ID from LoopStatus
    std::vector<size_t> enabledLoops = loops;
    enabledLoops.erase(std::remove(enabledLoops.begin(), enabledLoops.end(), vcalLoop), enabledLoops.end()); // remove POI
    loopStatus.setEnabledLoops(enabledLoops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    medCnt[uid]++;

    unsigned vcal = loopStatus.get(vcalLoop);
    for(unsigned col=1; col<=nCol; col++) {
        for (unsigned row=1; row<=nRow; row++) {
            // Use bin number as identifier
            unsigned bin = hh->binNum(col, row);
            if (hh->getBin(bin) != 0 || reverse) {
                std::string name = "Scurve";
                name += "-" + std::to_string(col) + "-" + std::to_string(row);

                // Check if Histogram exists
                // if (histos[{uid, bin}] == nullptr) {
                //     auto hhh = std::make_unique<Histo1d>(name, vcalBins+1, vcalMin-((double)vcalStep/2.0), vcalMax+((double)vcalStep/2.0), loopStatus);
                //     hhh->setXaxisTitle("Vcal");
                //     hhh->setYaxisTitle("Occupancy");
                //     histos[{uid, bin}] = std::move(hhh);

                if (histos[{uid, bin}] == nullptr) {
                    auto hhh = createHisto1d(name, "Vcal", vcalBins+1, vcalMin-((double)vcalStep/2.0), vcalMax+((double)vcalStep/2.0), "Occupancy", loopStatus);
                    histos[{uid, bin}] = std::move(hhh);
                }

                // Add up Histograms
                double thisBin = hh->getBin(bin);
                histos[{uid, bin}]->fill(vcal, thisBin);

                // Got all data, finish up Analysis
                // TODO This requires the loop to run from low to high and a hit in the last bin
                if (vcal >= vcalMax) {
                    // Scale histos
                    //histos[{uid, bin}]->scale(1.0/(double)injections);
                    lm_status_struct status;
                    lm_control_struct control;
                    control = lm_control_float;
                    //control.verbosity = 3;
                    control.verbosity = 0;
                    const unsigned n_par = 4;
                    //double par[n_par] = {((vcalMax-vcalMin)/2.0)+vcalMin,  5 , (double) injections};
                    double par[n_par] = {((vcalMax-vcalMin)/2.0)+vcalMin,  0.05*(((vcalMax-vcalMin)/2.0)+vcalMin)  , (double) injections, 0};
                    std::chrono::high_resolution_clock::time_point start;
                    std::chrono::high_resolution_clock::time_point end;
                    start = std::chrono::high_resolution_clock::now();

                    if (use_scurvegauss) {
                        // mean and sigma calculated rather than fitted, implementation libUtil/scurvegauss.cpp

                    //     scurvegauss(par, vcalBins, &x[0], histos[{uid, bin}]->getData());
                    // }
                    // else if (reverse) {
                    //     lmcurve(n_par, par, vcalBins, &x[0], histos[{uid, bin}]->getData(), reverseScurveFct, &control, &status);
                    // } else {
                    //     lmcurve(n_par, par, vcalBins, &x[0], histos[{uid, bin}]->getData(), scurveFct, &control, &status);                        
                        scurvegauss(par, x.size(), x.data(), histos[{uid, bin}]->getData());
                    }
                    else if (reverse) {
                        lmcurve(n_par, par, x.size(), x.data(), histos[{uid, bin}]->getData(), reverseScurveFct, &control, &status);
                    } else {
                        lmcurve(n_par, par, x.size(), x.data(), histos[{uid, bin}]->getData(), scurveFct, &control, &status);
                    }

                    end = std::chrono::high_resolution_clock::now();
                    std::chrono::microseconds fitTime = std::chrono::duration_cast<std::chrono::microseconds>(end-start);
                    if (thrMap[uid] == nullptr) {
                        createFitResultHistograms(uid, loopStatus);
                    }

                    double chi2= status.fnorm/(double)(x.size() - n_par);

                    //override the following chi2 check if using scurvegauss (no fit involved)
                    if (use_scurvegauss) chi2 = 1.;

                    if (fitSuccess(par, chi2)) {
                        thrMap[uid]->setBin(bin, feCfg->toCharge(par[0], useScap, useLcap));
                        // Reudce effect of vcal offset on this, don't want to probe at low vcal
                        sigMap[uid]->setBin(bin, feCfg->toCharge(par[0]+par[1], useScap, useLcap)-feCfg->toCharge(par[0], useScap, useLcap));
                        chiDist[uid]->fill(status.fnorm/(double)status.nfev);
                        timeDist[uid]->fill(fitTime.count());
                        chi2Map[uid]->setBin(bin, chi2 );
                        statusMap[uid]->setBin(bin, status.outcome);
                        statusDist[uid]->fill(status.outcome);

                    } else {
                        n_failedfit++;
                        alog->debug("[{}] [{}] Failed fit Col({}) Row({}) Threshold({}) Chi2({}) Status({}) Entries({}) Mean({})", id, feCfg->getName(), col, row, thrMap[uid]->getBin(bin), chi2, status.outcome, histos[{uid, bin}]->getEntries(), histos[{uid, bin}]->getMean());
                    }
                    if (m_dumpDebugScurvePlots && row == nRow/2 && col%10 == 0) {
                        output->pushData(std::move(histos[{uid, bin}]));
                    }
                    histos[{uid, bin}].reset(nullptr);
                }
            }
        }
    }

    // Finished full vcal loop, if feedback loop provide TDAC feedback
    // Requires odd number of TDAC steps, optimised for 3 iterations
    if (medCnt[uid] == n_count && fb != nullptr) {
        if (uid[pixelFeedbackLoop] == 0) { // First pixel feedback iteration
            thrTarget = thrMap[uid]->getMean();
        }

        // if (step[uid] == nullptr) {
        //     auto hh2 = std::make_unique<Histo2d>("StepMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
        //     hh2->setXaxisTitle("Column");
        //     hh2->setYaxisTitle("Row");
        //     hh2->setZaxisTitle("TDAC change");
        //     step[uid] = std::move(hh2);
        // }

        // if (deltaThr[uid] == nullptr) {
        //     auto hh2 = std::make_unique<Histo2d>("DeltaThreshold", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
        //     hh2->setXaxisTitle("Column");
        //     hh2->setYaxisTitle("Row");
        //     hh2->setZaxisTitle("Delta Threshold [e]");
        //     deltaThr[uid] = std::move(hh2);

        if (step[uid] == nullptr) {
            auto hh2 = createHistoMap("StepMap", "TDAC change", nCol, nRow, loopStatus);
            step[uid] = std::move(hh2);
        }

        if (deltaThr[uid] == nullptr) {
            auto hh2 = createHistoMap("DeltaThreshold", "Delta Threshold [e]", nCol, nRow, loopStatus);
            deltaThr[uid] = std::move(hh2);
        }

        for(unsigned col=1; col<=nCol; col++) {
            for (unsigned row=1; row<=nRow; row++) {
                unsigned bin = thrMap[uid]->binNum(col, row);
                step[uid]->setBin(bin, 0);
                if (thrMap[uid]->getBin(bin) != 0) {
                    double curDeltaThr = thrTarget - thrMap[uid]->getBin(bin);
                    if (uid[pixelFeedbackLoop] == 0) { // First pixel feedback iteration
                        if (curDeltaThr > 5) { // Increase Threshold
                            step[uid]->setBin(bin, -1);
                        } else if (curDeltaThr < -5) { //Decrease Threshold
                            step[uid]->setBin(bin, +1);
                        }
                    } else if (step[prevUid]->getBin(bin) != 0) { // Second or higher loop
                        if (fabs(curDeltaThr) < fabs(deltaThr[prevUid]->getBin(bin))) {
                            // Did the sign change?
                            if ( ((curDeltaThr < 0) ? -1 : (curDeltaThr > 0)) != ((deltaThr[prevUid]->getBin(bin) < 0) ? -1 : (deltaThr[prevUid]->getBin(bin) > 0)) ) {
                                step[uid]->setBin(bin, 0); // Best setting
                            } else {
                                step[uid]->setBin(bin, step[prevUid]->getBin(bin)); // Move in the same direction
                            }
                        } else {
                            // Last setting was better
                            step[uid]->setBin(bin, step[prevUid]->getBin(bin)*-1);
                        }
                    }
                }
                deltaThr[uid]->setBin(bin, thrTarget - thrMap[uid]->getBin(bin));
            }
        }
        prevUid = uid;
        alog->info("[{}] --> Sending feedback #{}", this->id, loopStatus.toString());
        fb->feedback(this->id, std::make_unique<Histo2d>(*(step[uid].get())));
    }
}

void ScurveFitter::end() {

    if (fb != nullptr) {
        alog->info("[{}] Tuned to ==> {}", this->id, thrTarget);
    }

    for (auto &[uid, thrMapPtr] : thrMap) {
        if (thrMapPtr) {

            LoopStatus loopStatus = thrMapPtr->getStat();

            int bin_width, xlow, xhigh, bins;
            double thrMean = thrMap[uid]->getMean();
            double thrRms = thrMap[uid]->getStdDev();
            double sigMean = sigMap[uid]->getMean();
            double sigRms = sigMap[uid]->getStdDev();


            bin_width = 10;
            int rThrMean = (int)(thrMean) - (int)(thrMean)%bin_width;
            int rThrRms = (int)(thrRms) - (int)(thrRms)%bin_width;
            xlow = rThrMean-(rThrRms*5)-bin_width/2.0;
            if (xlow < 0)
                xlow = -1*bin_width/2.0;
            xhigh = rThrMean+(rThrRms*5)+bin_width/2.0;
            if ((xhigh-xlow)%bin_width != 0)
                xhigh += ((xhigh-xlow)%bin_width);
            if (xlow > xhigh) {// Something wrong, prevent
                xhigh = xlow+bin_width;
                alog->warn("[{}] --> xlow > xhigh, resetting boundaries, this should not happen under normal circumstances!", this->id);
            }
            bins = (xhigh-xlow)/bin_width;

            // auto hh1 = std::make_unique<Histo1d>("ThresholdDist", bins, xlow, xhigh, loopStatus);
            // hh1->setXaxisTitle("Threshold [e]");
            // hh1->setYaxisTitle("Number of Pixels");
            // thrDist[uid] = std::move(hh1);

            auto hh1 = createHisto1d("ThresholdDist", "Threshold [e]", bins, xlow, xhigh, "Number of Pixels", loopStatus);
            thrDist[uid] = std::move(hh1);

            bin_width = 5;
            int rSigMean = (int)(sigMean) - (int)(sigMean)%bin_width;
            int rSigRms = (int)(sigRms) - (int)(sigRms)%bin_width;
            xlow = rSigMean-(rSigRms*5)-bin_width/2.0;
            if (xlow < 0)
                xlow = -1*bin_width/2.0;
            xhigh = rSigMean+(rSigRms*5)+bin_width/2.0;
            if ((xhigh-xlow)%bin_width != 0)
                xhigh += ((xhigh-xlow)%bin_width);
            if (xlow > xhigh) {// Something wrong, prevent
                xhigh = xlow+bin_width;
                alog->warn("[{}] --> xlow > xhigh, resetting boundaries, this should not happen under normal circumstances!", this->id);
            }
            bins = (xhigh-xlow)/bin_width;

            // hh1 = std::make_unique<Histo1d>("NoiseDist", bins, xlow, xhigh, loopStatus);
            // hh1->setXaxisTitle("Noise [e]");
            // hh1->setYaxisTitle("Number of Pixels");
            // sigDist[uid] = std::move(hh1);

            hh1 = createHisto1d("NoiseDist", "Noise [e]", bins, xlow, xhigh, "Number of Pixels", loopStatus);
            sigDist[uid] = std::move(hh1);

            for(unsigned bin=0; bin<(nCol*nRow); bin++) {
                if (thrMap[uid]->getBin(bin) != 0)
                    thrDist[uid]->fill(thrMap[uid]->getBin(bin));
                if (sigMap[uid]->getBin(bin) != 0)
                    sigDist[uid]->fill(sigMap[uid]->getBin(bin));
            }

            // Before moving data to clipboard
            {
              auto style = fmt::fg(fmt::terminal_color::yellow) | fmt::emphasis::bold;
              alog->info(fmt::format(style, "[{}][{}][{}] Threshold Mean = {} +- {}", id, feCfg->getName(), loopStatus.toString(), thrMap[uid]->getMean(), thrMap[uid]->getStdDev()));
              alog->info(fmt::format(style, "[{}][{}][{}] Noise Mean = {} +- {}", id, feCfg->getName(), loopStatus.toString(), sigMap[uid]->getMean(), sigMap[uid]->getStdDev()));
              alog->info(fmt::format(style, "[{}][{}][{}] Number of failed fits = {}", id, feCfg->getName(), loopStatus.toString(), n_failedfit));
            }
            output->pushData(std::move(thrDist[uid]));
            output->pushData(std::move(thrMap[uid]));
            output->pushData(std::move(sigDist[uid]));
            output->pushData(std::move(chi2Map[uid]));
            output->pushData(std::move(statusMap[uid]));
            output->pushData(std::move(statusDist[uid]));
            output->pushData(std::move(step[uid]));
            output->pushData(std::move(deltaThr[uid]));
        }

        output->pushData(std::move(sigMap[uid]));
        output->pushData(std::move(chiDist[uid]));
        output->pushData(std::move(timeDist[uid]));
    }
}

void NPointGain::init(const ScanLoopInfo *s) {
    for (unsigned n = 0; n < s->size(); n++) {
        auto l = s->getLoop(n);
        if (isPOILoop(l)) {
            m_injectionLoopIndex = n;
            break;
        }
    }
}

void NPointGain::loadConfig(const json &j) {
    if (j.contains("parametersOfInterest")) {
        for (unsigned i = 0; i < j["parametersOfInterest"].size(); i++) {
            m_parametersOfInterest.push_back(j["parametersOfInterest"][i]);
        }
    }

    if (j.contains("skipDependencyCheck"))
        m_skipDependencyCheck = j["skipDependencyCheck"];

    if (j.contains("fitFunction") && (m_respFuncMap.find(j["fitFunction"]) != m_respFuncMap.end())) {
        if (m_respFuncMap.find(j["fitFunction"]) != m_respFuncMap.end()) {
            m_respFuncName = j["fitFunction"];
        } else {
            alog->warn("NPointGain: Unknown response function '{}' provided in config.", static_cast<std::string>(j["fitFunction"]));
            m_respFuncName = "linear";
        }
    } else {
        m_respFuncName = "linear";
    }

    if (j.contains("produceValidationPlots")) {
        m_produceValidationHistos = j["produceValidationPlots"];
    }

    m_respFunc = m_respFuncMap[m_respFuncName];
    m_gainConvFunc = m_gainConvFuncMap[m_respFuncName];
    m_respFuncNParams = m_respFuncNParamsMap[m_respFuncName];
    alog->info("NPointGain: Response function set to '{}'.", m_respFuncName);
}

void NPointGain::processHistogram(HistogramBase *h) {
    std::string hname = h->getName();
    std::string prefix = hname.substr(0, hname.find("_"));

    // pick storage container based on input histogram
    InjectionDataContainer* container;
    if (prefix == "ThresholdMap") {
        container = &m_thresholdMap;
    } else if (prefix == "NoiseMap") {
        container = &m_outputNoiseMap;
    } else {
        return;
    }

    auto histo = dynamic_cast<Histo2d*>(h);
    if (histo == nullptr)
        return;

    // get the scan parameter value (injection) and initialize corresponding map element
    double inj_raw = histo->getStat().get(m_injectionLoopIndex);
    double inj = convertInjectionUnit(inj_raw);
    (*container)[inj] = std::vector<std::vector<double>>(nCol, std::vector<double>(nRow));

    // fill values for each channel
    for (unsigned col = 0; col < nCol; col++) {
        for (unsigned row = 0; row < nRow; row++) {
            int binNum = histo->binNum(col+1, row+1);
            (*container)[inj][col][row] = convertThresholdUnit(histo->getBin(binNum));
        }
    }
}

std::vector<double> NPointGain::createResponseCurve(unsigned col, unsigned row) {
    std::vector<double> thresholds;
    thresholds.reserve(m_injections.size());
    for(const auto inj : m_injections) {
        thresholds.push_back(m_thresholdMap[inj][col][row]);
    }
    return thresholds;
}

std::vector<double> NPointGain::guessInitialFitParams(const std::vector<double>& thresholds) {
    std::vector<double> fitParams;

    if (m_respFuncName == "linear" || m_respFuncName == "polynomial") {
        // easy to just get slope from data
        double slope = (thresholds[thresholds.size()-1] - thresholds[0]) \
                    / (m_injections[m_injections.size()-1] - m_injections[0]);
        fitParams.push_back(0.);
        fitParams.push_back(slope);
        if (m_respFuncName == "polynomial") {
            fitParams.push_back(0.);
        }
    } else {
        // exponential is a bit trickier, so just leave ones for now
        fitParams = std::vector<double>(m_respFuncNParamsMap[m_respFuncName], 1.);
    }

    return fitParams;
}

void NPointGain::fitResponseCurve(const std::vector<double>& thresholds, std::vector<double>& fitParams) {
    lm_status_struct status;
    lm_control_struct control = lm_control_float;
    control.verbosity = 0;

    lmcurve(
        fitParams.size(), fitParams.data(),
        m_injections.size(), m_injections.data(), thresholds.data(),
        m_respFunc, &control, &status);

    if (status.outcome > 3) {
        alog->warn("Fit failed with status {}: {}", status.outcome, lm_infmsg[status.outcome]);
    }
}

void NPointGain::writeOutputHistograms() {
    auto injectionHisto = createHisto1d("InjectionValues",
        "x", m_injections.size(), -0.5, m_injections.size()-0.5, "y");
    auto fitParamsHisto = createHistoMap3d<float>("ResponseFitParams",
                            nCol, nRow,
                            "z", m_respFuncNParams, -0.5, m_respFuncNParams-0.5);
    auto thresholdHisto = createHistoMap3d<float>("Thresholds",
                            nCol, nRow,
                            "z", m_injections.size(), -0.5, m_injections.size()-0.5);
    auto outputNoiseHisto = createHistoMap3d<float>("OutputNoise",
                            nCol, nRow,
                            "z", m_injections.size(), -0.5, m_injections.size()-0.5);
    auto gainCurveHisto = createHistoMap3d<float>("GainCurve",
                            nCol, nRow,
                            "z", m_injections.size(), -0.5, m_injections.size()-0.5);
    auto inputNoiseHisto = createHistoMap3d<float>("InputNoise",
                            nCol, nRow,
                            "z", m_injections.size(), -0.5, m_injections.size()-0.5);

    for (unsigned injIdx = 0; injIdx < m_injections.size(); injIdx++) {
        double inj = m_injections[injIdx];
        injectionHisto->fill(injIdx, inj);
    }

    for (unsigned col = 0; col < nCol; col++) {
        for (unsigned row = 0; row < nRow; row++) {
            const auto& fitParams = m_responseParamMap[col][row];
            for (unsigned parIdx = 0; parIdx < m_respFuncNParams; parIdx++) {
                fitParamsHisto->fill(col, row, parIdx, fitParams[parIdx]);
            }

            for (unsigned injIdx = 0; injIdx < m_injections.size(); injIdx++) {
                double inj = m_injections[injIdx];
                thresholdHisto->fill(col, row, injIdx, m_thresholdMap[inj][col][row]);
                outputNoiseHisto->fill(col, row, injIdx, m_outputNoiseMap[inj][col][row]);
                gainCurveHisto->fill(col, row, injIdx, m_gainMap[inj][col][row]);
                inputNoiseHisto->fill(col, row, injIdx, m_inputNoiseMap[inj][col][row]);
            }
        }
    }

    output->pushData(std::move(injectionHisto));
    output->pushData(std::move(fitParamsHisto));
    output->pushData(std::move(thresholdHisto));
    output->pushData(std::move(outputNoiseHisto));
    output->pushData(std::move(gainCurveHisto));
    output->pushData(std::move(inputNoiseHisto));
}

void NPointGain::writeValidationHistograms() {
    // determine global min/max for histogram ranges
    double thresholdMax = -1e9, thresholdMin = 1e9;
    double outputNoiseMax = -1e9, outputNoiseMin = 1e9;
    double gainMax = -1e9, gainMin = 1e9;
    double inputNoiseMax = -1e9, inputNoiseMin = 1e9;
    for (const auto& inj : m_injections) {
        for (unsigned col = 0; col < nCol; col++) {
            for (unsigned row = 0; row < nRow; row++) {
                double threshold = m_thresholdMap[inj][col][row];
                if (threshold > thresholdMax) thresholdMax = threshold;
                if (threshold < thresholdMin) thresholdMin = threshold;

                double outputNoise = m_outputNoiseMap[inj][col][row];
                if (outputNoise > outputNoiseMax) outputNoiseMax = outputNoise;
                if (outputNoise < outputNoiseMin) outputNoiseMin = outputNoise;

                double gain = m_gainMap[inj][col][row];
                if (gain > gainMax) gainMax = gain;
                if (gain < gainMin) gainMin = gain;

                double inputNoise = m_inputNoiseMap[inj][col][row];
                if (inputNoise > inputNoiseMax) inputNoiseMax = inputNoise;
                if (inputNoise < inputNoiseMin) inputNoiseMin = inputNoise;
            }
        }
    }

    for (unsigned injIdx = 0; injIdx < m_injections.size(); injIdx++) {
        auto thresholdHisto = std::make_unique<Histo1d>(
            "Thresholds-Injection-" + std::to_string(injIdx),
            40, thresholdMin, thresholdMax);
        auto outputNoiseHisto = std::make_unique<Histo1d>(
            "OutputNoise-Injection-" + std::to_string(injIdx),
            40, outputNoiseMin, outputNoiseMax);
        auto gainHisto = std::make_unique<Histo1d>(
            "Gain-Injection-" + std::to_string(injIdx),
            40, gainMin, gainMax);
        auto inputNoiseHisto = std::make_unique<Histo1d>(
            "InputNoise-Injection-" + std::to_string(injIdx),
            40, inputNoiseMin, inputNoiseMax);

        double inj = m_injections[injIdx];
        for (unsigned col = 0; col < nCol; col++) {
            for (unsigned row = 0; row < nRow; row++) {
                thresholdHisto->fill(m_thresholdMap[inj][col][row]);
                outputNoiseHisto->fill(m_outputNoiseMap[inj][col][row]);
                gainHisto->fill(m_gainMap[inj][col][row]);
                inputNoiseHisto->fill(m_inputNoiseMap[inj][col][row]);
            }
        }

        output->pushData(std::move(thresholdHisto));
        output->pushData(std::move(outputNoiseHisto));
        output->pushData(std::move(gainHisto));
        output->pushData(std::move(inputNoiseHisto));
    }
}

void NPointGain::end() {
    // get injections vector for fitting, needs to be double for lmcurve
    // it is already sorted because it came from a map!
    // also might as well initialize storage maps here
    for (const auto& pair : m_thresholdMap) {
        double inj = pair.first;
        m_injections.push_back(inj);
        m_gainMap[inj] = std::vector<std::vector<double>>(nCol, std::vector<double>(nRow));
        m_inputNoiseMap[inj] = std::vector<std::vector<double>>(nCol, std::vector<double>(nRow));
    }

    // run response curve fit for each channel and fill storage maps
    m_responseParamMap = std::vector<std::vector<std::vector<double>>>(nCol, std::vector<std::vector<double>>(nRow));
    for (unsigned col = 0; col < nCol; col++) {
        for (unsigned row = 0; row < nRow; row++) {
            auto thresholds = createResponseCurve(col, row);
            std::vector<double> fitParams = guessInitialFitParams(thresholds);
            fitResponseCurve(thresholds, fitParams);
            m_responseParamMap[col][row] = fitParams;

            for (const auto& inj : m_injections) {
                double gain = m_gainConvFunc(inj, fitParams.data());
                m_gainMap[inj][col][row] = gain;
                m_inputNoiseMap[inj][col][row] = gain > 0 ? convertInputNoiseUnit(m_outputNoiseMap[inj][col][row] / gain) : 0;
            }
        }
    }

    writeOutputHistograms();
    if (m_produceValidationHistos) {
        writeValidationHistograms();
    }

}

void OccGlobalThresholdTune::init(const ScanLoopInfo *s) {
    n_count = 1;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isDataLoop() || l->isTriggerLoop() || l->isMaskLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("OccGlobalThresholdTune: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }

        if (l->isGlobalFeedbackLoop()) {
            fb = std::make_unique<GlobalFeedbackSender>(feedback);
        }
    }
}

void OccGlobalThresholdTune::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot names
    std::string name = "OccupancyMap";
    std::string name2 = "OccupancyDist";


    // Check if Histogram exists
    // if (occMaps[uid] == nullptr) {
    //     Histo2d *hh = new Histo2d("OccupancyMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     occMaps[uid].reset(hh);
    //     //Histo1d *hhh = new Histo1d("OccupancyDist", injections+1, -0.5, injections+0.5, typeid(this));
    //     // Ignore first and last bin to dismiss masked or not functioning pixels
    //     Histo1d *hhh = new Histo1d("OccupancyDist", injections-1, 0.5, injections-0.5, loopStatus);
    //     hhh->setXaxisTitle("Occupancy");
    //     hhh->setYaxisTitle("Number of Pixels");
    //     occDists[uid].reset(hhh);
    //     innerCnt[uid] = 0;

    if (occMaps[uid] == nullptr) {
        occMaps[uid] = createHistoMap(name, "Hits", nCol, nRow, loopStatus);
        //Histo1d *hhh = new Histo1d(name2, injections+1, -0.5, injections+0.5, typeid(this));
        // Ignore first and last bin to dismiss masked or not functioning pixels
        auto hhh = createHisto1d(name2, "Occupancy", injections-1, 0.5, injections-0.5, "Number of Pixels", loopStatus);
        occDists[uid] = std::move(hhh);
        innerCnt[uid] = 0;
    }

    // Add up Histograms
    occMaps[uid]->add(*(Histo2d*)h);
    innerCnt[uid]++;

    // Got all data, finish up Analysis
    if (innerCnt[uid] == n_count) {

        for(unsigned i=0; i<occMaps[uid]->size(); i++)
            occDists[uid]->fill(occMaps[uid]->getBin(i));

        m_entries = occDists[uid]->getEntries();

        // inverts sign if previous number of entries in bathtub plot is larger than current number of entries in bathtub plot
        if (m_entries < (nCol*nRow)*0.005) { // Want at least 0.5% of all pixels to fire
            m_sign = -1;
        } else if (m_entries < m_oldEntries && !m_done) {
            m_sign *= -1;
        }

        alog->info("[{}] Total Entries = {}. Previous Total Entries = {}. Sign = {}.", id, m_entries, m_oldEntries, m_sign);

        m_oldEntries = m_entries;

        fb->feedback(this->id, m_sign, m_done);
        output->pushData(std::move(occMaps[uid]));
        output->pushData(std::move(occDists[uid]));
        innerCnt[uid] = 0;
        //delete occMaps[uid];
        occMaps[uid] = nullptr;
        //delete occDists[uid];
        occDists[uid] = nullptr;
    }

}

void OccPixelThresholdTune::loadConfig(const json &j){
    if (j.contains("occLowCut")) {
        if (!j["occLowCut"].is_array()) {
            alog->error("Could not load \"occLowCut\" from config!");
            return;
        }
        m_occLowCut.clear();
        for(auto &i: j["occLowCut"]){
            m_occLowCut.push_back(i);
        }
    }
    if (j.contains("occHighCut")) {
        if (!j["occHighCut"].is_array()) {
            alog->error("Could not load \"occHighCut\" from config!");
            return;
        }
        m_occHighCut.clear();
        for(auto &i: j["occHighCut"]){
          m_occHighCut.push_back(i);
        }
    }
}

void OccPixelThresholdTune::init(const ScanLoopInfo *s) {
    n_count = 1;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("OccPixelThresholdTune: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }

        if (l->isPixelFeedbackLoop()) {
            fb = std::make_unique<PixelFeedbackSender>(feedback);
            if(fb == nullptr) {
                alog->error("OccPixelThresholdTune: loop declared as pixel feedback does not implement feedback");
            }
        }
    }
}

void OccPixelThresholdTune::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot names
    std::string name = "OccupancyMap";
    std::string name2 = "OccupancyDist";

    // Check if Histogram exists
    // if (occMaps[uid] == nullptr) {
    //     Histo2d *hh = new Histo2d("OccupancyMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     occMaps[uid].reset(hh);

    if (occMaps[uid] == nullptr) {
        occMaps[uid]= createHistoMap(name, "Hits", nCol, nRow, loopStatus);
    }

    // Add up Histograms
    occMaps[uid]->add(*(Histo2d*)h);
    innerCnt[uid]++;

    // Got all data, finish up Analysis
    if (innerCnt[uid] == n_count) {
        double mean = 0;
        // auto fbHisto = std::make_unique<Histo2d>("feedback", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5);
        // std::unique_ptr<Histo1d> occDist(new Histo1d("OccupancyDist", injections-1, 0.5, injections-0.5, loopStatus));
        // occDist->setXaxisTitle("Occupancy");
        // occDist->setYaxisTitle("Number of Pixels");
        auto fbHisto = createHistoMap("feedback", "z", nCol, nRow, loopStatus);
        auto occDist = createHisto1d(name2, "Occupancy", injections-1, 0.5, injections-0.5, "Number of Pixels", loopStatus);
        for (unsigned i=0; i<fbHisto->size(); i++) {
            double occ = occMaps[uid]->getBin(i);
            if ((occ/(double)injections) > m_occHighCut.at(m_cutIndex)) {
                fbHisto->setBin(i, -1);
            } else if ((occ/(double)injections) < m_occLowCut.at(m_cutIndex)) {
                fbHisto->setBin(i, +1);
            } else {
                fbHisto->setBin(i, 0);
            }
            mean += occMaps[uid]->getBin(i);
            occDist->fill(occMaps[uid]->getBin(i));
        }

        alog->info("[{}] Mean Occupancy = {}", id, mean/(nCol*nRow*(double)injections));
        alog->info("[{}] RMS = {}", id, occDist->getStdDev());

        fb->feedback(this->id, std::move(fbHisto));
        output->pushData(std::move(occMaps[uid]));
        output->pushData(std::move(occDist));
        innerCnt[uid] = 0;
        if (m_cutIndex < std::min(m_occLowCut.size(), m_occHighCut.size()-1)) {
            m_cutIndex++;
        }
        //delete occMaps[uid];
        occMaps[uid] = nullptr;
    }

}

void InjDelayTune::init(const ScanLoopInfo *s) {
    n_count = 1;
    n_param_loop = 1;
    param_loop_max = 1;
    n_filled_bins = 0;
    step = 1;
    finishedFeedback = false;

    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
        if (!(l->isDataLoop() || l->isTriggerLoop() || l->isMaskLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
            if (l->isParameterLoop() || l->isGlobalFeedbackLoop()) {
                n_param_loop = n_param_loop * cnt;
                param_loop_max = l->getMax();
                step = l->getStep();
            }
        } else {
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("InjDelayTune: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
        if (l->isGlobalFeedbackLoop()) {
            fb.reset(new GlobalFeedbackSender(feedback));
        }
    }
}

void InjDelayTune::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    // Select correct output container
    unsigned long ident = 0;
    unsigned long offset = 1;

    // Determine identifier
    std::string name = "OccupancyMap";
    for (unsigned n=0; n<loops.size(); n++) {
        ident += h->getStat().get(loops[n])*offset;
        offset *= loopMax[n];
        name += "-" + std::to_string(h->getStat().get(loops[n]));
    }

    if (delayDist == nullptr) {
        Histo1d *hhh = new Histo1d("OccVsDelay", param_loop_max, 0.5, param_loop_max + 0.5);
        hhh->setXaxisTitle("InjFineDelay");
        hhh->setYaxisTitle("Occupancy");
        delayDist.reset(hhh);
    }

    // Check if Histogram exists
    if (occMaps[ident] == nullptr) {
        Histo2d *hh = new Histo2d(name, nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5);
        hh->setXaxisTitle("Column");
        hh->setYaxisTitle("Row");
        hh->setZaxisTitle("Hits");
        occMaps[ident].reset(hh);
    }

    // Add up Histograms
    occMaps[ident]->add(*(Histo2d*)h);
    innerCnt[ident]++;

    // Got all data, finish up Analysis
    if (innerCnt[ident] == n_count) {
        double sum = 0;
        for (unsigned int ibin = 0; ibin < (nCol * nRow); ++ibin) {
            sum += occMaps[ident]->getBin(ibin);
        }
        sum = sum / (nCol * nRow * injections);
        delayDist->setBin(ident*step, sum);
        output->pushData(std::move(occMaps[ident]));
        innerCnt[ident] = 0;
        occMaps[ident] = nullptr;

        if (!finishedFeedback) {
            fb->feedbackStep(this->id, step, false);
            if (n_filled_bins > n_param_loop) {
                double mean = delayDist->getMean();
                double stdv = delayDist->getStdDev();

                // We want to set injection to happen 1/5 of the way into the bc window
                // Assuming the delay curve is a uniform distribution, it should have a std of width/sqrt(12)
                // So width = sqrt(12)*stdev. 1/5 of time into the window will be (mean - 3*width/10)
                // Or mean - 3*sqrt(12)*stdev/10 ~= mean - 1.03*stdev
                // Use the ceil of this number just to be sure we are not too early
                double delay_to_set = ceil(mean - 3.0*sqrt(12)*stdv/10.0);

                output->pushData(std::move(delayDist));
                fb->feedbackFinal(this->id, delay_to_set, true);
                finishedFeedback = true;
                delayDist = nullptr;
            }
        }
        n_filled_bins++;
    }
}

void InjDelayTune::end() {
}

// TODO exclude every loop
void L1Analysis::init(const ScanLoopInfo *s) {
    n_count = 1;
    injections = 0;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("L1Analysis: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
    }
}

void L1Analysis::processHistogram(HistogramBase *h) {
    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot name
    std::string name = "L1Dist";

    // Check if Histogram exists
    // if (l1Histos[uid] == nullptr) {
    //     Histo1d *hh = new Histo1d("L1Dist", 16, -0.5, 15.5, loopStatus);
    //     hh->setXaxisTitle("L1Id");
    //     hh->setYaxisTitle("Hits");
    //     l1Histos[uid].reset(hh);
    //     innerCnt[uid] = 0;

    if (l1Histos[uid] == nullptr) {
        l1Histos[uid] = createHisto1d(name, "L1Id", 16, -0.5, 15.5, "Hits", loopStatus);
        innerCnt[uid] = 0;
    }

    // Add up Histograms
    if (h->getName() == L1Dist::outputName()) {
        l1Histos[uid]->add(*(Histo1d*)h);
        innerCnt[uid]++;
    } else {
        return;
    }

    // Got all data, finish up Analysis
    if (innerCnt[uid] == n_count) {
        output->pushData(std::move(l1Histos[uid]));
        innerCnt[uid] = 0;
    }
}

void L1Analysis::end() {
}

void TagAnalysis::init(const ScanLoopInfo *s) {
    n_count = 1;
    injections = 0;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

    }
}

void TagAnalysis::processHistogram(HistogramBase *h) {
    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot names
    std::string name = "TagDist";
    std::string name2 = "TagMap";
    std::string name3 = "OccMap";

    // Check if Histogram exists
    // if (tagHistos[uid] == nullptr) {
    //     Histo1d *h = new Histo1d("TagDist", 257, -0.5, 256.5, loopStatus);
    //     h->setXaxisTitle("Tag");
    //     h->setYaxisTitle("Hits");
    //     tagHistos[uid].reset(h);
    //     tagDistInnerCnt[uid] = 0;

    //     Histo2d *hh = new Histo2d("TagMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Tag");
    //     tagMaps[uid].reset(hh);
    //     tagMapInnerCnt[uid] = 0;

    //     hh = new Histo2d("OccMap", nCol, 0.5, nCol + 0.5, nRow, 0.5, nRow + 0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     occMaps[uid].reset(hh);
    //     occInnerCnt[uid] = 0;

    if (tagHistos[uid] == nullptr) {
        tagHistos[uid] = createHisto1d(name, "Tag", 257, -0.5, 256.5, "Hits", loopStatus);
        tagDistInnerCnt[uid] = 0;

        tagMaps[uid] = createHistoMap(name2, "Tag", nCol, nRow, loopStatus);
        tagMapInnerCnt[uid] = 0;

        occMaps[uid] = createHistoMap(name3, "Hits", nCol, nRow, loopStatus);
        occInnerCnt[uid] = 0;
    }

    // Add up Histograms
    if (h->getName() == TagDist::outputName()) {
        tagHistos[uid]->add(*(Histo1d*)h);
        tagDistInnerCnt[uid]++;
    } else if (h->getName() == TagMap::outputName()) {
        tagMaps[uid]->add(*(Histo2d*)h);
        tagMapInnerCnt[uid]++;
    } else if (h->getName() == OccupancyMap::outputName()) {
        occMaps[uid]->add(*(Histo2d*)h);
        occInnerCnt[uid]++;
    } else {
        return;
    }

    // Got all data, finish up Analysis
    // if (tagDistInnerCnt[uid] == n_count && tagMapInnerCnt[uid] == n_count && occInnerCnt[uid] == n_count) {
    //     std::unique_ptr<Histo2d> meanTagMap(new Histo2d("MeanTagMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus)); 
    //     meanTagMap->setXaxisTitle("Column");
    //     meanTagMap->setYaxisTitle("Row");
    //     meanTagMap->setZaxisTitle("Mean Tag");

    if (tagDistInnerCnt[uid] == n_count && tagMapInnerCnt[uid] == n_count && occInnerCnt[uid] == n_count) {
        auto meanTagMap = createHistoMap("MeanTagMap", "Mean Tag", nCol, nRow, loopStatus);

        meanTagMap->add(*tagMaps[uid]);
        meanTagMap->divide(*occMaps[uid]);

        output->pushData(std::move(tagHistos[uid]));
        output->pushData(std::move(meanTagMap));

        tagDistInnerCnt[uid] = 0;
        tagMapInnerCnt[uid] = 0;
        occInnerCnt[uid] = 0;
    }
}

void TagAnalysis::end() {
}

void TotDistPlotter::init(const ScanLoopInfo *s) {
    n_count = 1;
    injections = 0;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop() || l->isParameterLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }
        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("TotDistPlotter: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
    }
}

void TotDistPlotter::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != TotDist::outputName())
        return;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    //Plot name
    std::string name = "TotDist";

    // Check if Histogram exists
    // if (tot[uid] == nullptr) {
    //     Histo1d *hh = new Histo1d("TotDist", 16, 0.5, 16.5, loopStatus);
    //     hh->setXaxisTitle("ToT [bc]");
    //     hh->setYaxisTitle("Hits");
    //     tot[uid].reset(hh);
    //     innerCnt[uid] = 0;

    if (tot[uid] == nullptr) {
        tot[uid] = createHisto1d(name, "ToT [bc]", 16, 0.5, 16.5, "Hits", loopStatus);
        innerCnt[uid] = 0;
    }

    // Add up Histograms
    tot[uid]->add(*(Histo1d*)h);
    innerCnt[uid]++;

    // Got all data, finish up Analysis
    if (innerCnt[uid] == n_count) {
        output->pushData(std::move(tot[uid]));
    }
}

void NoiseAnalysis::init(const ScanLoopInfo *s) {
    // We assume the nosie scan only has one trigger and data loop
    occ = createHistoMap("Occupancy", "Hits", nCol, nRow);

    tag = createHisto1d("TagDist", "Tag", 257, -0.5, 256.5, "Hits");

    tot = createHistoMap("TotMap", "Averaged ToT", nCol, nRow);

    totDist = createHisto1d("TotDist", "ToT [bc]", 16, 0.5, 16.5, "Hits");
    n_trigger = 0;
}

void NoiseAnalysis::processHistogram(HistogramBase *h) {
    if (h->getName() == OccupancyMap::outputName()) {
        occ->add(*(Histo2d*)h);
    }
    else if (h->getName() == TotMap::outputName()) {
        tot->add(*(Histo2d*)h);
    }
    else if (h->getName() == TagDist::outputName()) {
        tag->add(*(Histo1d*)h);
    }
    else if (h->getName() == HitsPerEvent::outputName()) {
        n_trigger += ((Histo1d*)h)->getEntries();
    }
    else if (h->getName() == TotDist::outputName()) {
        totDist->add(*(Histo1d*)h);
    }

}

void NoiseAnalysis::loadConfig(const json &j){
    if (j.contains("createMask")){
        createMask=j["createMask"];
    }
    if (j.contains("noiseThr")){
        noiseThr=j["noiseThr"];
    }
    if (j.contains("doAltMask")){
        doAltMask=j["doAltMask"];
    }
    if (j.contains("minOcc")){
        minOcc=j["minOcc"];
    }
}

void NoiseAnalysis::end() {
    auto noiseOcc = createHistoMap("NoiseOccupancy", "Noise Occupancy hits/bc", nCol, nRow);

    auto mask = createHistoMap(doAltMask ? "AltNoiseMask" : "NoiseMask",
                              doAltMask ? "AltMask" : "Mask",
                              nCol, nRow);

    noiseOcc->add(&*occ);
    noiseOcc->scale(1.0/(double)n_trigger);
    alog->info("[{}] Received {} total trigger!", id, n_trigger);

    // Fail tags for printout
    unsigned failNoiseOcc = 0, failBoth = 0, failBefore = 0, failNew = 0;

    for(unsigned col=1; col<=nCol; col++) {
        for (unsigned row=1; row<=nRow; row++) {
            unsigned i = noiseOcc->binNum(col, row);
            unsigned curEn = feCfg->getPixelEn(col-1, row-1, doAltMask);
            failBefore += (!curEn); // add currently disabled pixels to total count

            if (noiseOcc->getBin(i) > noiseThr) {
                failNoiseOcc++;
                if (occ->getBin(i) >= minOcc) {
                    failBoth++;

                    mask->setBin(i, 0);

                    // CurEn is 0 for disabled pixels, 1 for enabled
                    failNew += curEn; // add count only if pixel is currently enabled

                    if (make_mask&&createMask) {
                        // maskPixel starts at 0,0
                        feCfg->maskPixel(col-1, row-1, doAltMask);
                    }
                } else {
                    mask->setBin(i, 1);
                } // if
            } // if
        } // for
    } // for

    alog->info("[{}]  Found {} pixels failing noise occupancy cut of {}", id, failNoiseOcc, noiseThr);
    alog->info("[{}]  Found {} pixels failing noise + raw occupancy cut of {}", id, failBoth, minOcc);
    alog->info("[{}] Masked {} new pixels, for {} total ({} previously)", id, failNew, failBefore + failNew, failBefore);

    // Get averaged tot
    tot->divide(*occ);

    output->pushData(std::move(occ));
    output->pushData(std::move(tot));
    output->pushData(std::move(tag));
    output->pushData(std::move(noiseOcc));
    output->pushData(std::move(mask));
    output->pushData(std::move(totDist));
}

void NoiseTuning::init(const ScanLoopInfo *s) {
    n_count = 1;
    pixelFb = nullptr;
    globalFb = nullptr;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isGlobalFeedbackLoop()) {
            globalFb = std::make_unique<GlobalFeedbackSender>(feedback);
        }

        if (l->isPixelFeedbackLoop()) {
            pixelFb = std::make_unique<PixelFeedbackSender>(feedback);
        }
    }
}

void NoiseTuning::processHistogram(HistogramBase *h) {
    if (!(h->getName() == OccupancyMap::outputName()))
        return;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    // Plot name
    std::string name = "OccMap";

    // if (occMaps[uid] == nullptr) {
    //     Histo2d *hh = new Histo2d("OccMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5, loopStatus);
    //     hh->setXaxisTitle("Column");
    //     hh->setYaxisTitle("Row");
    //     hh->setZaxisTitle("Hits");
    //     innerCnt[uid] = 0;
    //     occMaps[uid].reset(hh);

    if (occMaps[uid] == nullptr) {
        innerCnt[uid] = 0;
        occMaps[uid] = createHistoMap(name, "Hits", nCol, nRow, loopStatus);
    }

    //Easy make it pretty
    occMaps[uid]->add(&*(Histo2d*)h);
    innerCnt[uid]++;

    if (innerCnt[uid] == n_count) {
        SPDLOG_LOGGER_TRACE(alog, "");
        if (globalFb != nullptr) { // Global Threshold Tuning
            SPDLOG_LOGGER_TRACE(alog, "");
            unsigned numOfHits = 0;
            for (unsigned i=0; i<occMaps[uid]->size(); i++) {
                if (occMaps[uid]->getBin(i) > 1) {
                    numOfHits++;
                }
            }
            alog->info("[{}] Number of pixels with hits: {}", id, numOfHits);
            if (numOfHits < 10) { // TODO not hardcode this value
                globalFb->feedbackStep(id, -1, false);
            } else {
                globalFb->feedbackStep(id, 0, true);
            }
        }

        if (pixelFb != nullptr) { // Pixel Threshold Tuning
            auto fbHisto = createHistoMap("feedback", "z", nCol, nRow);
            SPDLOG_LOGGER_TRACE(alog, "");
            unsigned pixelWoHits = 0;
            for (unsigned i=0; i<occMaps[uid]->size(); i++) {
                if (occMaps[uid]->getBin(i) < 2) { //TODO un-hardcode this
                    fbHisto->setBin(i, -1);
                    pixelWoHits++;
                } else {
                    fbHisto->setBin(i, 0);
                }
            }
            alog->info("[{}] Number of pixels with hits: {}", id, pixelWoHits);

            pixelFb->feedbackStep(id, std::move(fbHisto));
        }
        output->pushData(std::move(occMaps[uid]));
        occMaps[uid] = nullptr;
    }
}

void NoiseTuning::end() {
}

void DelayAnalysis::init(const ScanLoopInfo *s) {
    n_count = nCol*nRow;
    injections = 50;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop() || l->isParameterLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        // Vcal Loop
        if (l->isParameterLoop()) {
            delayLoop = n;
            delayMax = l->getMax();
            delayMin = l->getMin();
            delayStep = l->getStep();
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("DelayAnalysis: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
    }
    count = 0;
}

void DelayAnalysis::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != L13d::outputName())
        return;


    Histo3d *hh = (Histo3d*) h;
    for(unsigned l1=0; l1<16; l1++) { //TODO hardcoded l1
        for(unsigned col=1; col<=nCol; col++) {
            for (unsigned row=1; row<=nRow; row++) {
                long unsigned bin = hh->binNum(col, row, l1);
                if (hh->getBin(bin) != 0) {
                    //std::cout << col << " " << row << " " << l1 << " " << bin << std::endl;
                    // Select correct output containe
                    unsigned ident = (row-1)+((col-1)*(nRow));
                    unsigned delay = hh->getStat().get(delayLoop);
                    // Determine identifier
                    std::string name = "Delay";
                    name += "-" + std::to_string(col) + "-" + std::to_string(row);
                    // Check for other loops
                    /*
                       unsigned outerIdent = 0;
                       unsigned offset = nCol*nRow;
                       unsigned outerOffset = 1;
                       for (unsigned n=0; n<loops.size(); n++) {
                       ident += hh->getStat().get(loops[n])*offset;
                       outerIdent += hh->getStat().get(loops[n])*offset;
                       offset *= loopMax[n];
                       outerOffset *= loopMax[n];
                       name += "-" + std::to_string(hh->getStat().get(loops[n]));
                       }*/

                    // Check if Histogram exists
                    if (histos[ident] == nullptr) {
                        // TODO hardcoded
                        histos[ident] = createHisto1d(name, "Delay", 256, -0.5, 255.5, "Occupancy");
                        innerCnt[ident] = 0;
                        count++;
                    }

                    // Add up Histograms
                    histos[ident]->fill((16*l1)+delay, hh->getBin(bin));
                    innerCnt[ident]++;

                    // Got all data, finish up Analysis
                    if (delay == delayMax) { // TODO hardcoded
                        if (delayMap == nullptr) {
                            delayMap = createHistoMap("DelayMap", "Mean Delay", nCol, nRow);
                        }
                        if (rmsMap == nullptr) {
                            rmsMap = createHistoMap("RmsMap", "RMS", nCol, nRow);
                        }
                        if (histos[ident]->getMean() > 0 && histos[ident]->getMean() < 256) {
                            delayMap->setBin(ident, histos[ident]->getMean());
                            rmsMap->setBin(ident, histos[ident]->getStdDev());
                        }
                    }
                }
            }
        }
    }
}

void DelayAnalysis::end() {
    output->pushData(std::move(delayMap));
    output->pushData(std::move(rmsMap));
    for(unsigned bin=0; bin<(nRow*nCol); bin++) {
        if (histos[bin]) {
            output->pushData(std::move(histos[bin]));
            bin+=1000;
        }
    }
}

void ParameterAnalysis::loadConfig(const json &j){
    if (j.contains("createMap")){
        m_createMap = j["createMap"];
    }
    if (j.contains("targetLoopIndex")){
        paramLoopNo = j["targetLoopIndex"];
    }
}

void ParameterAnalysis::init(const ScanLoopInfo *s) {
    alog->info("ParameterAnalysis init");
    paramName = "UnknownParam";
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(paramLoopNo == n || l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop() || l->isParameterLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        }

        // Parameter loop of interest
        if (l->isParameterLoop() || paramLoopNo == n) {
            paramLoopNo = n;
            paramMax = l->getMax();
            paramMin = l->getMin();
            paramStep = l->getStep();
            paramBins = (paramMax-paramMin)/paramStep;
            auto paramLoop = dynamic_cast<const StdParameterAction*>(l);
            if(paramLoop == nullptr) {
                // In case targetLoopIndex points to non-parameter loop
                alog->info("ParameterAnalysis: loop declared as parameter loop does not have a name");
                paramName = "Loop"+std::to_string(paramLoopNo);
            } else {
                paramName = paramLoop->getParName();
            }
        }

        if (l->isTriggerLoop()) {
            auto trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("ParameterAnalysis: loop declared as trigger does not have a count");
            } else {
                injections = trigLoop->getTrigCnt();
            }
        }
    }

    if(paramLoopNo >= s->size()) {
      paramLoopNo = 0xffffffff;
      alog->error("ParameterAnalysis: no parameter loop found");
    }
}

void ParameterAnalysis::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    if(paramLoopNo == 0xffffffff) {
        // Already printed error in init
        return;
    }

    Histo2d *hh = (Histo2d*) h;

    // Get unique ID from LoopStatus
    LoopStatus loopStatus = h->getStat();
    loopStatus.setEnabledLoops(loops);
    LoopStatus::UID uid = loopStatus.uniqueID();

    for(unsigned col=1; col<=nCol; col++) {
        for (unsigned row=1; row<=nRow; row++) {
            unsigned bin = hh->binNum(col, row);
            if (hh->getBin(bin) != 0) {
                // Select correct output container
                unsigned param = hh->getStat().get(paramLoopNo);

                // Check if Histogram exists
                // if (paramMaps[uid] == nullptr) {
                //     Histo2d *hhh = new Histo2d(paramName, paramBins+1, paramMin-((double)paramStep/2.0), paramMax+((double)paramStep/2.0), injections-1, 0.5, injections-0.5, loopStatus);
                //     hhh->setXaxisTitle(paramName);
                //     hhh->setYaxisTitle("Occupancy");
                //     hhh->setZaxisTitle("Number of pixels");
                //     paramMaps[uid].reset(hhh);
                // }
                // if (paramCurves[uid] == nullptr) {
                //     Histo2d *hhh = new Histo2d(paramName + "_Map", nCol*nRow, -0.5, nCol*nRow-0.5, paramBins+1, paramMin-((double)paramStep/2.0), paramMax+((double)paramStep/2.0), loopStatus);
                //     hhh->setXaxisTitle("Channel Number");
                //     hhh->setYaxisTitle(paramName);
                //     hhh->setZaxisTitle("Number of Hits");
                //     paramCurves[uid].reset(hhh);

                if (paramMaps[uid] == nullptr) {
                    auto hhh = createHisto2d(paramName,
                         paramName, paramBins+1, paramMin-((double)paramStep/2.0), paramMax+((double)paramStep/2.0),
                         "Occupancy", injections-1, 0.5, injections-0.5,
                         "Number of pixels", 
                         loopStatus);
                    paramMaps[uid] = std::move(hhh);
                }
                if (paramCurves[uid] == nullptr) {
                    auto hhh = createHisto2d(paramName + "_Map",
                         "Channel Number", nCol*nRow, -0.5, nCol*nRow-0.5,
                         paramName, paramBins+1, paramMin-((double)paramStep/2.0), paramMax+((double)paramStep/2.0),
                         "Number of Hits",
                        loopStatus);
                    paramCurves[uid] = std::move(hhh);
                }

                // Add up Histograms
                double thisBin = hh->getBin(bin);
                paramMaps[uid]->fill(param, thisBin);
                paramCurves[uid]->fill(bin, param, thisBin);
            }
        }
    }
}

void ParameterAnalysis::end() {
    alog->trace("ParameterAnalysis end");
    for (auto &[uid, mapPtr] : paramMaps) {
        if (m_createMap) {
            output->pushData(std::move(paramCurves[uid]));
        }
        output->pushData(std::move(mapPtr));
    }
}

void TriggerThrottleAnalysis::init(const ScanLoopInfo *s) {
    n_count = 1;
    target_occ = 128;
    current_inj = 0;
    // int receivers;
    for (unsigned n=0; n<s->size(); n++) {
        auto l = s->getLoop(n);
        if (!(l->isTriggerLoop() || l->isMaskLoop() || l->isDataLoop())) {
            loops.push_back(n);
            loopMax.push_back((unsigned)l->getMax());
        } else {
            unsigned cnt = (l->getMax() - l->getMin())/l->getStep();
            if (cnt == 0)
                cnt = 1;
            n_count = n_count*cnt;
        }

        if (l->isTriggerLoop()) {
            trigLoop = dynamic_cast<const StdTriggerAction*>(l);
            if(trigLoop == nullptr) {
                alog->error("TriggerThrottleAnalysis");
            } else {
                start_inj = trigLoop->getTrigCnt();
                injections = start_inj;
            }
        }

        if (l->isGlobalFeedbackLoop()) {
            fb = std::make_unique<GlobalFeedbackSender>(feedback);
            if(fb == nullptr) {
                alog->error("TriggerThrottleAnalysis");
            }
        }
    }
}

void TriggerThrottleAnalysis::processHistogram(HistogramBase *h) {
    // Check if right Histogram
    if (h->getName() != OccupancyMap::outputName())
        return;

    // Select correct output container
    unsigned ident = 0;
    unsigned offset = 0;

    // Determine identifier
    std::string name = "OccupancyMap";
    std::string name2 = "OuterOccupancyMap";
    for (unsigned n=0; n<loops.size(); n++) {
        ident += h->getStat().get(loops[n])+offset;
        offset += loopMax[n];
        name += "-" + std::to_string(h->getStat().get(loops[n]));
        name2 += "-" + std::to_string(h->getStat().get(loops[n]));
    }

    // Check if Histogram exists
    if (occMaps[ident] == nullptr) {
        Histo2d *hh = new Histo2d(name, nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5);
        hh->setXaxisTitle("Column");
        hh->setYaxisTitle("Row");
        hh->setZaxisTitle("Hits");
        occMaps[ident].reset(hh);
        innerCnt[ident] = 0;
    }

    if (outerOccMaps[ident] == nullptr) {
        Histo2d *hh = new Histo2d(name2, nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5);
        hh->setXaxisTitle("Column");
        hh->setYaxisTitle("Row");
        hh->setZaxisTitle("Hits");
        outerOccMaps[ident].reset(hh);
    }

    // Add up Histograms
    occMaps[ident]->add(*(Histo2d*)h);
    outerOccMaps[ident]->add(*(Histo2d*)h);
    innerCnt[ident]++;

    // Got all data, finish up Analysis
    if (innerCnt[ident] == n_count) {

        double sign = 1;

        for(unsigned i=0; i<occMaps[ident]->size(); i++) {
            double occupancy = (occMaps[ident]->getBin(i))/(double)target_occ;
            if (sign == 1 && occupancy > 0.5)
                sign = 0;
            if (sign >= 0 && occupancy > 1.5) {
                sign = -1;
                break;
            }
        }        
        current_inj += injections;
        bool done = current_inj >= target_inj;
        alog->trace("Throttling trigger with {} injections, sign {}. Total inj {}. Target {}.",injections,sign, current_inj, target_inj);
        if (sign == 1) {
            injections *= 2;

            alog->warn("Trigger throttle wants to change trigger count {} {}",
                       trigLoop->getTrigCnt(),
                       injections);
            // trigLoop->setTrigCnt(injections);
        } else if (sign == -1) {
            injections /= 2;
        }
        if (done) {
            current_inj = 0;
            injections = start_inj;
            output->pushData(std::move(outerOccMaps[ident]));
            outerOccMaps[ident] = nullptr;
        } else if (injections+current_inj > target_inj) {
            injections = target_inj - current_inj;
        }

        alog->warn("Trigger throttle wants to change trigger count {} {}",
                   trigLoop->getTrigCnt(),
                   injections);
        // trigLoop->setTrigCnt(injections);
        fb->feedback(this->id, sign, done);

        //output->pushData(std::move(occMaps[ident]));
        innerCnt[ident] = 0;
        //delete occMaps[ident];
        occMaps[ident] = nullptr;
    }
}

void TriggerThrottleAnalysis::end() {

}

void TriggerThrottleAnalysis::loadConfig(const json &j) {
    if (!j["target_occ"].empty()) {
        target_occ = j["target_occ"];
    }
    if (!j["target_trigs"].empty()) {
        target_inj = j["target_trigs"];
    } else 
        target_inj = 10000;
}
