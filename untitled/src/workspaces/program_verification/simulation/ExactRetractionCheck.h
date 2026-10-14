#pragma once
#include <QString>

struct MeshData;
struct ToolMove;

enum class RetractionEvidenceState { Separated, Interference, Uncertain };

struct RetractionEvidence {
    RetractionEvidenceState state = RetractionEvidenceState::Uncertain;
    QString message;
};

// Bounded numerical evidence for the nominal cutter/shank cylinders, not the holder or machine.
RetractionEvidence checkExactRetraction(const MeshData &mesh, const ToolMove &move);
