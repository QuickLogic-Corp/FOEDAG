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
#include "Main/EventLoopProbe.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <filesystem>

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

void runEventLoop(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

std::filesystem::path probeFile() {
  return std::filesystem::temp_directory_path() / "event_loop_probe_test.jsonl";
}

}  // namespace

TEST(EventLoopProbe, unset_environment_starts_no_probe) {
  qunsetenv(EventLoopProbe::ENV);
  EXPECT_EQ(EventLoopProbe::fromEnvironment(nullptr), nullptr);
}

// Each report line carries the longest stretch the loop was blocked in its period.
TEST(EventLoopProbe, reports_the_blocked_time_per_period) {
  const auto path = probeFile();
  std::filesystem::remove(path);
  qputenv(EventLoopProbe::ENV, QByteArray::fromStdString(path.string()));
  std::unique_ptr<EventLoopProbe> probe{EventLoopProbe::fromEnvironment(nullptr)};
  qunsetenv(EventLoopProbe::ENV);
  ASSERT_NE(probe, nullptr);
  ASSERT_TRUE(probe->isOpen());

  QTimer::singleShot(300, [] { QThread::msleep(250); });
  QTimer::singleShot(1500, [] { QThread::msleep(600); });
  runEventLoop(2600);
  probe.reset();

  QFile f(QString::fromStdString(path.string()));
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
  ASSERT_GE(lines.size(), 2);
  QJsonObject first = QJsonDocument::fromJson(lines[0]).object();
  QJsonObject second = QJsonDocument::fromJson(lines[1]).object();
  EXPECT_GE(first["max_lag_ms"].toInt(), 240);
  EXPECT_LT(first["max_lag_ms"].toInt(), 590);
  EXPECT_GE(second["max_lag_ms"].toInt(), 590);
  EXPECT_GT(first["ticks"].toInt(), 0);
  EXPECT_GT(second["t_ms"].toInt(), first["t_ms"].toInt());
  std::filesystem::remove(path);
}
