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

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QTimer>

namespace FOEDAG {

// Measures how long the GUI event loop is blocked: a timer due every interval
// fires late by that much. Writes one JSON line per report period with the worst
// lag in it, for aurora2's GUI latency test (flow engine T-024).
class EventLoopProbe : public QObject {
 public:
  static constexpr const char* ENV = "AURORA_EVENT_LOOP_PROBE";

  // A probe writing to the file $AURORA_EVENT_LOOP_PROBE names; nullptr if unset.
  static EventLoopProbe* fromEnvironment(QObject* parent);

  EventLoopProbe(const QString& path, QObject* parent, int intervalMs = 10,
                 int reportMs = 1000);
  bool isOpen() const { return m_out.isOpen(); }

 private:
  void tick();

  QFile m_out;
  QTimer m_timer;
  QElapsedTimer m_clock;
  const int m_intervalMs;
  const int m_reportMs;
  qint64 m_lastTick = 0;
  qint64 m_periodStart = 0;
  qint64 m_maxLagMs = 0;
  int m_ticks = 0;
};

}  // namespace FOEDAG
