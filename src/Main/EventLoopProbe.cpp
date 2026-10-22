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
#include "EventLoopProbe.h"

#include <algorithm>
#include <iostream>

namespace FOEDAG {

EventLoopProbe* EventLoopProbe::fromEnvironment(QObject* parent) {
  const QString path = qEnvironmentVariable(ENV);
  if (path.isEmpty()) return nullptr;
  return new EventLoopProbe(path, parent);
}

EventLoopProbe::EventLoopProbe(const QString& path, QObject* parent,
                               int intervalMs, int reportMs)
    : QObject(parent),
      m_out(path),
      m_intervalMs(intervalMs),
      m_reportMs(reportMs) {
  if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    std::cerr << ENV << ": cannot write " << path.toStdString() << std::endl;
    return;
  }
  m_timer.setTimerType(Qt::PreciseTimer);
  connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
  m_clock.start();
  m_timer.start(m_intervalMs);
}

void EventLoopProbe::tick() {
  const qint64 now = m_clock.elapsed();
  m_maxLagMs = std::max(m_maxLagMs, now - m_lastTick - m_intervalMs);
  m_lastTick = now;
  ++m_ticks;
  if (now - m_periodStart < m_reportMs) return;
  // Flushed per line, so a GUI that is killed still leaves its measurements.
  m_out.write(QStringLiteral("{\"t_ms\": %1, \"max_lag_ms\": %2, \"ticks\": %3}\n")
                  .arg(now)
                  .arg(m_maxLagMs)
                  .arg(m_ticks)
                  .toUtf8());
  m_out.flush();
  m_periodStart = now;
  m_maxLagMs = 0;
  m_ticks = 0;
}

}  // namespace FOEDAG
