#pragma once

#include "document/Document.hpp"

#include <QSize>
#include <QVector>

namespace wobble
{

struct LayerCompositionMemoryEstimate final
{
    bool valid = false;
    int peakSurfaceCount = 0;
    quint64 bytesPerSurface = 0;
    quint64 peakBytes = 0;
};

class LayerCompositionPlan final
{
public:
    static constexpr int paintOperationScratchSurfaceCount = 2;

    enum class OperationType
    {
        PaintLayer,
        BeginGroup,
        EndGroup
    };

    struct Operation final
    {
        OperationType type = OperationType::PaintLayer;
        int layerIndex = -1;
        int matchingOperationIndex = -1;
    };

    static LayerCompositionPlan build(const Document &document);

    bool isValid() const;
    const QVector<Operation> &operations() const;
    int peakSurfaceCount() const;
    LayerCompositionMemoryEstimate memoryEstimate(
        const QSize &surfaceSize) const;

private:
    bool m_valid = false;
    int m_peakSurfaceCount = 0;
    QVector<Operation> m_operations;
};

}
