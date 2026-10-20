#pragma once
#include <QString>
#include <memory>

struct MeshData;
struct ToolMove;

enum class RetractionEvidenceState { Separated, Interference, Uncertain };

struct RetractionEvidence {
    RetractionEvidenceState state = RetractionEvidenceState::Uncertain;
    QString message;
};

// Bounded numerical evidence for the nominal cutter/shank cylinders, not the holder or machine.
RetractionEvidence checkExactRetraction(const MeshData &mesh, const ToolMove &move);

// Source preparation is shared only inside one synchronous motion check.
// Standalone checks always create their own batch.
class ExactRetractionBatch {
    friend class SimulationController;
    friend RetractionEvidence checkExactRetraction(const MeshData &, const ToolMove &);
    explicit ExactRetractionBatch(const MeshData &mesh);
    ~ExactRetractionBatch();
    ExactRetractionBatch(const ExactRetractionBatch &) = delete;
    ExactRetractionBatch &operator=(const ExactRetractionBatch &) = delete;
    RetractionEvidence check(const ToolMove &move);
    struct Source;
    const MeshData &m_mesh;
    std::unique_ptr<Source> m_source;
};
