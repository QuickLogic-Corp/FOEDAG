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

namespace FOEDAG::GuiTestMode {

namespace {
bool active{false};
}

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
  QApplication::setCursorFlashTime(0);
  for (Qt::UIEffect effect :
       {Qt::UI_AnimateCombo, Qt::UI_AnimateMenu, Qt::UI_AnimateTooltip,
        Qt::UI_FadeMenu, Qt::UI_FadeTooltip})
    QApplication::setEffectEnabled(effect, false);
}

bool isActive() { return active; }

bool isDisplayMode() {
  return qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_SHOW") == 1 ||
         qEnvironmentVariableIntValue("GUI_TEST_FRAMEWORK_INTERACTIVE") == 1;
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
