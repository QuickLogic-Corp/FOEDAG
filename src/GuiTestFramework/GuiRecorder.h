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

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>

class QAbstractButton;
class QAction;
class QIODevice;
class QWidget;

namespace FOEDAG {

// Writes the user's GUI actions as a "# gui-test: sync" replay script
// (aurora --record). The rules are in aurora2
// docs/specs/gui-test-automation/design-install.md, §2 stage 2.
class GuiRecorder : public QObject {
  Q_OBJECT

 public:
  static GuiRecorder *instance();
  static bool isActive();

  bool start(const QString &file, QString *error);
  void start(QIODevice *out);  // not owned; for tests
  void stop();
  // Closes every open frame and writes what is pending.
  void flush();

  // One gui_* action: everything it causes is recorded as one line at most.
  class Frame {
   public:
    explicit Frame(QObject *target);
    ~Frame();
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;

   private:
    int m_id{0};
  };

  // A button is about to be clicked. Recorded before its handlers run, since
  // a handler may delete the button before clicked is emitted.
  void aboutToClick(QAbstractButton *button);

  // gui_assert lines for the widget's current value (REQ-026).
  void checkpoint(QWidget *widget);

  // The values of $::gui_test::project_dir and $::gui_test::install_dir.
  static QString projectDir();
  static QString installDir();
  void setDirsForTest(const QString &project, const QString &install);
  void setClockForTest(std::function<qint64()> msSinceStart);

 protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

 private slots:
  void onConsoleCommand(const QString &command);

 private:
  struct Offer {
    QStringList lines;  // notes, then the action; empty when unrecorded
    QString unrecorded;
    QPointer<QObject> source;
    bool action{false};
  };
  struct OpenFrame {
    int id;
    int entry;
    QPointer<QObject> target;
    int level;
    bool input;
    const QEvent *event;
    int eventType;
    QList<Offer> offers;
  };
  struct Entry {
    bool closed{false};
    bool action{false};
    qint64 openedAt{0};
    quint64 seq{0};  // last milestone seen when the frame opened
    QStringList lines;
  };
  struct Milestone {
    quint64 seq;
    qint64 at;
    QString name;
    QString arg;
  };

  GuiRecorder() = default;
  void begin();
  int openFrame(QObject *target, bool input, const QEvent *event);
  void closeFrame(int id);
  void closeInputFrames(int level);
  void offer(QObject *source, const QStringList &lines, bool action,
             const QString &unrecordedText = {});
  void watch(QWidget *widget);
  void watch(QAction *action);
  void onButton(QAbstractButton *button);
  void onAction(QAction *action);
  QString pathWord(const QString &text, QStringList *notes) const;
  void writeReady();
  void writeLine(const QString &line);
  qint64 now() const;

  QIODevice *m_out{nullptr};
  std::unique_ptr<QFile> m_file;
  QList<OpenFrame> m_frames;  // innermost last
  QList<Entry> m_entries;
  int m_nextToWrite{0};
  int m_nextFrameId{1};
  QList<Milestone> m_milestones;
  quint64 m_seq{0};
  quint64 m_writtenSeq{0};
  qint64 m_lastActionAt{0};
  QString m_projectDir;
  QString m_installDir;
  std::function<qint64()> m_clock;
  QElapsedTimer m_started;
};

// Tcl list quoting, so recorded lines are canonical.
QString tclWord(const QString &word);

}  // namespace FOEDAG
