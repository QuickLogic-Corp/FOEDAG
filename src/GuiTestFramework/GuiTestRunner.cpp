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
#include <QMenu>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "GuiRecorder.h"
#include "GuiTestMode.h"
#include "GuiTestScriptView.h"

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

bool GuiTestSpaceGate::arrive(bool breakpoint) {
  if (breakpoint) m_stepping = true;
  if (m_stepping) {
    arm();
  } else {
    m_armed = false;
    m_open = true;
  }
  return m_stepping;
}

void GuiTestSpaceGate::run() {
  m_stepping = false;
  m_armed = false;
  m_open = true;
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

QString GuiTestRunner::startupError() {
  if (GuiTestMode::isGenGoldensMode() && GuiTestMode::isDisplayMode())
    return "gen-goldens mode writes goldens only from offscreen runs; unset "
           "GUI_TEST_FRAMEWORK_SHOW and GUI_TEST_FRAMEWORK_INTERACTIVE";
  return {};
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
  if (const QString error = startupError(); !error.isEmpty()) fail(error);

  QFile f{file};
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    fail(QString("cannot open replay script %1").arg(file));
  const QString text = QTextStream{&f}.readAll();
  const QStringList lines = text.split('\n');

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
  const bool interactive =
      qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_INTERACTIVE") == 1;
  if (interactive || GuiTestMode::isDebugMode()) {
    bool ok{false};
    int timeoutMs = qEnvironmentVariableIntValue(
        "GUI_TEST_FRAMEWORK_INTERACTIVE_TIMEOUT", &ok);
    if (!interactive || !ok || timeoutMs < 0) timeoutMs = -1;
    m_gate = new GuiTestSpaceGate(timeoutMs, this);
    m_gate->setStepping(interactive);
    if (interactive)
      std::cout << "Interactive mode: press Space in an application window to "
                   "run each line"
                << (timeoutMs >= 0 ? ", or wait " +
                                         std::to_string(timeoutMs) + " ms"
                                   : std::string{})
                << "." << std::endl;
  }
  if (GuiTestMode::isDebugMode()) {
    QVector<GuiTestScriptView::Span> spans;
    for (const Command &c : m_commands)
      spans.append({c.line, int(c.text.count('\n')) + 1});
    m_view = new GuiTestScriptView(file, text, spans);
    connect(m_view, &GuiTestScriptView::runRequested, m_gate,
            &GuiTestSpaceGate::run);
    m_view->show();
    showProgress();
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
    const bool breakpoint = m_view && m_view->hasBreakpoint(m_next);
    if (m_gate->arrive(breakpoint))
      std::cout << (breakpoint ? "BREAK " : "NEXT ") << command.line << ": "
                << command.text.toStdString() << std::endl;
  }
  bool windowShown = false;
  for (QWidget *w : QApplication::topLevelWidgets())
    windowShown = windowShown || (w->isVisible() && w->windowType() != Qt::Popup &&
                                  w->windowType() != Qt::ToolTip);
  const bool held = windowShown && !m_gate->isOpen();
  if (m_view) m_view->setPaused(held);
  return held;
}

void GuiTestRunner::scheduleStep(int delayMs) {
  // A fresh single-shot timer each time: Qt does not fire a timer again while
  // its own callback runs, and a line runs inside this callback.
  QTimer::singleShot(delayMs, this, &GuiTestRunner::step);
}

void GuiTestRunner::step() {
  if (m_finished) return;  // the result is on screen in debug mode
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
  showProgress();
  scheduleStep(0);
  const QByteArray script = m_commands.at(index).text.toUtf8();
  const int code =
      Tcl_EvalEx(m_interp, script.constData(), script.size(), TCL_EVAL_GLOBAL);
  if (code != TCL_OK) fail(Tcl_GetStringResult(m_interp));
  m_running.removeLast();
  showProgress();
}

void GuiTestRunner::screenshotTaken(const QString &path) {
  if (m_view && !m_running.isEmpty())
    m_view->addScreenshot(m_commands.at(m_running.last().index).line, path);
}

void GuiTestRunner::showProgress() {
  if (!m_view) return;
  QVector<int> running;
  for (const Frame &f : m_running) running.append(f.index);
  m_view->showProgress(running, m_next);
}

bool GuiTestRunner::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::Show && !m_running.isEmpty() &&
      !m_running.last().modal && m_running.last().strictWaits == 0) {
    auto w = qobject_cast<QWidget *>(watched);
    // A context menu waits for a pick the way a modal dialog waits for an
    // answer.
    if (w && w->isWindow() && (w->isModal() || qobject_cast<QMenu *>(w)))
      m_running.last().modal = w;
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
  if (m_view)
    m_view->showResult(m_running.isEmpty() ? -1 : m_running.last().index,
                       message);
  finish(1);
}

void GuiTestRunner::warn(const QString &message) {
  // Strict mode lists every known issue still open in one run.
  const bool strict =
      qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_STRICT_WARN") == 1;
  if (strict) m_warnFailed = true;
  std::cout << (strict ? "FAIL: " : "WARN: ") << message.toStdString()
            << std::endl;
  if (!m_running.isEmpty()) {
    const Command &command = m_commands.at(m_running.last().index);
    std::cout << "  at " << m_file.toStdString() << ":" << command.line
              << ": " << command.text.toStdString() << std::endl;
  }
}

void GuiTestRunner::finish(int code) {
  if (GuiRecorder::isActive()) GuiRecorder::instance()->flush();
  if (code == 0 && m_warnFailed) code = 1;
  if (code == 0) std::cout << "PASS: " << m_file.toStdString() << std::endl;
  for (int type : {TCL_STDOUT, TCL_STDERR})
    if (Tcl_Channel channel = Tcl_GetStdChannel(type)) Tcl_Flush(channel);
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);
  // Debug mode keeps the application and the result on screen until the
  // replay window is closed; no further line runs meanwhile.
  m_finished = true;
  if (m_view && m_view->isVisible()) {
    if (code == 0) m_view->showResult(-1, {});
    QEventLoop wait;
    connect(m_view, &GuiTestScriptView::closed, &wait, &QEventLoop::quit);
    wait.exec();
  }
  // Exit-time teardown crashes in ~MainWindow (Qt accessibility), which would
  // turn every result into a signal; the run's result is all that matters.
  std::_Exit(code);
}

}  // namespace FOEDAG
