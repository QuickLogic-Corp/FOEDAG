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

class QWidget;

// Rendering settings that make screenshots repeatable in replay mode
// (REQ-014). The application's style stays with the caller, which wraps
// Fusion when isActive().
namespace FOEDAG::GuiTestMode {

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 1000;

// Before the QApplication exists: offscreen platform (unless display mode),
// scale factor and font DPI.
void prepareEnvironment(bool replay);
// Right after the QApplication exists: no cursor blink, no animations.
void applyToApplication();

bool isActive();
// GUI_TEST_FRAMEWORK_SHOW or GUI_TEST_FRAMEWORK_INTERACTIVE (REQ-036).
bool isDisplayMode();
// GUI_TEST_FRAMEWORK_GOLDEN=update.
bool isBlessMode();

void fixWindowSize(QWidget *mainWindow);

}  // namespace FOEDAG::GuiTestMode
