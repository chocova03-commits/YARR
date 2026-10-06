#include "catch.hpp"

#include "ScanHelper.h"
#include "AllAnalyses.h"
#include "AllChips.h"
#include "AnalysisDataProcessor.h"
#include "Bookkeeper.h"
#include "ScanFactory.h"
#include "FrontEndClipBoards.h"
#include "LoopStatus.h"

#include <set>
#include <utility>

#include "EmptyHw.h"
#include "EmptyFrontEnd.h"

namespace {
  // Minimal ScanLoopInfo, so a fixed loop geometry can be handed to an
  // analysis without a full ScanFactory/LoopEngine.
  class TestScanInfo : public ScanLoopInfo {
      struct TestLoopInfo : public LoopActionBaseInfo {
        TestLoopInfo(LoopStyle s, int mn, int mx, unsigned st)
          : LoopActionBaseInfo(s) {
          min = mn;
          max = mx;
          step = st;
        }
      };

      std::vector<TestLoopInfo> loops;

    public:
      explicit TestScanInfo(const std::vector<TestLoopInfo> &in) : loops(in) {}
      unsigned size() const override { return loops.size(); }
      const LoopActionBaseInfo *getLoop(unsigned n) const override { return &loops[n]; }
  };
}

TEST_CASE("AnalysisHierarchy", "[Analysis]") {

  // Configuration
  json cfg;
  // Four analysis algorithms
  cfg["scan"]["analysis"]["0"]["algorithm"] = "AnalysisA";
  cfg["scan"]["analysis"]["0"]["dependOn"]  = {"AnalysisB"}; 

  cfg["scan"]["analysis"]["1"]["algorithm"] = "AnalysisB";

  cfg["scan"]["analysis"]["2"]["algorithm"] = "AnalysisC";
  cfg["scan"]["analysis"]["2"]["dependOn"]  = {"AnalysisB"};

  cfg["scan"]["analysis"]["3"]["algorithm"] = "AnalysisD";
  cfg["scan"]["analysis"]["3"]["dependOn"]  = {"AnalysisA", "AnalysisB"};

  cfg["scan"]["analysis"]["n_count"] = 4;

  std::vector<std::vector<int>> algoIndexTiers;
  ScanHelper::buildAnalysisHierarchy(algoIndexTiers, cfg["scan"]["analysis"]);

  // Expected algorithm tiers
  // tier 0:     B(1)
  //            / | \
  // tier 1: A(0) | C(2)
  //            \ |
  // tier 2:     D(3)
  std::vector<std::vector<int>> expIndexTiers = {{1}, {2, 0}, {3}};

  CAPTURE (algoIndexTiers);

  REQUIRE (algoIndexTiers == expIndexTiers);
}

TEST_CASE("AnalysisChainIO", "[Analysis]") {

  EmptyHw hwCtrl;
  Bookkeeper bookie(&hwCtrl, &hwCtrl);
  FeedbackClipboardMap fbData;
  ScanFactory scan(&bookie, &fbData);

  // Add one FE, with large enough histogram for testing hit
  auto fe = std::make_unique<EmptyFrontEnd>(FrontEndGeometry{10, 10});
  fe->setActive(true);

  REQUIRE ( fe );

  REQUIRE ( fe->isActive() );

  unsigned channel = 42;
  unsigned last_id = bookie.addFe(std::move(fe), channel);

  // Create a dummy scan config with known analyses to test IO connections
  // The dependency here is for test only and is arbitrary and meaningless
  json scanCfg;
  // tier 0:  OccupancyAnalysis(0)  
  //           |               |
  // tier 1:   |         L1Analysis(1)
  //            \             /
  // tier 2:    TagAnalysis(2)

  scanCfg["scan"]["analysis"]["0"]["algorithm"] = "OccupancyAnalysis";

  scanCfg["scan"]["analysis"]["1"]["algorithm"] = "L1Analysis";
  scanCfg["scan"]["analysis"]["1"]["dependOn"]  = {"OccupancyAnalysis"};

  scanCfg["scan"]["analysis"]["2"]["algorithm"] = "TagAnalysis";
  scanCfg["scan"]["analysis"]["2"]["dependOn"]  = {"OccupancyAnalysis","L1Analysis"};

  scanCfg["scan"]["analysis"]["n_count"] = 3;


  // Build analyses
  std::map<unsigned, std::vector<std::unique_ptr<AnalysisDataProcessor>> > analyses;
  ScanHelper::buildAnalyses(analyses, scanCfg, bookie, &scan, &fbData, -1, "./", -1, -1);

  auto& AnalysisProcessors = analyses[last_id];
  
  // Check there are three analysis tiers
  REQUIRE (AnalysisProcessors.size() == 3);

  // Start the analysis processors
  for (auto& aproc : AnalysisProcessors) {
    aproc->init();
    aproc->run();
  }

  // Old fe has been moved to BK, get pointer
  auto our_fe = bookie.getFe(last_id);
  
  // Send an OccupancyMap histogram to the analysis chain
  unsigned nCol = our_fe->geo.nCol;
  unsigned nRow = our_fe->geo.nRow;
  Histo2d* occmap = new Histo2d("OccupancyMap", nCol, 0.5, nCol+0.5, nRow, 0.5, nRow+0.5);
  occmap->setBin(1, 43);

  std::unique_ptr<HistogramBase> h(occmap);
  auto &cp = our_fe->clipboards();
  cp.clipHisto.pushData(std::move(h));

  // The input histogram should be processed by the OccupancyAnalysis
  // The other two algorithms should do nothing but only pass the output of OccupancyAnalysis to the downstream

  // Join threads
  cp.clipHisto.finish();

  for (unsigned i=0; i<AnalysisProcessors.size(); ++i) {
    AnalysisProcessors[i]->join();
    cp.clipResult.at(i)->finish();
  }

  // Check the final output
  auto &output = *(cp.clipResult.back());

  REQUIRE (!output.empty());

  while(!output.empty()) {
    auto result = output.popData();
    CAPTURE (result->getName());
    // Two histograms are expected in the ouput: OccupancyMap and EnMask
    // Both are from the OccupancyAnalysis

    if (result->getName() == "OccupancyMap") {
      auto h2d = dynamic_cast<Histo2d*>(result.get());
      REQUIRE (h2d);

      CHECK (h2d->getOverflow() < 1e-9);
      CHECK (h2d->getUnderflow() < 1e-9);

      // Check its content
      REQUIRE (h2d->getBin(1) == 43);
    }
  }

}

// Drive OccupancyAnalysis through multiple loop iterations with distinct
// LoopStatus, to check that it buckets by uniqueID() rather than
// conflating different parameter/mask-step positions into one histogram.
// This exercises the LoopStatus/uniqueID machinery the way a real
// multi-loop scan config does, which the single-histogram AnalysisChainIO
// test above does not.
TEST_CASE("AnalysisChainUID", "[Analysis]") {

  EmptyHw hwCtrl;
  Bookkeeper bookie(&hwCtrl, &hwCtrl);

  FrontEndGeometry geom{4, 4};
  unsigned nCol = geom.nCol;
  unsigned nRow = geom.nRow;
  auto fe = std::make_unique<EmptyFrontEnd>(geom);
  fe->setActive(true);
  unsigned last_id = bookie.addFe(std::move(fe), 42);

  // Two "outer" loops that OccupancyAnalysis treats as parameter-of-interest
  // loops (bucketed separately, one output histogram per combination), plus
  // a mask loop and a trigger loop that get summed over into n_count.
  //   loop 0: parameter loop,   3 steps (0, 1, 2)
  //   loop 1: global feedback,  2 steps (0, 1) -- also kept as a distinct bucket
  //   loop 2: mask loop,        2 steps (0, 1) -- summed into n_count, not a UID axis
  //   loop 3: trigger loop,     100 count
  // Mask/trigger loop counts follow the (max-min)/step convention used by
  // real mask loops (e.g. a 64-step pixel mask loop has min=0, max=64), so
  // 2 mask iterations means max=2, not max=1.
  TestScanInfo scanInfo({
      {LOOP_STYLE_PARAMETER,       0, 2, 1},
      {LOOP_STYLE_GLOBAL_FEEDBACK, 0, 1, 1},
      {LOOP_STYLE_MASK,            0, 2, 1},
      {LOOP_STYLE_TRIGGER,         0, 0, 1},
  });

  auto analysis = StdDict::getAnalysis("OccupancyAnalysis");
  REQUIRE (analysis);
  analysis->setConfig(bookie.getFeCfg(last_id));
  analysis->setMapSize(geom);
  analysis->loadConfig(json::object());

  AnalysisProcessor proc(last_id);
  auto &cp = bookie.getFe(last_id)->clipboards();
  cp.clipResult.emplace_back(new ClipBoard<HistogramBase>());
  proc.connect(&scanInfo, &(cp.clipHisto), cp.clipResult.back().get(), nullptr);
  proc.addAlgorithm(std::move(analysis));

  proc.init();
  proc.run();

  // For each of the 2*2=4 (loop0, loop1) combinations, push both mask-loop
  // iterations so OccupancyAnalysis considers that bucket complete, filling
  // bin 0 with a value that identifies the (loop0, loop1) combination.
  for (unsigned p=0; p<3; p++) {
    for (unsigned g=0; g<2; g++) {
      for (unsigned m=0; m<2; m++) {
        LoopStatus stat({p, g, m, 0},
                         {LOOP_STYLE_PARAMETER, LOOP_STYLE_GLOBAL_FEEDBACK,
                          LOOP_STYLE_MASK, LOOP_STYLE_TRIGGER});
        auto h = std::make_unique<Histo2d>("OccupancyMap",
                                            nCol, 0.5, nCol+0.5,
                                            nRow, 0.5, nRow+0.5, stat);
        h->setBin(0, 10*p + g);
        cp.clipHisto.pushData(std::move(h));
      }
    }
  }

  cp.clipHisto.finish();
  proc.join();
  cp.clipResult.back()->finish();

  auto &output = *(cp.clipResult.back());
  REQUIRE (!output.empty());

  // One OccupancyMap + one EnMask per (loop0, loop1) combination: 3*2*2 = 12
  std::set<std::pair<unsigned,unsigned>> seenCombinations;
  unsigned occMapCount = 0;

  while (!output.empty()) {
    auto result = output.popData();
    CAPTURE (result->getName());

    if (result->getName() != "OccupancyMap")
      continue;

    occMapCount++;

    auto h2d = dynamic_cast<Histo2d*>(result.get());
    REQUIRE (h2d);

    unsigned p = h2d->getStat().get(0);
    unsigned g = h2d->getStat().get(1);

    // Both mask-loop iterations contributed, so bin 0 should be 2x the
    // per-iteration value -- if UID bucketing were wrong (e.g. conflating
    // distinct (p, g) combinations), this sum would be corrupted.
    CHECK (h2d->getBin(0) == 2 * (10*p + g));

    auto inserted = seenCombinations.insert({p, g});
    // Each (p, g) combination should produce exactly one OccupancyMap
    CHECK (inserted.second);
  }

  // All 3*2 = 6 (loop0, loop1) combinations produced a distinct histogram
  CHECK (occMapCount == 6);
  CHECK (seenCombinations.size() == 6);
}
