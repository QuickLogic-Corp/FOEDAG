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
#include "GuiTestRunner.h"

extern "C" {
#include <tcl.h>
}

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "GuiRecorder.h"

namespace FOEDAG {

GuiTestSpaceGate::GuiTestSpaceGate(int timeoutMs, QObject *parent)
    : QObject(parent), m_timeoutMs(timeoutMs) {
  qApp->installEventFilter(this);
}

void GuiTestSpaceGate::arm() {
  m_armed = true;
  m_open = false;
  m_armedFor.start();
}

bool GuiTestSpaceGate::isOpen() const {
  return m_open ||
         (m_armed && m_timeoutMs >= 0 && m_armedFor.elapsed() >= m_timeoutMs);
}

bool GuiTestSpaceGate::eventFilter(QObject *watched, QEvent *event) {
  const QEvent::Type type = event->type();
  if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
    return QObject::eventFilter(watched, event);
  auto key = static_cast<QKeyEvent *>(event);
  if (key->key() != Qt::Key_Space || key->modifiers() != Qt::NoModifier)
    return QObject::eventFilter(watched, event);
  if (type == QEvent::KeyPress && m_armed) {
    if (!key->isAutoRepeat()) {
      m_armed = false;
      m_open = true;
      m_swallowRelease = true;
    }
    return true;
  }
  // The release of the Space that opened the gate would otherwise click a
  // button that has focus.
  if (type == QEvent::KeyRelease && m_swallowRelease) {
    if (!key->isAutoRepeat()) m_swallowRelease = false;
    return true;
  }
  return QObject::eventFilter(watched, event);
}

GuiTestRunner *GuiTestRunner::instance() {
  static GuiTestRunner *runner = new GuiTestRunner;
  return runner;
}

bool GuiTestRunner::isSyncScript(const std::string &file) {
  std::ifstream in{file};
  std::string first;
  std::getline(in, first);
  if (!first.empty() && first.back() == '\r') first.pop_back();
  return first == kSyncHeader;
}

GuiTestRunner::StrictWait::StrictWait() {
  auto &running = GuiTestRunner::instance()->m_running;
  if (!running.isEmpty()) ++running.last().strictWaits;
}

GuiTestRunner::StrictWait::~StrictWait() {
  auto &running = GuiTestRunner::instance()->m_running;
  if (!running.isEmpty()) --running.last().strictWaits;
}

void GuiTestRunner::run(Tcl_Interp *interp, const QString &file) {
  m_interp = interp;
  m_file = file;

  QFile f{file};
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    fail(QString("cannot open replay script %1").arg(file));
  const QStringList lines = QTextStream{&f}.readAll().split('\n');

  QString pending;
  int pendingLine = 0;
  for (int i = 0; i < lines.size(); ++i) {
    const QString &line = lines.at(i);
    if (pending.isEmpty()) {
      const QString trimmed = line.trimmed();
      if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
      pendingLine = i + 1;
      pending = line;
    } else {
      pending += '\n' + line;
    }
    if (Tcl_CommandComplete(pending.toUtf8().constData())) {
      m_commands.append({pendingLine, pending});
      pending.clear();
    }
  }
  if (!pending.isEmpty())
    fail(QString("%1:%2: incomplete command").arg(file).arg(pendingLine));

  // REQ-027: recordings refer to these instead of absolute paths.
  Tcl_Eval(m_interp, "namespace eval ::gui_test {}");
  Tcl_SetVar(m_interp, "::gui_test::project_dir",
             GuiRecorder::projectDir().toUtf8().constData(), TCL_GLOBAL_ONLY);
  Tcl_SetVar(m_interp, "::gui_test::install_dir",
             GuiRecorder::installDir().toUtf8().constData(), TCL_GLOBAL_ONLY);

  qApp->installEventFilter(this);
  if (qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_INTERACTIVE") == 1) {
    bool ok{false};
    int timeoutMs = qEnvironmentVariableIntValue(
        "GUI_TEST_FRAMEWORK_INTERACTIVE_TIMEOUT", &ok);
    if (!ok || timeoutMs < 0) timeoutMs = -1;
    m_gate = new GuiTestSpaceGate(timeoutMs, this);
    std::cout << "Interactive mode: press Space in an aurora window to run "
                 "each line"
              << (timeoutMs >= 0 ? ", or wait " + std::to_string(timeoutMs) +
                                      " ms"
                                : std::string{})
              << "." << std::endl;
  }
  scheduleStep(0);

  QEventLoop loop;
  loop.exec();
  finish(0);
}

bool GuiTestRunner::canRunNext() const {
  if (m_running.isEmpty()) return true;
  // Inside a nested loop: only while the innermost line's modal dialog is
  // open. Once it closes, that line must return first.
  const QWidget *modal = m_running.last().modal;
  return modal && modal->isVisible();
}

// Interactive mode: announces the next line once, then holds it until Space.
// Before any window is shown there is nowhere to press Space, so no hold.
bool GuiTestRunner::holdForSpace() {
  if (!m_gate) return false;
  if (m_announced != m_next) {
    m_announced = m_next;
    const Command &command = m_commands.at(m_next);
    std::cout << "NEXT " << command.line << ": " << command.text.toStdString()
              << std::endl;
    m_gate->arm();
  }
  bool windowShown = false;
  for (QWidget *w : QApplication::topLevelWidgets())
    windowShown = windowShown || (w->isVisible() && w->windowType() != Qt::Popup &&
                                  w->windowType() != Qt::ToolTip);
  return windowShown && !m_gate->isOpen();
}

void GuiTestRunner::scheduleStep(int delayMs) {
  // A fresh single-shot timer each time: Qt does not fire a timer again while
  // its own callback runs, and a line runs inside this callback.
  QTimer::singleShot(delayMs, this, &GuiTestRunner::step);
}

void GuiTestRunner::step() {
  if (!canRunNext()) {
    scheduleStep(kBlockedPollMs);
    return;
  }
  if (m_next >= m_commands.size()) {
    if (m_running.isEmpty()) finish(0);
    const QWidget *modal = m_running.last().modal;
    fail(QString("script ended while dialog '%1' (%2) is open")
             .arg(modal->windowTitle(), modal->objectName()));
  }
  if (holdForSpace()) {
    scheduleStep(kHeldPollMs);
    return;
  }
  const int index = m_next++;
  m_running.append(Frame{index, {}});
  scheduleStep(0);
  const QByteArray script = m_commands.at(index).text.toUtf8();
  const int code =
      Tcl_EvalEx(m_interp, script.constData(), script.size(), TCL_EVAL_GLOBAL);
  if (code != TCL_OK) fail(Tcl_GetStringResult(m_interp));
  m_running.removeLast();
}

bool GuiTestRunner::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::Show && !m_running.isEmpty() &&
      !m_running.last().modal && m_running.last().strictWaits == 0) {
    auto w = qobject_cast<QWidget *>(watched);
    if (w && w->isWindow() && w->isModal()) m_running.last().modal = w;
  }
  return QObject::eventFilter(watched, event);
}

void GuiTestRunner::fail(const QString &message) {
  std::cout << "FAIL: " << message.toStdString() << std::endl;
  if (!m_running.isEmpty()) {
    const Command &command = m_commands.at(m_running.last().index);
    std::cout << "  at " << m_file.toStdString() << ":" << command.line
              << ": " << command.text.toStdString() << std::endl;
  }
  finish(1);
}

void GuiTestRunner::finish(int code) {
  if (GuiRecorder::isActive()) GuiRecorder::instance()->flush();
  if (code == 0) std::cout << "PASS: " << m_file.toStdString() << std::endl;
  for (int type : {TCL_STDOUT, TCL_STDERR})
    if (Tcl_Channel channel = Tcl_GetStdChannel(type)) Tcl_Flush(channel);
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);
  // Exit-time teardown crashes in ~MainWindow (Qt accessibility), which would
  // turn every result into a signal; the run's result is all that matters.
  std::_Exit(code);
}

}  // namespace FOEDAG
