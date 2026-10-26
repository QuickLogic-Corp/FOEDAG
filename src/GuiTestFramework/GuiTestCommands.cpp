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
#include "GuiTestCommands.h"

extern "C" {
#include <tcl.h>
}

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGroupBox>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <optional>
#include <utility>

#include "GuiRecorder.h"
#include "GuiScreenshot.h"
#include "GuiTestHub.h"
#include "GuiTestMode.h"
#include "GuiTestRunner.h"

namespace FOEDAG {

namespace {

constexpr int kDefaultTimeoutMs = 30000;
constexpr int kPollMs = 20;

// Every gui_* action: the snapshot gui_wait_event counts from, and one
// recorder frame, so the recorder writes it as a user's action.
struct GuiTestAction {
  explicit GuiTestAction(QObject *target) : frame(target) {
    GuiTestHub::instance()->takeSnapshot();
  }
  GuiRecorder::Frame frame;
};

int fail(Tcl_Interp *interp, const QString &message) {
  Tcl_SetObjResult(interp, Tcl_NewStringObj(message.toUtf8().constData(), -1));
  return TCL_ERROR;
}

int succeed(Tcl_Interp *interp, const QString &result = {}) {
  Tcl_SetObjResult(interp, Tcl_NewStringObj(result.toUtf8().constData(), -1));
  return TCL_OK;
}

QString plain(QString text) { return text.remove('&'); }

QString describe(const QObject *o) {
  return QString("%1 '%2'").arg(o->metaObject()->className(), o->objectName());
}

struct Options {
  QStringList positional;
  QHash<QString, QString> values;
  QSet<QString> flags;
};

// Options may appear anywhere after the command name.
bool parseOptions(int argc, const char *argv[], const QSet<QString> &valueOpts,
                  const QSet<QString> &flagOpts, Options *out,
                  QString *error) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromUtf8(argv[i]);
    if (valueOpts.contains(arg)) {
      if (i + 1 >= argc) {
        *error = QString("option %1 needs a value").arg(arg);
        return false;
      }
      out->values.insert(arg, QString::fromUtf8(argv[++i]));
    } else if (flagOpts.contains(arg)) {
      out->flags.insert(arg);
    } else {
      out->positional.append(arg);
    }
  }
  return true;
}

bool timeoutOption(const Options &options, int *timeoutMs, QString *error) {
  *timeoutMs = kDefaultTimeoutMs;
  if (!options.values.contains("-timeout")) return true;
  bool ok{false};
  *timeoutMs = options.values.value("-timeout").toInt(&ok);
  if (!ok || *timeoutMs < 0) {
    *error = "-timeout expects milliseconds";
    return false;
  }
  return true;
}

QList<QAction *> allActions() {
  QSet<QAction *> actions;
  for (QWidget *w : QApplication::allWidgets()) {
    for (QAction *a : w->actions()) actions.insert(a);
    for (QAction *a : w->findChildren<QAction *>(Qt::FindDirectChildrenOnly))
      actions.insert(a);
  }
  return actions.values();
}

// Exactly one visible widget with this objectName (and class) must exist.
QWidget *findWidget(const QString &name, const QString &className,
                    QString *error) {
  QList<QWidget *> visible;
  int hidden = 0;
  for (QWidget *w : QApplication::allWidgets()) {
    if (w->objectName() != name) continue;
    if (!className.isEmpty() && !w->inherits(className.toUtf8().constData()))
      continue;
    (w->isVisible() ? visible.append(w) : void(++hidden));
  }
  if (visible.size() == 1) return visible.first();
  if (visible.isEmpty()) {
    *error = QString("no visible widget named '%1'").arg(name);
    if (hidden) *error += QString(" (%1 hidden)").arg(hidden);
  } else {
    QStringList classes;
    for (QWidget *w : visible) classes << w->metaObject()->className();
    *error = QString("%1 visible widgets named '%2' (%3)")
                 .arg(visible.size())
                 .arg(name, classes.join(", "));
  }
  return nullptr;
}

QAction *findAction(const QString &name, QString *error) {
  QList<QAction *> found;
  for (QAction *a : allActions())
    if (a->objectName() == name) found.append(a);
  // Views rebuilt on reopen leave their old, hidden copies alive.
  if (found.size() > 1) {
    QList<QAction *> live;
    for (QAction *a : found) {
      auto parent = qobject_cast<QWidget *>(a->parent());
      if (!parent || parent->isVisible()) live.append(a);
    }
    if (!live.isEmpty()) found = live;
  }
  if (found.size() == 1) return found.first();
  *error = found.isEmpty()
               ? QString("no action named '%1'").arg(name)
               : QString("%1 actions named '%2'").arg(found.size()).arg(name);
  return nullptr;
}

QObject *findObject(const QString &name, QString *error) {
  QString widgetError;
  if (QWidget *w = findWidget(name, {}, &widgetError)) return w;
  QString actionError;
  if (QAction *a = findAction(name, &actionError)) return a;
  const bool noAction = actionError.startsWith("no action");
  *error = noAction ? widgetError + " and no action of that name" : actionError;
  return nullptr;
}

// Polls the condition until it holds or the timeout expires. A modal dialog
// opened above the wait blocks its return until the dialog closes; if it is
// still open at the timeout, the run is ended from inside its loop. A strict
// wait does not let the next script line answer such a dialog.
bool waitUntil(const std::function<bool()> &condition, int timeoutMs,
               const QString &what, bool strict) {
  std::optional<GuiTestRunner::StrictWait> guard;
  if (strict) guard.emplace();
  GuiTestHub::instance()->pollBusy();
  if (condition()) return true;

  QElapsedTimer elapsed;
  elapsed.start();
  QEventLoop loop;
  const int level = QThread::currentThread()->loopLevel() + 1;
  bool satisfied{false};
  QTimer poll;
  poll.setInterval(kPollMs);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
    GuiTestHub::instance()->pollBusy();
    const bool nested = QThread::currentThread()->loopLevel() > level;
    if (!satisfied) satisfied = condition();
    if (satisfied) {
      loop.quit();
      if (!nested) return;
    }
    if (elapsed.elapsed() < timeoutMs) return;
    if (nested) {
      QWidget *modal = QApplication::activeModalWidget();
      GuiTestRunner::instance()->fail(
          QString("%1: timed out after %2 ms while dialog '%3' (%4) is open")
              .arg(what)
              .arg(timeoutMs)
              .arg(modal ? modal->windowTitle() : QString{},
                   modal ? modal->objectName() : QString{}));
    }
    loop.quit();
  });
  poll.start();
  loop.exec();
  return satisfied;
}

// A dialog left open is the usual reason a wait never ends.
QString openDialogNote() {
  QWidget *modal = QApplication::activeModalWidget();
  if (!modal) return {};
  return QString(" (dialog '%1' (%2) is open)")
      .arg(modal->windowTitle(), modal->objectName());
}

QWidget *mouseTarget(QWidget *w) {
  if (auto area = qobject_cast<QAbstractScrollArea *>(w)) return area->viewport();
  return w;
}

void sendMouse(QWidget *w, QEvent::Type type, const QPoint &pos,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
  const QPointF local{pos};
  QMouseEvent event{type,   local,   QPointF{w->mapToGlobal(pos)},
                    button, buttons, Qt::NoModifier};
  QCoreApplication::sendEvent(w, &event);
}

QString displayedText(QObject *o, bool *ok) {
  *ok = true;
  if (auto w = qobject_cast<QLabel *>(o)) return w->text();
  if (auto w = qobject_cast<QLineEdit *>(o)) return w->text();
  if (auto w = qobject_cast<QAbstractButton *>(o)) return plain(w->text());
  if (auto w = qobject_cast<QComboBox *>(o)) return w->currentText();
  if (auto w = qobject_cast<QPlainTextEdit *>(o)) return w->toPlainText();
  if (auto w = qobject_cast<QTextEdit *>(o)) return w->toPlainText();
  if (auto w = qobject_cast<QAbstractSpinBox *>(o)) return w->text();
  if (auto w = qobject_cast<QTabWidget *>(o))
    return plain(w->tabText(w->currentIndex()));
  if (auto w = qobject_cast<QTabBar *>(o))
    return plain(w->tabText(w->currentIndex()));
  if (auto w = qobject_cast<QAbstractItemView *>(o))
    return w->currentIndex().data().toString();
  if (auto w = qobject_cast<QGroupBox *>(o)) return w->title();
  if (auto w = qobject_cast<QProgressBar *>(o)) return w->text();
  if (auto a = qobject_cast<QAction *>(o)) return plain(a->text());
  if (auto w = qobject_cast<QWidget *>(o); w && w->isWindow())
    return w->windowTitle();
  *ok = false;
  return {};
}

QAbstractButton *dialogButton(QWidget *dialog, const QString &text) {
  for (QAbstractButton *b : dialog->findChildren<QAbstractButton *>())
    if (b->isVisible() && plain(b->text()) == text) return b;
  return nullptr;
}

QAbstractButton *roleButton(QWidget *dialog, bool accept) {
  const QSet<QDialogButtonBox::ButtonRole> roles =
      accept ? QSet{QDialogButtonBox::AcceptRole, QDialogButtonBox::YesRole}
             : QSet{QDialogButtonBox::RejectRole, QDialogButtonBox::NoRole};
  for (auto box : dialog->findChildren<QDialogButtonBox *>())
    for (QAbstractButton *b : box->buttons())
      if (b->isVisible() && b->isEnabled() && roles.contains(box->buttonRole(b)))
        return b;
  return nullptr;
}

// Answers the active modal dialog. Returns an error message, or empty.
QString answerDialog(QWidget *modal, const QString &how, const QString &text) {
  const GuiTestAction act{modal};
  if (how == "button") {
    QAbstractButton *b = dialogButton(modal, text);
    if (!b) return QString("dialog '%1' has no button '%2'")
                   .arg(modal->windowTitle(), text);
    if (!b->isEnabled()) return QString("button '%1' is disabled").arg(text);
    b->click();
    return {};
  }
  const bool accept = how == "accept";
  if (QAbstractButton *b = roleButton(modal, accept)) {
    b->click();
    return {};
  }
  if (auto box = qobject_cast<QMessageBox *>(modal); box && !accept &&
                                                      box->escapeButton()) {
    box->escapeButton()->click();
    return {};
  }
  auto dialog = qobject_cast<QDialog *>(modal);
  if (!dialog) return QString("%1 is not a dialog").arg(describe(modal));
  accept ? dialog->accept() : dialog->reject();
  return {};
}

int guiFind(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  if (!parseOptions(argc, argv, {"-class"}, {}, &o, &error) ||
      o.positional.size() != 1)
    return fail(interp, "usage: gui_find <objName> [-class <QClass>]" +
                            (error.isEmpty() ? "" : ": " + error));
  const QString name = o.positional.first();
  if (!findWidget(name, o.values.value("-class"), &error))
    return fail(interp, QString("gui_find %1: %2").arg(name, error));
  return succeed(interp, name);
}

int guiClick(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  if (!parseOptions(argc, argv, {"-button"}, {"-double"}, &o, &error) ||
      o.positional.size() != 1)
    return fail(interp,
                "usage: gui_click <objName> [-button left|right] [-double]");
  const QString name = o.positional.first();
  const QString button = o.values.value("-button", "left");
  if (button != "left" && button != "right")
    return fail(interp, QString("gui_click %1: -button must be left or right")
                            .arg(name));
  QWidget *w = findWidget(name, {}, &error);
  if (!w) return fail(interp, QString("gui_click %1: %2").arg(name, error));
  if (!w->isEnabled())
    return fail(interp, QString("gui_click %1: widget is disabled").arg(name));

  const GuiTestAction act{w};
  const bool dbl = o.flags.contains("-double");
  auto pushButton = qobject_cast<QAbstractButton *>(w);
  if (pushButton && button == "left" && !dbl) {
    if (GuiRecorder::isActive())
      GuiRecorder::instance()->aboutToClick(pushButton);
    pushButton->click();
    return succeed(interp);
  }
  QWidget *target = mouseTarget(w);
  const QPoint pos = target->rect().center();
  const Qt::MouseButton b = button == "left" ? Qt::LeftButton : Qt::RightButton;
  sendMouse(target, QEvent::MouseButtonPress, pos, b, b);
  sendMouse(target, QEvent::MouseButtonRelease, pos, b, Qt::NoButton);
  if (dbl) {
    sendMouse(target, QEvent::MouseButtonDblClick, pos, b, b);
    sendMouse(target, QEvent::MouseButtonRelease, pos, b, Qt::NoButton);
  }
  if (b == Qt::RightButton) {
    QContextMenuEvent menu{QContextMenuEvent::Mouse, pos,
                           target->mapToGlobal(pos)};
    QCoreApplication::sendEvent(target, &menu);
  }
  return succeed(interp);
}

int guiSetText(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 3) return fail(interp, "usage: gui_set_text <objName> <text>");
  const QString name = QString::fromUtf8(argv[1]);
  const QString text = QString::fromUtf8(argv[2]);
  QString error;
  QWidget *w = findWidget(name, {}, &error);
  if (!w) return fail(interp, QString("gui_set_text %1: %2").arg(name, error));
  if (!w->isEnabled())
    return fail(interp,
                QString("gui_set_text %1: widget is disabled").arg(name));

  const GuiTestAction act{w};
  auto typeInto = [&](QLineEdit *edit) -> int {
    if (edit->isReadOnly())
      return fail(interp,
                  QString("gui_set_text %1: widget is read-only").arg(name));
    // insert() goes through the validator and emits textEdited like typing.
    edit->selectAll();
    edit->insert(text);
    if (edit->text() != text)
      return fail(interp, QString("gui_set_text %1: expected '%2', got '%3'")
                              .arg(name, text, edit->text()));
    emit edit->editingFinished();
    return succeed(interp);
  };
  if (w->inherits("QConsole")) {
    // The console reads a command typed at its prompt and run on Return.
    for (const QChar c : text) {
      QKeyEvent key{QEvent::KeyPress, 0, Qt::NoModifier, QString{c}};
      QCoreApplication::sendEvent(w, &key);
    }
    QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier};
    QCoreApplication::sendEvent(w, &enter);
    return succeed(interp);
  }
  if (auto edit = qobject_cast<QLineEdit *>(w)) return typeInto(edit);
  if (auto combo = qobject_cast<QComboBox *>(w); combo && combo->lineEdit())
    return typeInto(combo->lineEdit());
  if (auto spin = qobject_cast<QAbstractSpinBox *>(w)) {
    auto edit = spin->findChild<QLineEdit *>();
    if (!edit) return fail(interp, QString("gui_set_text %1: no editor").arg(name));
    edit->selectAll();
    edit->insert(text);
    spin->interpretText();
    emit spin->editingFinished();
    return succeed(interp);
  }
  if (auto edit = qobject_cast<QPlainTextEdit *>(w); edit && !edit->isReadOnly()) {
    edit->setPlainText(text);
    return succeed(interp);
  }
  if (auto edit = qobject_cast<QTextEdit *>(w); edit && !edit->isReadOnly()) {
    edit->setPlainText(text);
    return succeed(interp);
  }
  return fail(interp, QString("gui_set_text %1: %2 does not take text")
                          .arg(name, w->metaObject()->className()));
}

int guiSelect(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  if (!parseOptions(argc, argv, {"-column"}, {}, &o, &error) ||
      o.positional.size() != 2)
    return fail(interp, "usage: gui_select <objName> <itemText> [-column n]");
  const QString name = o.positional.at(0);
  const QString item = o.positional.at(1);
  const int column = o.values.value("-column", "0").toInt();
  QWidget *w = findWidget(name, {}, &error);
  if (!w) return fail(interp, QString("gui_select %1: %2").arg(name, error));
  if (!w->isEnabled())
    return fail(interp, QString("gui_select %1: widget is disabled").arg(name));

  const GuiTestAction act{w};
  auto notFound = [&](const QStringList &items) {
    return fail(interp, QString("gui_select %1: no item '%2' (items: %3)")
                            .arg(name, item, items.mid(0, 10).join(", ")));
  };
  if (auto combo = qobject_cast<QComboBox *>(w)) {
    const int index = combo->findText(item, Qt::MatchExactly);
    if (index < 0) {
      QStringList items;
      for (int i = 0; i < combo->count(); ++i) items << combo->itemText(i);
      return notFound(items);
    }
    combo->setCurrentIndex(index);
    emit combo->activated(index);
    emit combo->textActivated(item);
    return succeed(interp);
  }
  QTabBar *bar = qobject_cast<QTabBar *>(w);
  if (auto tabs = qobject_cast<QTabWidget *>(w)) bar = tabs->tabBar();
  if (bar) {
    QStringList items;
    for (int i = 0; i < bar->count(); ++i) {
      if (plain(bar->tabText(i)) == item) {
        emit bar->tabBarClicked(i);  // a real click emits it; the recorder uses it
        bar->setCurrentIndex(i);
        return succeed(interp);
      }
      items << plain(bar->tabText(i));
    }
    return notFound(items);
  }
  if (auto view = qobject_cast<QAbstractItemView *>(w); view && view->model()) {
    QAbstractItemModel *model = view->model();
    const QModelIndexList hits =
        model->match(model->index(0, column, view->rootIndex()),
                     Qt::DisplayRole, item, 1,
                     Qt::MatchExactly | Qt::MatchRecursive | Qt::MatchWrap);
    if (hits.isEmpty()) {
      QStringList items;
      for (int r = 0; r < model->rowCount(view->rootIndex()); ++r)
        items << model->index(r, column, view->rootIndex()).data().toString();
      return notFound(items);
    }
    const QModelIndex index = hits.first();
    view->scrollTo(index);
    view->setCurrentIndex(index);
    emit view->clicked(index);
    return succeed(interp);
  }
  return fail(interp, QString("gui_select %1: %2 has no items")
                          .arg(name, w->metaObject()->className()));
}

int guiTriggerAction(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 2) return fail(interp, "usage: gui_trigger_action <objName>");
  const QString name = QString::fromUtf8(argv[1]);
  QString error;
  QAction *action = findAction(name, &error);
  if (!action)
    return fail(interp, QString("gui_trigger_action %1: %2").arg(name, error));
  if (!action->isEnabled())
    return fail(interp,
                QString("gui_trigger_action %1: action is disabled").arg(name));
  const GuiTestAction act{action};
  action->trigger();
  return succeed(interp);
}

int guiGet(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 3) return fail(interp, "usage: gui_get <objName> <property>|-text");
  const QString name = QString::fromUtf8(argv[1]);
  const QString what = QString::fromUtf8(argv[2]);
  QString error;
  QObject *o = findObject(name, &error);
  if (!o) return fail(interp, QString("gui_get %1: %2").arg(name, error));
  if (what == "-text") {
    bool ok{false};
    const QString text = displayedText(o, &ok);
    if (!ok)
      return fail(interp, QString("gui_get %1: %2 shows no text")
                              .arg(name, o->metaObject()->className()));
    return succeed(interp, text);
  }
  const QVariant value = o->property(what.toUtf8().constData());
  if (!value.isValid())
    return fail(interp, QString("gui_get %1: %2 has no property '%3'")
                            .arg(name, o->metaObject()->className(), what));
  if (value.typeId() == QMetaType::Bool)
    return succeed(interp, value.toBool() ? "1" : "0");
  if (value.typeId() == QMetaType::QStringList) {
    Tcl_Obj *list = Tcl_NewListObj(0, nullptr);
    for (const QString &s : value.toStringList())
      Tcl_ListObjAppendElement(
          interp, list, Tcl_NewStringObj(s.toUtf8().constData(), -1));
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
  }
  return succeed(interp, value.toString());
}

int guiDrag(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 6)
    return fail(interp, "usage: gui_drag <objName> <x1> <y1> <x2> <y2>");
  const QString name = QString::fromUtf8(argv[1]);
  int c[4];
  for (int i = 0; i < 4; ++i) {
    bool ok{false};
    c[i] = QString::fromUtf8(argv[i + 2]).toInt(&ok);
    if (!ok)
      return fail(interp, QString("gui_drag %1: coordinates must be integers")
                              .arg(name));
  }
  QString error;
  QWidget *w = findWidget(name, {}, &error);
  if (!w) return fail(interp, QString("gui_drag %1: %2").arg(name, error));
  if (!w->isEnabled())
    return fail(interp, QString("gui_drag %1: widget is disabled").arg(name));

  const GuiTestAction act{w};
  QWidget *target = mouseTarget(w);
  const QPoint from{c[0], c[1]};
  const QPoint to{c[2], c[3]};
  sendMouse(target, QEvent::MouseButtonPress, from, Qt::LeftButton,
            Qt::LeftButton);
  constexpr int kSteps = 5;
  for (int i = 1; i <= kSteps; ++i)
    sendMouse(target, QEvent::MouseMove, from + (to - from) * i / kSteps,
              Qt::NoButton, Qt::LeftButton);
  sendMouse(target, QEvent::MouseButtonRelease, to, Qt::LeftButton,
            Qt::NoButton);
  return succeed(interp);
}

int guiWaitFor(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  int timeoutMs{0};
  if (!parseOptions(argc, argv, {"-timeout", "-message"}, {}, &o, &error) ||
      o.positional.size() != 1 || !timeoutOption(o, &timeoutMs, &error))
    return fail(interp,
                "usage: gui_wait_for <expr> [-timeout ms] [-message text]" +
                    (error.isEmpty() ? "" : ": " + error));
  const QByteArray expr = o.positional.first().toUtf8();
  QString lastError;
  auto holds = [&]() {
    int result{0};
    if (Tcl_ExprBoolean(interp, expr.constData(), &result) != TCL_OK) {
      lastError = Tcl_GetStringResult(interp);
      return false;
    }
    lastError.clear();
    return result != 0;
  };
  const QString what = QString("gui_wait_for {%1}").arg(o.positional.first());
  if (waitUntil(holds, timeoutMs, what, true)) return succeed(interp);
  QString message = QString("%1: timed out after %2 ms").arg(what).arg(timeoutMs);
  if (o.values.contains("-message")) message += ": " + o.values.value("-message");
  if (!lastError.isEmpty()) message += " (last error: " + lastError + ")";
  return fail(interp, message + openDialogNote());
}

int guiAssert(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 2 && argc != 3)
    return fail(interp, "usage: gui_assert <expr> [message]");
  const QString expr = QString::fromUtf8(argv[1]);
  const QString what = QString("gui_assert {%1}").arg(expr);
  int result{0};
  if (Tcl_ExprBoolean(interp, argv[1], &result) != TCL_OK)
    return fail(interp, what + ": " + Tcl_GetStringResult(interp));
  if (result) return succeed(interp);
  QString message = what + ": false";
  if (argc == 3) message += ": " + QString::fromUtf8(argv[2]);
  Tcl_Obj *source = Tcl_NewStringObj(argv[1], -1);
  Tcl_IncrRefCount(source);
  if (Tcl_Obj *values = Tcl_SubstObj(interp, source, TCL_SUBST_ALL))
    message += QString(" (evaluated: %1)").arg(Tcl_GetString(values));
  Tcl_DecrRefCount(source);
  return fail(interp, message);
}

int guiDialog(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  int timeoutMs{0};
  const char *usage =
      "usage: gui_dialog accept|reject|button <text> [-timeout ms]";
  if (!parseOptions(argc, argv, {"-timeout"}, {}, &o, &error) ||
      o.positional.isEmpty() || !timeoutOption(o, &timeoutMs, &error))
    return fail(interp, usage);
  const QString how = o.positional.first();
  const bool byButton = how == "button";
  if (!(how == "accept" || how == "reject" || byButton) ||
      o.positional.size() != (byButton ? 2 : 1))
    return fail(interp, usage);
  const QString text = byButton ? o.positional.at(1) : QString{};
  const QString what = QString("gui_dialog %1").arg(o.positional.join(' '));

  // Answered from inside the poll, so a dialog that opens while waiting is
  // reached from within its own event loop.
  QString answerError;
  auto answered = [&]() {
    QWidget *modal = QApplication::activeModalWidget();
    if (!modal) return false;
    answerError = answerDialog(modal, how, text);
    return true;
  };
  if (!waitUntil(answered, timeoutMs, what, false))
    return fail(interp, QString("%1: no modal dialog within %2 ms")
                            .arg(what)
                            .arg(timeoutMs));
  if (!answerError.isEmpty())
    return fail(interp, QString("%1: %2").arg(what, answerError));
  return succeed(interp);
}

int guiWaitEvent(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  Options o;
  QString error;
  int timeoutMs{0};
  if (!parseOptions(argc, argv, {"-timeout"}, {}, &o, &error) ||
      o.positional.isEmpty() || o.positional.size() > 2 ||
      !timeoutOption(o, &timeoutMs, &error))
    return fail(interp, "usage: gui_wait_event <name> [arg] [-timeout ms]");
  const QString name = o.positional.at(0);
  const QString arg = o.positional.value(1);
  if (!waitForMilestone(name, arg, timeoutMs, &error))
    return fail(interp, error);
  return succeed(interp);
}

QWidget *mainWindow() {
  for (QWidget *w : QApplication::topLevelWidgets())
    if (w->isVisible() && w->inherits("QMainWindow")) return w;
  return nullptr;
}

bool writeImage(const QImage &image, const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  return image.save(path, "PNG");
}

int guiScreenshot(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  const char *usage =
      "usage: gui_screenshot <scenario>/<step> [-widget objName] "
      "[-mask objName ...]";
  QString id;
  QString widgetName;
  QStringList maskNames;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromUtf8(argv[i]);
    if (arg == "-widget" || arg == "-mask") {
      if (i + 1 >= argc) return fail(interp, usage);
      const QString value = QString::fromUtf8(argv[++i]);
      arg == "-widget" ? void(widgetName = value) : void(maskNames << value);
    } else if (id.isEmpty()) {
      id = arg;
    } else {
      return fail(interp, usage);
    }
  }
  const QStringList parts = id.split('/');
  if (parts.size() != 2 || parts[0].isEmpty() || parts[1].isEmpty() ||
      id.contains(".."))
    return fail(interp, usage);
  const QString what = "gui_screenshot " + id;

  QString error;
  QWidget *target = widgetName.isEmpty() ? mainWindow()
                                         : findWidget(widgetName, {}, &error);
  if (!target)
    return fail(interp, QString("%1: %2").arg(what, widgetName.isEmpty()
                                                        ? "no main window shown"
                                                        : error));
  QList<QWidget *> masks;
  for (const QString &name : maskNames) {
    QWidget *mask = findWidget(name, {}, &error);
    if (!mask) return fail(interp, QString("%1: mask %2").arg(what, error));
    masks << mask;
  }

  const QString goldenRoot =
      qEnvironmentVariable("GUI_TEST_FRAMEWORK_GOLDEN_DIR");
  const QString golden = QString("%1/%2/%3.png")
                             .arg(goldenRoot, GuiScreenshot::platformDir(), id);
  GuiScreenshot::Sidecar sidecar;
  if (!goldenRoot.isEmpty()) {
    sidecar = GuiScreenshot::readSidecar(
        golden.chopped(4) + ".json", &error);
    if (!error.isEmpty()) return fail(interp, what + ": " + error);
  }

  QImage image;
  if (!GuiScreenshot::settle(target, masks, sidecar.masks,
                             GuiScreenshot::kDefaultSettleMs, &image))
    return fail(interp, QString("%1: unstable: the image still changed after "
                                "%2 ms")
                            .arg(what)
                            .arg(GuiScreenshot::kDefaultSettleMs));
  QString artifacts = qEnvironmentVariable("GUI_TEST_FRAMEWORK_ARTIFACT_DIR");
  if (artifacts.isEmpty()) artifacts = QDir::currentPath() + "/screenshots";
  const QString capture = QString("%1/%2.png").arg(artifacts, id);
  if (!writeImage(image, capture))
    return fail(interp, QString("%1: cannot write %2").arg(what, capture));

  const bool display = GuiTestMode::isDisplayMode();
  auto report = [&](const QString &line) {
    std::cout << line.toStdString() << std::endl;
    return succeed(interp);
  };
  if (goldenRoot.isEmpty())
    return report(QString("SKIP %1: GUI_TEST_FRAMEWORK_GOLDEN_DIR is not set")
                      .arg(id));
  if (GuiTestMode::isBlessMode()) {
    if (!writeImage(image, golden))
      return fail(interp, QString("%1: cannot write %2").arg(what, golden));
    return report("BLESS " + id);
  }
#ifdef __APPLE__
  return report(QString("SKIP %1: no macOS goldens yet").arg(id));
#endif
  const QImage reference{golden};
  if (reference.isNull()) {
    if (display) return report("MISSING " + id);
    return fail(interp, QString("%1: missing golden %2").arg(what, golden));
  }
  const int differing =
      GuiScreenshot::countDifferences(reference, image, sidecar.tolerance);
  if (differing >= 0 && differing <= sidecar.maxDiffPixels)
    return display ? report("MATCH " + id) : succeed(interp);

  const QString diff = QString("%1/%2.diff.png").arg(artifacts, id);
  writeImage(GuiScreenshot::triptych(reference, image), diff);
  const QString detail =
      differing < 0
          ? QString("size differs: golden %1x%2, capture %3x%4")
                .arg(reference.width())
                .arg(reference.height())
                .arg(image.width())
                .arg(image.height())
          : QString("%1 px").arg(differing);
  if (display) return report(QString("MISMATCH %1 (%2)").arg(id, detail));
  return fail(interp, QString("%1: mismatch: %2, %3 allowed; triptych %4")
                          .arg(what, detail)
                          .arg(sidecar.maxDiffPixels)
                          .arg(diff));
}

int guiTestCheckpoint(void *, Tcl_Interp *interp, int argc,
                      const char *argv[]) {
  if (argc != 2) return fail(interp, "usage: gui_test_checkpoint <objName>");
  const QString name = QString::fromUtf8(argv[1]);
  QString error;
  QWidget *w = findWidget(name, {}, &error);
  if (!w)
    return fail(interp, QString("gui_test_checkpoint %1: %2").arg(name, error));
  if (GuiRecorder::isActive()) GuiRecorder::instance()->checkpoint(w);
  return succeed(interp);
}

int guiTestRun(void *, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (argc != 2) return fail(interp, "usage: gui_test_run <script>");
  GuiTestRunner::instance()->run(interp, QString::fromUtf8(argv[1]));
}

}  // namespace

QString guiDisplayedText(QObject *object, bool *ok) {
  return displayedText(object, ok);
}

bool waitForMilestone(const QString &name, const QString &arg, int timeoutMs,
                      QString *error) {
  const QString awaited = arg.isEmpty() ? name : name + " " + arg;
  const QString what = "gui_wait_event " + awaited;
  GuiTestHub *hub = GuiTestHub::instance();
  if (waitUntil([&]() { return hub->reachedSinceSnapshot(name, arg); },
                timeoutMs, what, true))
    return true;
  *error = QString("%1: timed out after %2 ms; last milestones: %3")
               .arg(what)
               .arg(timeoutMs)
               .arg(hub->recent().isEmpty() ? "none"
                                            : hub->recent().join(", ")) +
           openDialogNote();
  return false;
}

void registerGuiTestCommands(Tcl_Interp *interp) {
  const std::pair<const char *, Tcl_CmdProc *> commands[] = {
      {"gui_find", guiFind},
      {"gui_click", guiClick},
      {"gui_set_text", guiSetText},
      {"gui_select", guiSelect},
      {"gui_trigger_action", guiTriggerAction},
      {"gui_get", guiGet},
      {"gui_drag", guiDrag},
      {"gui_wait_for", guiWaitFor},
      {"gui_assert", guiAssert},
      {"gui_dialog", guiDialog},
      {"gui_wait_event", guiWaitEvent},
      {"gui_screenshot", guiScreenshot},
      {"gui_test_run", guiTestRun},
      {"gui_test_checkpoint", guiTestCheckpoint},
  };
  for (const auto &[name, proc] : commands)
    Tcl_CreateCommand(interp, name, proc, nullptr, nullptr);
}

}  // namespace FOEDAG
