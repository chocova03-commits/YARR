#ifndef YARR_HISTOGRAM_ALGORITHM_H
#define YARR_HISTOGRAM_ALGORITHM_H

#include <memory>
#include <thread>

#include "FrontEndGeometry.h"
#include "HistogramBase.h"
#include "LoopStatus.h"

// Could be EventDataBase?
class FrontEndData;

/**
 * Process a stream of events and produce a histogram.
 */
class HistogramAlgorithm {
    public:
        HistogramAlgorithm() {
        }
        virtual ~HistogramAlgorithm() = default;

        virtual void create(const LoopStatus &stat) {}
        
        std::unique_ptr<HistogramBase> getHisto() {
            return std::move(r);
        }
        
        virtual void processEvent(FrontEndData *data) {}
        virtual void loadConfig(const json &config) {}
        
        void setMapSize(const FrontEndGeometry &geom) {
            fe_geom = geom;
        }
    protected:
        std::unique_ptr<HistogramBase> r;
        FrontEndGeometry fe_geom{336, 80};
};

#endif
