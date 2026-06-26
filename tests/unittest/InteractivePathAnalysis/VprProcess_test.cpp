/**
  * @file VprProcess_test.cpp
  * @author Oleksandr Pyvovarov (APivovarov@quicklogic.com or
  aleksandr.pivovarov.84@gmail.com or
  * https://github.com/w0lek)
  * @date 2024-03-12
  * @copyright Copyright 2021 The Foedag team

  * GPL License

  * Copyright (c) 2021 The Open-Source FPGA Foundation

  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.

  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.

  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Verifies Process (VprProcess) error deduplication and bypass list behavior.
// These tests run headless — Process is pure QProcess/signal, no widget needed.

#include "InteractivePathAnalysis/VprProcess.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

// Run a Process with the given program/args, wait up to timeoutMs for it to
// finish, and return all messages emitted via innerErrorOccurred.
QStringList runAndCollect(const QString& program, const QStringList& args,
                          const QStringList& bypass = {},
                          int timeoutMs = 5000) {
  Process proc("test");
  for (const auto& b : bypass) proc.addInnerErrorToBypass(b);

  QStringList collected;
  QObject::connect(&proc, &Process::innerErrorOccurred,
                   [&](QString msg) { collected << msg; });

  proc.setProgram(program);
  proc.setArguments(args);
  proc.QProcess::start();

  QEventLoop loop;
  QObject::connect(&proc,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   &loop, &QEventLoop::quit);
  QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
  loop.exec();
  // Drain any remaining async events (e.g. last readyReadStandardError)
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
  return collected;
}

// Python3 one-liner that writes messages to stderr with explicit flushes and
// small sleeps so Qt dispatches each as a separate readyReadStandardError event.
QString stderrScript(const QStringList& lines) {
  QStringList stmts;
  for (int i = 0; i < lines.size(); ++i) {
    stmts << QString("sys.stderr.write('%1\\n'); sys.stderr.flush()").arg(
        lines[i]);
    if (i < lines.size() - 1) stmts << "time.sleep(0.05)";
  }
  return "import sys,time; " + stmts.join("; ");
}

}  // namespace

// Each unique message should fire innerErrorOccurred exactly once.
TEST(VprProcess, UniqueMessagesEachFireOnce) {
  QStringList msgs{"first_warning", "second_warning", "third_warning"};
  auto collected = runAndCollect(
      "python3", {"-c", stderrScript(msgs)});

  ASSERT_EQ(3, collected.size());
  EXPECT_TRUE(collected[0].contains("first_warning"));
  EXPECT_TRUE(collected[1].contains("second_warning"));
  EXPECT_TRUE(collected[2].contains("third_warning"));
}

// A message emitted twice to stderr must fire the signal only once (dedup).
TEST(VprProcess, DuplicateMessageEmittedOnce) {
  QStringList lines{"vpr_timing_warning", "vpr_timing_warning", "other_warning"};
  auto collected = runAndCollect(
      "python3", {"-c", stderrScript(lines)});

  ASSERT_EQ(2, collected.size());
  EXPECT_TRUE(collected[0].contains("vpr_timing_warning"));
  EXPECT_TRUE(collected[1].contains("other_warning"));
}

// A message matching the bypass list must never fire innerErrorOccurred.
TEST(VprProcess, BypassedMessageNotEmitted) {
  QStringList lines{"GLib-GIO-WARNING **: accessibility bus error",
                    "real_vpr_error"};
  auto collected = runAndCollect(
      "python3", {"-c", stderrScript(lines)},
      /*bypass=*/{"GLib-GIO-WARNING **:"});

  ASSERT_EQ(1, collected.size());
  EXPECT_TRUE(collected[0].contains("real_vpr_error"));
}

// A bypassed message repeated multiple times must never fire the signal.
TEST(VprProcess, BypassedDuplicateNeverEmitted) {
  QStringList lines{"GTK_IS_LABEL error", "GTK_IS_LABEL error",
                    "another_error"};
  auto collected = runAndCollect(
      "python3", {"-c", stderrScript(lines)},
      /*bypass=*/{"GTK_IS_LABEL"});

  ASSERT_EQ(1, collected.size());
  EXPECT_TRUE(collected[0].contains("another_error"));
}

// When no stderr is produced, no signal should fire.
TEST(VprProcess, NoStderrNoSignal) {
  auto collected = runAndCollect(
      "python3", {"-c", "import sys; sys.stdout.write('stdout only\\n')"});
  EXPECT_EQ(0, collected.size());
}
