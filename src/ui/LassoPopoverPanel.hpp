// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#pragma once

#include <QWidget>

namespace ugurugu
{

class CanvasWidget;

class LassoPopoverPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LassoPopoverPanel(CanvasWidget *canvas, QWidget *parent = nullptr);
};

}
