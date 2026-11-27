/**
  * @file VprProcess.cpp
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

#include "VprProcess.h"

#include "SimpleLogger.h"

#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace FOEDAG {

namespace {
// Every live Process. Aurora exits through Tcl's exit, which calls C exit()
// and so destroys no widget: a VPR server left running would outlive aurora
// and, on Windows, keep its inherited stdout open.
QSet<Process*>& liveProcesses() {
  static QSet<Process*> processes;
  return processes;
}

#ifdef _WIN32
// On Windows, Tcl's exit can end the process without running atexit handlers,
// so each child also goes into a job that Windows kills when aurora ends,
// however it ends. The handle is never closed: the OS closes it at exit.
void killWithThisProcess(qint64 pid) {
  static HANDLE job = []() {
    HANDLE handle = CreateJobObjectW(nullptr, nullptr);
    if (handle) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(handle, JobObjectExtendedLimitInformation,
                              &limits, sizeof(limits));
    }
    return handle;
  }();
  if (!job || pid <= 0) return;
  HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                               static_cast<DWORD>(pid));
  if (!process) return;
  if (!AssignProcessToJobObject(job, process))
    SimpleLogger::instance().error("could not tie the process to aurora:",
                                   QString::number(GetLastError()));
  CloseHandle(process);
}
#endif
}  // namespace

void Process::killAllAtExit() {
  for (Process* process : liveProcesses()) {
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(1000);
    }
  }
}

Process::Process(const QString& name) : m_name(name) {
  // The set first: it is then destroyed only after the atexit handler ran.
  liveProcesses().insert(this);
  static const bool registered = std::atexit(&Process::killAllAtExit) == 0;
  Q_UNUSED(registered);
#ifdef _WIN32
  connect(this, &QProcess::started, this,
          [this]() { killWithThisProcess(processId()); });
#endif

  if (m_forwardProcessLog) {
    connect(this, &QProcess::readyReadStandardOutput, this, [this]() {
      QByteArray output = readAllStandardOutput();
      QList<QByteArray> lines = output.split('\n');
      for (const auto& line : lines) {
        SimpleLogger::instance().log(QString("%1 proc:").arg(m_name), line);
      }
    });
  };

  connect(this, &QProcess::readyReadStandardError, this, [this]() {
    QString error{QString::fromUtf8(readAllStandardError())};
    SimpleLogger::instance().error(QString("%1 proc:").arg(m_name), error);
    bool skipUIReportingError = false;
    for (const QString& bypassError : m_bypassInnerErrors) {
      if (error.contains(bypassError)) {
        skipUIReportingError = true;
        break;
      }
    }
    if (!skipUIReportingError && !m_seenErrors.contains(error)) {
      m_seenErrors.insert(error);
      emit innerErrorOccurred(error);
    }
  });

  m_watcherTimer.setInterval(PROCESS_WATCHER_INTERVAL_MS);
  QObject::connect(&m_watcherTimer, &QTimer::timeout, this,
                   &Process::checkEvent);
  m_watcherTimer.start();

  m_prevState = state();
}

Process::~Process() {
  liveProcesses().remove(this);
  stopAndWaitProcess();
}

void Process::stopAndWaitProcess() {
  m_watcherTimer.stop();
  terminate();
  if (!waitForFinished(PROCESS_FINISH_TIMOUT_MS)) {
    kill();
    waitForFinished();
  }
}

void Process::start(const QString& fullCmd) {
  QList<QString> fragments = fullCmd.split(" ");
  if (!fragments.isEmpty()) {
    m_cmd = fragments[0];

    fragments.pop_front();
    m_args = fragments;

    restart();
  }
}

void Process::stop() {
  stopAndWaitProcess();
  m_isFirstRun = true;
  m_prevState = QProcess::ProcessState::NotRunning;
  emit runStatusChanged(false);
}

void Process::restart() {
  setProgram(m_cmd);
  setArguments(m_args);
  SimpleLogger::instance().log("--- running", m_cmd, m_args.join(" "));
  QProcess::start();

  if (m_isFirstRun) {
    m_isFirstRun = false;
  } else {
    emit restarted();
  }

  if (!m_watcherTimer.isActive()) {
    m_watcherTimer.start();
  }
}

void Process::checkEvent() {
  QProcess::ProcessState curState = state();
  if (m_prevState != curState) {
#ifdef USE_VPR_VIEWER_AUTO_RECOVERY
    if (curState == QProcess::NotRunning) {
      SimpleLogger::instance().log("vpr is not running, restarting");
      restart();
    }
#endif
    emit runStatusChanged(curState != QProcess::NotRunning);
    m_prevState = curState;
  }
}

}  // namespace FOEDAG
