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
#include "GuiTestMode.h"

#include <QApplication>
#include <QWidget>

#include "tcl.h"

#ifdef __linux__
#include <execinfo.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#endif

namespace FOEDAG::GuiTestMode {

namespace {
bool active{false};

#ifdef __linux__
// Temporary diagnostic for the Tcl panic on the RHEL golden node (#2681):
// prints the panic, the stacks and this thread's std channels, then aborts.
[[noreturn]] void panicWithBacktrace(const char *format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fprintf(stderr, "\n[gui-test] Tcl panic in thread %ld\n", static_cast<long>(syscall(SYS_gettid)));
  void *frames[64];
  backtrace_symbols_fd(frames, backtrace(frames, 64), STDERR_FILENO);
  std::fflush(stderr);
  char pid[16];
  std::snprintf(pid, sizeof pid, "%d", getpid());
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
  if (pid_t child = fork(); child == 0) {
    dup2(STDERR_FILENO, STDOUT_FILENO);
    execlp("gdb", "gdb", "-p", pid, "-batch", "-ex", "thread apply all bt", nullptr);
    _exit(127);
  } else if (child > 0) {
    waitpid(child, nullptr, 0);
  }
  // Last: Tcl may be the broken part.
  for (int type : {TCL_STDIN, TCL_STDOUT, TCL_STDERR}) {
    Tcl_Channel channel = Tcl_GetStdChannel(type);
    std::fprintf(stderr, "[gui-test] std channel %d: %p %s\n", type, static_cast<void *>(channel),
                 channel ? Tcl_GetChannelName(channel) : "-");
  }
  std::abort();
}
#endif
}  // namespace

void prepareEnvironment(bool replay) {
  active = replay;
  if (!active) return;
  // Overrides the caller and the installed launcher, which picks xcb when
  // DISPLAY is set, so local runs render as CI does.
  if (!isDisplayMode()) qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_SCALE_FACTOR", "1");
  qputenv("QT_FONT_DPI", "96");
}

void applyToApplication() {
  if (!active) return;
#ifdef __linux__
  Tcl_SetPanicProc(panicWithBacktrace);
#endif
  QApplication::setCursorFlashTime(0);
  for (Qt::UIEffect effect :
       {Qt::UI_AnimateCombo, Qt::UI_AnimateMenu, Qt::UI_AnimateTooltip,
        Qt::UI_FadeMenu, Qt::UI_FadeTooltip})
    QApplication::setEffectEnabled(effect, false);
}

bool isActive() { return active; }

bool isDebugMode() {
  return qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_DEBUG") == 1;
}

bool isDisplayMode() {
  return qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_SHOW") == 1 ||
         qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_INTERACTIVE") == 1 ||
         isDebugMode();
}

bool isGenGoldensMode() {
  return qgetenv("GUI_TEST_FRAMEWORK_GOLDEN") == "update";
}

void fixWindowSize(QWidget *mainWindow) {
  if (!active || !mainWindow) return;
  // The main window maximizes itself, and the offscreen screen is small.
  mainWindow->setWindowState(Qt::WindowNoState);
  mainWindow->resize(kWindowWidth, kWindowHeight);
}

}  // namespace FOEDAG::GuiTestMode
