/*
Copyright 2026 The Foedag team

GPL License

Copyright (c) 2026 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

class QWidget;

namespace FOEDAG {

// The screenshot gate's parts (REQ-013 to REQ-017, REQ-031), kept free of
// the Tcl command so they can be unit tested.
namespace GuiScreenshot {

constexpr int kDefaultSettleMs = 2000;
const QColor kMaskColor{255, 0, 255};

// The optional <step>.json next to a golden.
struct Sidecar {
  int tolerance{0};      // per channel
  int maxDiffPixels{0};  // differing pixels allowed
  QList<QRect> masks;    // in the captured widget's coordinates
};
// A missing file gives the defaults; a broken one sets error.
Sidecar readSidecar(const QString &path, QString *error);

// Grabs the widget with every mask filled.
QImage capture(QWidget *widget, const QList<QWidget *> &maskWidgets,
               const QList<QRect> &maskRects);

// Grabs until two grabs in a row are identical. False if the image is
// still changing when the timeout expires ("unstable").
bool settle(QWidget *widget, const QList<QWidget *> &maskWidgets,
            const QList<QRect> &maskRects, int timeoutMs, QImage *image);

// Pixels whose channels differ by more than the tolerance; -1 when the
// sizes differ.
int countDifferences(const QImage &golden, const QImage &current,
                     int tolerance);

// golden | current | difference × 8, for a failed gate or a changed golden.
QImage triptych(const QImage &golden, const QImage &current);

// Golden subdirectory for this build's platform.
QString platformDir();

}  // namespace GuiScreenshot
}  // namespace FOEDAG
