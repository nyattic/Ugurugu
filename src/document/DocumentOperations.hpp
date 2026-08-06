// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#pragma once

#include "document/Document.hpp"

namespace ugurugu
{
namespace DocumentOperations
{

QSize initialCanvasSize(
    const QVector<Stroke> &operations, const QSize &fallback);

// Normalizes missing layer epochs and validates the ordered framebuffer
// operations without mutating the caller on failure.
bool normalizeAndValidate(Document &document);

}

}
