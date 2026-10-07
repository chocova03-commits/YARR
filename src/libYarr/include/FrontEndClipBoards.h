#ifndef YARR_FRONT_END_CLIPBOARDS_H
#define YARR_FRONT_END_CLIPBOARDS_H

#include "ClipBoard.h"

#include "RawData.h"
#include "EventDataBase.h"
#include "HistogramBase.h"
#include "ResultBase.h"

/// Clipboards to buffer data between processors
struct FrontEndClipBoards {
    ClipBoard<RawDataContainer> clipRawData;
    /// Copy of clipRawData for RawDataArchiver, only fed if archiveRawData is set
    ClipBoard<RawDataContainer> clipRawDataArchive;
    bool archiveRawData = false;
    ClipBoard<EventDataBase> clipData;
    ClipBoard<HistogramBase> clipHisto;
    ClipBoard<FeedbackProcessingInfo> clipProcFeedback;
    std::vector<std::unique_ptr<ClipBoard<HistogramBase>> > clipResult;

    /// Push raw data for processing, plus a copy for the RawDataArchiver if enabled.
    /// The copy shares the RawData buffers, which is fine as long as processors don't modify their input.
    void pushRawData(std::unique_ptr<RawDataContainer> rdc) {
        if (archiveRawData)
            clipRawDataArchive.pushData(std::make_unique<RawDataContainer>(*rdc));
        clipRawData.pushData(std::move(rdc));
    }
};

#endif
