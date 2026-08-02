#pragma once

#include "document/Document.hpp"

namespace wobble
{
namespace DocumentOperations
{

QSize initialCanvasSize(
    const QVector<Stroke> &operations, const QSize &fallback);

bool normalizeAndValidate(Document &document);

}

}
