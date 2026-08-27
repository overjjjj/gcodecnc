#pragma once

#include "SlotFrameFeature.h"

struct SlotFrameRecognitionResult {
    bool ok = false;
    SlotFrameFeature feature;
    QStringList reasons;
};

class SlotFrameRecognizer
{
public:
    static SlotFrameRecognitionResult Recognize(
        const MachiningFeature &source,
        const SetupContext &setup,
        const SlotFrameTopologyEvidence &evidence);
#ifdef CNEXT_ENABLE_OCC
    static SlotFrameTopologyEvidence BuildEvidence(
        const TopoGraph &graph,
        const MachiningFeature &source);
#endif
};
