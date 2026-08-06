// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#pragma once

#include "document/Document.hpp"

#include <QSize>

namespace ugurugu
{

class AnimationExportPolicy final
{
public:
    static long double estimatedWorkingBytes(
        const QSize &frameSize, qsizetype frameCount);
    static long double estimatedWorkingBytes(const Document &document);
    static long double estimatedWorkingBytes(
        const Document &document, const QSize &frameSize);
    static bool fitsMemoryBudget(const QSize &frameSize, qsizetype frameCount);
    static bool fitsMemoryBudget(const Document &document);
    static bool fitsMemoryBudget(
        const Document &document, const QSize &frameSize);
};

}
