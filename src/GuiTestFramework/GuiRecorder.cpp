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
#include "GuiRecorder.h"

extern "C" {
#include <tcl.h>
}

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionEvent>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <iostream>

#include "GuiTestCommands.h"
#include "GuiTestHub.h"
#include "GuiTestRunner.h"

namespace FOEDAG {

namespace {

constexpr int kMinWaitMs = 10000;
constexpr const char *kWatchedProperty = "_guiRecorderWatched";

int loopLevel() { return QThread::currentThread()->loopLevel(); }

QString plain(QString text) { return text.remove('&'); }

bool isAncestor(const QObject *ancestor, const QObject *object) {
  for (const QObject *o = object; o; o = o->parent())
    if (o == ancestor) return true;
  return false;
}

// Whether a recorded signal belongs to the widget the input event went to.
bool relatedTo(const QObject *source, const QObject *target) {
  if (!source || !target) return false;
  if (auto action = qobject_cast<const QAction *>(source)) {
    if (action == target) return true;
    for (QObject *o : action->associatedObjects())
      if (isAncestor(o, target)) return true;
    return false;
  }
  return isAncestor(source, target) || isAncestor(target, source);
}

QString unrecordedText(const QObject *object, const QString &text) {
  QStringList chain;
  for (const QObject *o = object->parent(); o; o = o->parent()) {
    QString part = o->metaObject()->className();
    if (!o->objectName().isEmpty()) part += "(" + o->objectName() + ")";
    chain << part;
  }
  return QString("%1 %2 < %3")
      .arg(object->metaObject()->className(), tclWord(text), chain.join(" < "));
}

// The widget whose name a recorded line uses: an editor inside a combo or a
// spin box is reached through its owner.
QWidget *namedOwner(QWidget *widget) {
  QWidget *parent = widget->parentWidget();
  if (qobject_cast<QLineEdit *>(widget) &&
      (qobject_cast<QComboBox *>(parent) ||
       qobject_cast<QAbstractSpinBox *>(parent)))
    return parent;
  return widget;
}

// The gui_dialog line that answers a modal dialog with this button, if any.
QString dialogAnswer(QAbstractButton *button) {
  QWidget *window = button->window();
  if (!window || !window->isModal()) return {};
  int role = QDialogButtonBox::InvalidRole;
  if (auto box = qobject_cast<QMessageBox *>(window))
    role = box->buttonRole(button);
  else if (auto box = qobject_cast<QDialogButtonBox *>(button->parentWidget()))
    role = box->buttonRole(button);
  if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole)
    return "gui_dialog accept";
  if (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole)
    return "gui_dialog reject";
  if (plain(button->text()).isEmpty()) return {};
  return "gui_dialog button " + tclWord(plain(button->text()));
}

QString escapeInQuotes(QString text) {
  for (const char c : {'\\', '"', '$', '[', ']'})
    text.replace(QChar(c), QString("\\") + QChar(c));
  return text;
}

}  // namespace

QString tclWord(const QString &word) {
  const QByteArray utf8 = word.toUtf8();
  const char *argv[] = {utf8.constData()};
  char *merged = Tcl_Merge(1, argv);
  const QString result = QString::fromUtf8(merged);
  Tcl_Free(merged);
  return result;
}

GuiRecorder *GuiRecorder::instance() {
  static GuiRecorder *recorder = new GuiRecorder;
  return recorder;
}

bool GuiRecorder::isActive() { return instance()->m_out != nullptr; }

QString GuiRecorder::projectDir() {
  static const QString dir = QDir::cleanPath(QDir::currentPath());
  return dir;
}

QString GuiRecorder::installDir() {
  return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/..");
}

void GuiRecorder::setDirsForTest(const QString &project,
                                 const QString &install) {
  m_projectDir = QDir::cleanPath(project);
  m_installDir = QDir::cleanPath(install);
}

void GuiRecorder::setClockForTest(std::function<qint64()> msSinceStart) {
  m_clock = std::move(msSinceStart);
}

qint64 GuiRecorder::now() const {
  return m_clock ? m_clock() : m_started.elapsed();
}

bool GuiRecorder::start(const QString &file, QString *error) {
  m_file = std::make_unique<QFile>(file);
  if (!m_file->open(QIODevice::WriteOnly | QIODevice::Text |
                    QIODevice::Truncate)) {
    *error = QString("cannot write the recording %1: %2")
                 .arg(file, m_file->errorString());
    m_file.reset();
    return false;
  }
  m_out = m_file.get();
  m_scenario = QFileInfo(file).completeBaseName();
  begin();
  return true;
}

void GuiRecorder::start(QIODevice *out) {
  m_out = out;
  begin();
}

void GuiRecorder::begin() {
  if (m_projectDir.isEmpty()) m_projectDir = projectDir();
  if (m_installDir.isEmpty()) m_installDir = installDir();
  m_started.start();
  qApp->installEventFilter(this);
  connect(GuiTestHub::instance(), &GuiTestHub::counted, this,
          [this](const QString &name, const QString &arg) {
            if (GuiTestHub::kindOf(name) != GuiTestHub::Kind::Sync) return;
            m_milestones.append({++m_seq, now(), name, arg});
          });
  connect(qApp, &QGuiApplication::lastWindowClosed, this, &GuiRecorder::flush);
  // A recorded session ends with Tcl's exit, which skips Qt's own signals.
  static const bool exitHandler = [] {
    Tcl_CreateExitHandler(
        [](ClientData) {
          if (isActive()) GuiRecorder::instance()->flush();
        },
        nullptr);
    return true;
  }();
  Q_UNUSED(exitHandler);
  for (QWidget *w : QApplication::allWidgets()) watch(w);
  writeLine(GuiTestRunner::kSyncHeader);
  writeLine("gui_start");
}

void GuiRecorder::stop() {
  flush();
  qApp->removeEventFilter(this);
  disconnect(GuiTestHub::instance(), nullptr, this, nullptr);
  disconnect(qApp, nullptr, this, nullptr);
  for (QWidget *w : QApplication::allWidgets()) {
    w->setProperty(kWatchedProperty, {});
    for (QAction *a : w->actions()) a->setProperty(kWatchedProperty, {});
  }
  m_out = nullptr;
  m_file.reset();
  m_frames.clear();
  m_entries.clear();
  m_milestones.clear();
  m_scenario = "recording";
  m_checkpoints = 0;
  m_nextToWrite = 0;
  m_seq = m_writtenSeq = 0;
  m_lastActionAt = 0;
  m_projectDir.clear();
  m_installDir.clear();
  m_clock = nullptr;
}

void GuiRecorder::flush() {
  while (!m_frames.isEmpty()) closeFrame(m_frames.last().id);
  writeReady();
}

GuiRecorder::Frame::Frame(QObject *target) {
  if (!isActive()) return;
  GuiRecorder *r = instance();
  r->closeInputFrames(loopLevel());
  m_id = r->openFrame(target, false, nullptr);
}

GuiRecorder::Frame::~Frame() {
  if (m_id && isActive()) instance()->closeFrame(m_id);
}

int GuiRecorder::openFrame(QObject *target, bool input, const QEvent *event) {
  Entry entry;
  entry.openedAt = now();
  entry.seq = m_seq;
  m_entries.append(entry);
  const int id = m_nextFrameId++;
  m_frames.append({id, int(m_entries.size()) - 1, target, loopLevel(), input,
                   event, event ? int(event->type()) : 0, {}});
  return id;
}

void GuiRecorder::closeInputFrames(int level) {
  while (!m_frames.isEmpty() && m_frames.last().input &&
         m_frames.last().level >= level)
    closeFrame(m_frames.last().id);
}

void GuiRecorder::closeFrame(int id) {
  int index = -1;
  for (int i = 0; i < m_frames.size(); ++i)
    if (m_frames.at(i).id == id) index = i;
  if (index < 0) return;
  // Frames opened inside this one end with it.
  while (m_frames.size() > index + 1) closeFrame(m_frames.last().id);
  const OpenFrame frame = m_frames.takeLast();

  const Offer *pick = nullptr;
  auto choose = [&](auto &&accept) {
    for (const Offer &o : frame.offers)
      if (!pick && accept(o)) pick = &o;
  };
  auto named = [](const Offer &o) { return !o.lines.isEmpty(); };
  auto related = [&](const Offer &o) {
    return relatedTo(o.source, frame.target);
  };
  choose([&](const Offer &o) { return named(o) && related(o) && o.action; });
  choose([&](const Offer &o) { return named(o) && related(o); });
  choose(named);
  choose([](const Offer &) { return true; });

  Entry &entry = m_entries[frame.entry];
  entry.closed = true;
  if (pick && named(*pick)) {
    entry.lines = pick->lines;
    entry.action = true;
  } else if (pick) {
    entry.lines = {"# UNRECORDED: " + pick->unrecorded};
    std::cerr << "WARNING: recorder: unrecorded action on "
              << pick->unrecorded.toStdString()
              << "; give the widget an objectName" << std::endl;
  }
  writeReady();
}

void GuiRecorder::offer(QObject *source, const QStringList &lines, bool action,
                        const QString &unrecordedText) {
  if (m_frames.isEmpty()) return;  // not caused by the user or a gui_* action
  Offer o;
  o.lines = lines;
  o.source = source;
  o.action = action;
  o.unrecorded = unrecordedText;
  m_frames.last().offers.append(o);
}

void GuiRecorder::writeReady() {
  while (m_nextToWrite < m_entries.size() &&
         m_entries.at(m_nextToWrite).closed) {
    const Entry &entry = m_entries.at(m_nextToWrite++);
    if (entry.action) {
      // One wait per (name, arg), for its last occurrence before the action.
      QList<Milestone> last;
      for (const Milestone &m : m_milestones) {
        if (m.seq <= m_writtenSeq || m.seq > entry.seq) continue;
        last.erase(std::remove_if(last.begin(), last.end(),
                                  [&](const Milestone &l) {
                                    return l.name == m.name && l.arg == m.arg;
                                  }),
                   last.end());
        last.append(m);
      }
      for (const Milestone &m : last) {
        const qint64 timeout =
            std::max<qint64>(3 * (m.at - m_lastActionAt), kMinWaitMs);
        QString line = "gui_wait_event " + tclWord(m.name);
        if (!m.arg.isEmpty()) line += " " + tclWord(m.arg);
        writeLine(line + QString(" -timeout %1").arg(timeout));
      }
      m_milestones.erase(
          std::remove_if(m_milestones.begin(), m_milestones.end(),
                         [&](const Milestone &m) { return m.seq <= entry.seq; }),
          m_milestones.end());
      m_writtenSeq = std::max(m_writtenSeq, entry.seq);
      m_lastActionAt = entry.openedAt;
    }
    for (const QString &line : entry.lines) writeLine(line);
  }
}

void GuiRecorder::writeLine(const QString &line) {
  if (!m_out) return;
  m_out->write((line + "\n").toUtf8());
  if (m_file) m_file->flush();
}

QString GuiRecorder::pathWord(const QString &text, QStringList *notes) const {
  if (!QDir::isAbsolutePath(text)) return tclWord(text);
  const QString path = QDir::cleanPath(text);
#ifdef _WIN32
  const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
  const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
  struct Base {
    QString dir;
    const char *variable;
  };
  QList<Base> bases{{m_projectDir, "$::gui_test::project_dir"},
                    {m_installDir, "$::gui_test::install_dir"}};
  // The deeper directory wins when one contains the other.
  std::sort(bases.begin(), bases.end(), [](const Base &a, const Base &b) {
    return a.dir.size() > b.dir.size();
  });
  for (const Base &base : bases) {
    if (base.dir.isEmpty()) continue;
    if (path.compare(base.dir, cs) == 0) return base.variable;
    if (path.startsWith(base.dir + "/", cs))
      return QString("\"%1/%2\"")
          .arg(base.variable, escapeInQuotes(path.mid(base.dir.size() + 1)));
  }
  notes->append("# absolute path, not under the project or install directory");
  return tclWord(text);
}

void GuiRecorder::watch(QAction *action) {
  if (!action || action->property(kWatchedProperty).isValid()) return;
  action->setProperty(kWatchedProperty, true);
  connect(action, &QAction::triggered, this, [this, action]() {
    if (action) onAction(action);
  });
}

void GuiRecorder::watch(QWidget *widget) {
  if (widget->property(kWatchedProperty).isValid()) return;
  widget->setProperty(kWatchedProperty, true);
  for (QAction *a : widget->actions()) watch(a);

  if (auto button = qobject_cast<QAbstractButton *>(widget)) {
    // For clicks that do not come from a mouse release, such as keyboard.
    connect(button, &QAbstractButton::clicked, this, [this, button]() {
      onButton(button);
    });
  } else if (auto combo = qobject_cast<QComboBox *>(widget)) {
    connect(combo, &QComboBox::activated, this, [this, combo](int index) {
      if (combo->objectName().isEmpty()) {
        offer(combo, {}, false,
              unrecordedText(combo, combo->itemText(index)));
        return;
      }
      offer(combo, {"gui_select " + tclWord(combo->objectName()) + " " +
                    tclWord(combo->itemText(index))},
            false);
    });
  } else if (auto edit = qobject_cast<QLineEdit *>(widget)) {
    connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
      // Return and then the loss of focus both finish the same edit; a dialog
      // accepted from its button would otherwise record it a second time.
      const char *recorded = "guiRecorderText";
      if (edit->property(recorded).toString() == edit->text()) return;
      edit->setProperty(recorded, edit->text());
      QWidget *owner = namedOwner(edit);
      if (owner->objectName().isEmpty()) {
        offer(edit, {}, false, unrecordedText(owner, edit->text()));
        return;
      }
      QStringList lines;
      const QString word = pathWord(edit->text(), &lines);
      lines << "gui_set_text " + tclWord(owner->objectName()) + " " + word;
      offer(edit, lines, false);
    });
  } else if (auto view = qobject_cast<QAbstractItemView *>(widget)) {
    connect(view, &QAbstractItemView::clicked, this,
            [this, view](const QModelIndex &index) {
              const QString text = index.data().toString();
              if (view->objectName().isEmpty()) {
                offer(view, {}, false, unrecordedText(view, text));
                return;
              }
              QString line =
                  "gui_select " + tclWord(view->objectName()) + " " +
                  tclWord(text);
              if (index.column() != 0)
                line += QString(" -column %1").arg(index.column());
              offer(view, {line}, false);
            });
  } else if (auto bar = qobject_cast<QTabBar *>(widget)) {
    connect(bar, &QTabBar::tabBarClicked, this, [this, bar](int index) {
      if (index < 0) return;
      auto tabs = qobject_cast<QTabWidget *>(bar->parentWidget());
      QWidget *named = tabs ? static_cast<QWidget *>(tabs) : bar;
      const QString text = plain(bar->tabText(index));
      if (named->objectName().isEmpty()) {
        offer(bar, {}, false, unrecordedText(named, text));
        return;
      }
      offer(bar, {"gui_select " + tclWord(named->objectName()) + " " +
                  tclWord(text)},
            false);
    });
  } else if (widget->inherits("FOEDAG::TclConsoleWidget")) {
    connect(widget, SIGNAL(commandEntered(QString)), this,
            SLOT(onConsoleCommand(QString)));
  }
}

void GuiRecorder::onButton(QAbstractButton *button) {
  auto tool = qobject_cast<QToolButton *>(button);
  if (tool && tool->defaultAction()) return;  // recorded as its action
  if (!button->objectName().isEmpty()) {
    offer(button, {"gui_click " + tclWord(button->objectName())}, false);
    return;
  }
  const QString answer = dialogAnswer(button);
  if (!answer.isEmpty()) {
    offer(button, {answer}, false);
    return;
  }
  offer(button, {}, false, unrecordedText(button, plain(button->text())));
}

void GuiRecorder::aboutToClick(QAbstractButton *button) { onButton(button); }

void GuiRecorder::onAction(QAction *action) {
  if (action->isSeparator() || action->menu()) return;
  if (action->objectName().isEmpty()) {
    offer(action, {}, true, unrecordedText(action, plain(action->text())));
    return;
  }
  offer(action, {"gui_trigger_action " + tclWord(action->objectName())}, true);
}

void GuiRecorder::onConsoleCommand(const QString &command) {
  QWidget *console = qobject_cast<QWidget *>(sender());
  if (!console) return;
  if (console->objectName().isEmpty()) {
    offer(console, {}, false, unrecordedText(console, command));
    return;
  }
  offer(console,
        {"gui_set_text " + tclWord(console->objectName()) + " " +
         tclWord(command)},
        false);
}

void GuiRecorder::checkpoint(QWidget *widget) {
  if (!widget) return;
  closeInputFrames(loopLevel());
  QWidget *named = namedOwner(widget);
  QStringList lines;
  const QString name = named->objectName();
  auto button = qobject_cast<QAbstractButton *>(named);
  bool hasText{false};
  const QString text = guiDisplayedText(named, &hasText);
  if (name.isEmpty()) {
    lines << "# UNRECORDED: checkpoint on " + unrecordedText(named, text);
    std::cerr << "WARNING: recorder: checkpoint on an unnamed "
              << named->metaObject()->className()
              << "; give the widget an objectName" << std::endl;
  } else if (button && button->isCheckable()) {
    const QString get = "[gui_get " + tclWord(name) + " checked]";
    lines << "gui_assert " + tclWord(button->isChecked() ? get : "!" + get);
  } else if (hasText && !named->inherits("QTextEdit") &&
             !named->inherits("QPlainTextEdit")) {
    // expr needs a quoted operand; a bare word is a syntax error.
    lines << "gui_assert " +
                 tclWord("[gui_get " + tclWord(name) + " -text] eq \"" +
                         escapeInQuotes(text) + "\"");
  } else {
    lines << "# UNRECORDED: checkpoint on " + name + ": no value to check";
  }
  // The window the widget is in; the main window is the default capture.
  QWidget *window = named->window();
  QString screenshot = QString("gui_screenshot %1/%2")
                           .arg(m_scenario)
                           .arg(++m_checkpoints, 2, 10, QChar('0'));
  if (window && !window->inherits("QMainWindow")) {
    if (window->objectName().isEmpty())
      screenshot = "# UNRECORDED: checkpoint screenshot of an unnamed " +
                   QString(window->metaObject()->className());
    else
      screenshot += " -widget " + tclWord(window->objectName());
  }
  lines << screenshot;
  Entry entry;
  entry.closed = true;
  entry.openedAt = now();
  entry.seq = m_seq;
  entry.lines = lines;
  m_entries.append(entry);
  writeReady();
}

bool GuiRecorder::eventFilter(QObject *watched, QEvent *event) {
  switch (event->type()) {
    case QEvent::Show:
      if (auto w = qobject_cast<QWidget *>(watched)) watch(w);
      break;
    case QEvent::ActionAdded:
      watch(static_cast<QActionEvent *>(event)->action());
      break;
    case QEvent::KeyPress: {
      auto key = static_cast<QKeyEvent *>(event);
      if (key->key() == Qt::Key_S &&
          key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        checkpoint(QApplication::focusWidget());
        return true;
      }
      [[fallthrough]];
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Shortcut:
    case QEvent::ContextMenu: {
      if (!qobject_cast<QWidget *>(watched) && !qobject_cast<QAction *>(watched))
        break;
      // An event passed on to a parent widget is still the same input. Event
      // objects are reused for new input, so the address alone is not proof.
      for (const OpenFrame &f : m_frames)
        if (f.input && f.event == event && f.eventType == event->type() &&
            watched != f.target && isAncestor(watched, f.target))
          return QObject::eventFilter(watched, event);
      closeInputFrames(loopLevel());
      openFrame(watched, true, event);
      if (event->type() == QEvent::MouseButtonRelease) {
        auto button = qobject_cast<QAbstractButton *>(watched);
        auto mouse = static_cast<QMouseEvent *>(event);
        if (button && button->isDown() && mouse->button() == Qt::LeftButton &&
            button->rect().contains(mouse->position().toPoint()))
          aboutToClick(button);
      }
      break;
    }
    default:
      break;
  }
  return QObject::eventFilter(watched, event);
}

}  // namespace FOEDAG
