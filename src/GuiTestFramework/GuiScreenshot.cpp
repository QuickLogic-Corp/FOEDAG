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
#include "GuiScreenshot.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cstdlib>

namespace FOEDAG::GuiScreenshot {

namespace {

constexpr int kSettlePollMs = 50;
constexpr int kDiffGain = 8;

void wait(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

}  // namespace

Sidecar readSidecar(const QString &path, QString *error) {
  Sidecar sidecar;
  QFile file{path};
  if (!file.exists()) return sidecar;
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QString("cannot read %1").arg(path);
    return sidecar;
  }
  QJsonParseError parse;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (!doc.isObject()) {
    *error = QString("%1: %2").arg(path, parse.errorString());
    return sidecar;
  }
  const QJsonObject json = doc.object();
  sidecar.tolerance = json.value("tol").toInt(0);
  sidecar.maxDiffPixels = json.value("max_diff_pixels").toInt(0);
  for (const QJsonValue &mask : json.value("masks").toArray()) {
    const QJsonArray r = mask.toArray();
    if (r.size() != 4) {
      *error = QString("%1: a mask is [x, y, w, h]").arg(path);
      return sidecar;
    }
    sidecar.masks << QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(),
                           r[3].toInt());
  }
  return sidecar;
}

QImage capture(QWidget *widget, const QList<QWidget *> &maskWidgets,
               const QList<QRect> &maskRects) {
  QImage image = widget->grab().toImage().convertToFormat(
      QImage::Format_ARGB32);
  QPainter painter{&image};
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  // grab() renders at the device pixel ratio; masks are in widget pixels.
  painter.scale(image.devicePixelRatio(), image.devicePixelRatio());
  for (QWidget *mask : maskWidgets) {
    if (!mask->isVisible()) continue;
    const QRect rect{mask->mapTo(widget, QPoint{0, 0}), mask->size()};
    painter.fillRect(rect, kMaskColor);
  }
  for (const QRect &rect : maskRects) painter.fillRect(rect, kMaskColor);
  painter.end();
  image.setDevicePixelRatio(1);
  return image;
}

bool settle(QWidget *widget, const QList<QWidget *> &maskWidgets,
            const QList<QRect> &maskRects, int timeoutMs, QImage *image) {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
  QElapsedTimer elapsed;
  elapsed.start();
  QImage previous = capture(widget, maskWidgets, maskRects);
  while (true) {
    wait(kSettlePollMs);
    QImage current = capture(widget, maskWidgets, maskRects);
    if (current == previous) {
      *image = current;
      return true;
    }
    if (elapsed.elapsed() >= timeoutMs) {
      *image = current;
      return false;
    }
    previous = current;
  }
}

int countDifferences(const QImage &golden, const QImage &current,
                     int tolerance) {
  if (golden.size() != current.size()) return -1;
  const QImage a = golden.convertToFormat(QImage::Format_ARGB32);
  const QImage b = current.convertToFormat(QImage::Format_ARGB32);
  int differing = 0;
  for (int y = 0; y < a.height(); ++y) {
    auto pa = reinterpret_cast<const QRgb *>(a.constScanLine(y));
    auto pb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const int d = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])),
                              std::abs(qGreen(pa[x]) - qGreen(pb[x])),
                              std::abs(qBlue(pa[x]) - qBlue(pb[x])),
                              std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))});
      if (d > tolerance) ++differing;
    }
  }
  return differing;
}

QImage triptych(const QImage &golden, const QImage &current) {
  const QImage a = golden.convertToFormat(QImage::Format_ARGB32);
  const QImage b = current.convertToFormat(QImage::Format_ARGB32);
  const int w = std::max(a.width(), b.width());
  const int h = std::max(a.height(), b.height());
  QImage diff{w, h, QImage::Format_ARGB32};
  diff.fill(Qt::black);
  for (int y = 0; y < h; ++y) {
    auto out = reinterpret_cast<QRgb *>(diff.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const bool inA = x < a.width() && y < a.height();
      const bool inB = x < b.width() && y < b.height();
      if (!inA || !inB) {
        out[x] = qRgb(255, 255, 255);  // only one image covers it
        continue;
      }
      const QRgb pa = a.pixel(x, y);
      const QRgb pb = b.pixel(x, y);
      auto amplify = [](int c1, int c2) {
        return std::min(255, std::abs(c1 - c2) * kDiffGain);
      };
      out[x] = qRgb(amplify(qRed(pa), qRed(pb)),
                    amplify(qGreen(pa), qGreen(pb)),
                    amplify(qBlue(pa), qBlue(pb)));
    }
  }
  QImage result{3 * w, h, QImage::Format_ARGB32};
  result.fill(Qt::white);
  QPainter painter{&result};
  painter.drawImage(0, 0, a);
  painter.drawImage(w, 0, b);
  painter.drawImage(2 * w, 0, diff);
  return result;
}

QString platformDir() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "darwin";
#else
  return "linux";
#endif
}

}  // namespace FOEDAG::GuiScreenshot
