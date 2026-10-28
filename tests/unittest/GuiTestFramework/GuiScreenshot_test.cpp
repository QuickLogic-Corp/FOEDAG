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
#include "GuiTestFramework/GuiScreenshot.h"

extern "C" {
#include <tcl.h>
}

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <sstream>

#include "GuiTestFramework/GuiTestCommands.h"
#include "GuiTestFramework/GuiTestRunner.h"
#include "gtest/gtest.h"

using namespace FOEDAG;
using namespace FOEDAG::GuiScreenshot;

namespace {

QImage render(const QString &text) {
  QLabel label{text};
  label.setFixedSize(120, 30);
  label.show();
  return capture(&label, {}, {});
}

// Paints a different colour on every timer tick while it runs.
class Flicker : public QWidget {
 public:
  explicit Flicker(int stopAfterMs) {
    setFixedSize(40, 40);
    connect(&m_timer, &QTimer::timeout, this, [this, stopAfterMs]() {
      if (stopAfterMs >= 0 && m_clock.elapsed() >= stopAfterMs) {
        m_timer.stop();
        return;
      }
      ++m_frame;
      update();
    });
    m_clock.start();
    m_timer.start(20);
  }

 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter{this}.fillRect(rect(), QColor::fromHsv((m_frame * 37) % 360,
                                                    255, 255));
  }

 private:
  QTimer m_timer;
  QElapsedTimer m_clock;
  int m_frame{0};
};

}  // namespace

// U-004: the comparator fails every unmasked alteration (REQ-019).
TEST(GuiScreenshot, ComparatorSelfTest) {
  const QImage reference = render("Gui 123");
  ASSERT_FALSE(reference.isNull());
  EXPECT_EQ(countDifferences(reference, reference, 0), 0);

  QImage recolour = reference.copy();
  QPainter{&recolour}.fillRect(QRect{4, 4, 5, 5}, Qt::red);
  EXPECT_GT(countDifferences(reference, recolour, 0), 0);

  QImage shifted{reference.size(), reference.format()};
  shifted.fill(Qt::white);
  QPainter{&shifted}.drawImage(1, 0, reference);
  EXPECT_GT(countDifferences(reference, shifted, 0), 0);

  QImage erased = reference.copy();
  QPainter{&erased}.fillRect(QRect{2, 2, 10, 10}, Qt::white);
  EXPECT_GT(countDifferences(reference, erased, 0), 0);

  const QImage glyph = render("Gui 128");
  EXPECT_GT(countDifferences(reference, glyph, 0), 0);

  // The same glyph change, under a mask covering the digits, passes.
  QLabel a{"Gui 123"}, b{"Gui 128"};
  for (QLabel *l : {&a, &b}) {
    l->setFixedSize(120, 30);
    l->show();
  }
  const QList<QRect> mask{QRect{20, 0, 100, 30}};
  EXPECT_EQ(countDifferences(capture(&a, {}, mask), capture(&b, {}, mask), 0),
            0);
}

// U-005: tolerance and the differing-pixel budget.
TEST(GuiScreenshot, Tolerance) {
  const QImage reference = render("Gui 123");
  QImage nudged = reference.copy();
  for (int x = 0; x < 5; ++x) {
    const QRgb p = nudged.pixel(x, 0);
    nudged.setPixel(x, 0, qRgb(std::min(qRed(p) + 2, 255),
                               std::max(qGreen(p) - 2, 0), qBlue(p)));
  }
  const int strict = countDifferences(reference, nudged, 0);
  EXPECT_EQ(strict, 5);
  EXPECT_GT(strict, 0) << "fails at the 0/0 default";
  EXPECT_LE(countDifferences(reference, nudged, 2), 5)
      << "passes with tol=2, max_diff_pixels=5";
  EXPECT_EQ(countDifferences(reference, reference.copy(0, 0, 10, 10), 0), -1)
      << "a size difference is reported apart";
}

// U-017: settle waits for the widget to stop changing (REQ-031).
TEST(GuiScreenshot, Settle) {
  Flicker settles{300};
  settles.show();
  QElapsedTimer timer;
  timer.start();
  QImage image;
  EXPECT_TRUE(settle(&settles, {}, {}, kDefaultSettleMs, &image));
  EXPECT_GE(timer.elapsed(), 300);
  EXPECT_EQ(countDifferences(image, capture(&settles, {}, {}), 0), 0);

  Flicker never{-1};
  never.show();
  EXPECT_FALSE(settle(&never, {}, {}, 500, &image)) << "reported as unstable";
}

// U-020: display mode reports a mismatch without failing; gen-goldens mode is
// refused.
TEST(GuiScreenshot, DisplayModeReportsOnly) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QLabel label{"Gui 123"};
  label.setObjectName("u020Label");
  label.setFixedSize(120, 30);
  label.show();
  QImage golden = capture(&label, {}, {});
  golden.setPixel(0, 0, golden.pixel(0, 0) ^ 0x00ffffff);
  const QString goldenPath =
      dir.path() + "/golden/" + platformDir() + "/u020/step.png";
  QDir().mkpath(QFileInfo(goldenPath).absolutePath());
  ASSERT_TRUE(golden.save(goldenPath));

  qputenv("GUI_TEST_FRAMEWORK_GOLDEN_DIR", (dir.path() + "/golden").toUtf8());
  qputenv("GUI_TEST_FRAMEWORK_ARTIFACT_DIR", (dir.path() + "/out").toUtf8());
  qputenv("GUI_TEST_FRAMEWORK_SHOW", "1");
  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  std::ostringstream out;
  std::streambuf *old = std::cout.rdbuf(out.rdbuf());
  const int code =
      Tcl_Eval(interp, "gui_screenshot u020/step -widget u020Label");
  std::cout.rdbuf(old);
  EXPECT_EQ(code, TCL_OK) << Tcl_GetStringResult(interp);
  EXPECT_NE(out.str().find("MISMATCH u020/step (1 px)"), std::string::npos)
      << out.str();
  EXPECT_TRUE(QFile::exists(dir.path() + "/out/u020/step.diff.png"));

  qunsetenv("GUI_TEST_FRAMEWORK_SHOW");
  EXPECT_EQ(Tcl_Eval(interp, "gui_screenshot u020/step -widget u020Label"),
            TCL_ERROR)
      << "outside display mode the same mismatch fails";

  qputenv("GUI_TEST_FRAMEWORK_SHOW", "1");
  qputenv("GUI_TEST_FRAMEWORK_GOLDEN", "update");
  EXPECT_FALSE(GuiTestRunner::startupError().isEmpty());
  qunsetenv("GUI_TEST_FRAMEWORK_SHOW");
  EXPECT_TRUE(GuiTestRunner::startupError().isEmpty());

  qunsetenv("GUI_TEST_FRAMEWORK_GOLDEN");
  qunsetenv("GUI_TEST_FRAMEWORK_GOLDEN_DIR");
  qunsetenv("GUI_TEST_FRAMEWORK_ARTIFACT_DIR");
  Tcl_DeleteInterp(interp);
}

// U-021: a golden written twice has the same bytes, with no time or text
// chunk (REQ-041).
TEST(GuiScreenshot, RepeatableBytes) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QLabel label{"Gui 123"};
  label.setObjectName("u021Label");
  label.setFixedSize(120, 30);
  label.show();
  qputenv("GUI_TEST_FRAMEWORK_GOLDEN_DIR", (dir.path() + "/golden").toUtf8());
  qputenv("GUI_TEST_FRAMEWORK_ARTIFACT_DIR", (dir.path() + "/out").toUtf8());
  qputenv("GUI_TEST_FRAMEWORK_GOLDEN", "update");
  Tcl_Interp *interp = Tcl_CreateInterp();
  registerGuiTestCommands(interp);
  const QString golden =
      dir.path() + "/golden/" + platformDir() + "/u021/step.png";
  QList<QByteArray> writes;
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(Tcl_Eval(interp, "gui_screenshot u021/step -widget u021Label"),
              TCL_OK)
        << Tcl_GetStringResult(interp);
    QFile file{golden};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    writes << file.readAll();
  }
  EXPECT_EQ(writes[0], writes[1]);
  for (const char *chunk : {"tIME", "tEXt", "zTXt", "iTXt"})
    EXPECT_FALSE(writes[0].contains(chunk)) << chunk;

  qunsetenv("GUI_TEST_FRAMEWORK_GOLDEN");
  qunsetenv("GUI_TEST_FRAMEWORK_GOLDEN_DIR");
  qunsetenv("GUI_TEST_FRAMEWORK_ARTIFACT_DIR");
  Tcl_DeleteInterp(interp);
}
