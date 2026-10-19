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
#include "GuiTestHub.h"

#include <QApplication>
#include <QChildEvent>
#include <QDialog>
#include <QFileInfo>
#include <QMetaMethod>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "GuiMilestone.h"

namespace FOEDAG {

namespace {

struct BridgeEntry {
  const char *className;
  const char *signal;  // normalized signature
  const char *milestone;
};

// Existing signals turned into milestones. Every name is listed in
// docs/specs/gui-test-automation/milestones.md of aurora2.
constexpr BridgeEntry kBridges[] = {
    {"FOEDAG::TaskManager", "started()", "task.started"},
    {"FOEDAG::TaskManager", "done()", "task.done"},
};

constexpr int kRecentSize = 10;
const QChar kArgSeparator{0x1f};

class BridgeRelay : public QObject {
  Q_OBJECT
 public:
  BridgeRelay(const QString &milestone, QObject *parent)
      : QObject(parent), m_milestone(milestone) {}
 public slots:
  void fire() { GuiTestHub::instance()->raise(m_milestone); }

 private:
  QString m_milestone;
};

bool isTrackedWindow(QWidget *w) {
  if (!w->isWindow()) return false;
  const Qt::WindowType type = w->windowType();
  return type != Qt::Popup && type != Qt::ToolTip && type != Qt::SplashScreen;
}

}  // namespace

GuiTestHub *GuiTestHub::instance() {
  static GuiTestHub *hub = new GuiTestHub;
  return hub;
}

GuiTestHub::Kind GuiTestHub::kindOf(const QString &name) {
  static const QStringList info{"task.started", "process.started", "busy.on"};
  return info.contains(name) ? Kind::Info : Kind::Sync;
}

void GuiTestHub::enable() {
  if (GuiTest::milestonesEnabled) return;
  GuiTest::milestoneSink = [](const QString &name, const QString &arg) {
    GuiTestHub::instance()->raise(name, arg);
  };
  GuiTest::bridgeSink = [](QObject *object) {
    GuiTestHub::instance()->bridge(object);
  };
  GuiTest::milestonesEnabled = true;
  if (qApp) qApp->installEventFilter(this);
}

void GuiTestHub::disable() {
  GuiTest::milestonesEnabled = false;
  if (qApp) qApp->removeEventFilter(this);
  m_count.clear();
  m_snapshot.clear();
  m_recent.clear();
  m_busy = false;
}

void GuiTestHub::raise(const QString &name, const QString &arg) {
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(
        this, [this, name, arg]() { record(name, arg); },
        Qt::QueuedConnection);
    return;
  }
  record(name, arg);
}

void GuiTestHub::record(const QString &name, const QString &arg) {
  ++m_count[name];
  if (!arg.isEmpty()) ++m_count[name + kArgSeparator + arg];
  m_recent.append(arg.isEmpty() ? name : name + " " + arg);
  while (m_recent.size() > kRecentSize) m_recent.removeFirst();
}

void GuiTestHub::bridge(QObject *object) {
  const QMetaObject *meta = object->metaObject();
  for (const BridgeEntry &entry : kBridges) {
    if (qstrcmp(meta->className(), entry.className) != 0) continue;
    const int index = meta->indexOfSignal(entry.signal);
    if (index < 0) {
      qWarning("GuiTestHub: %s has no signal %s", entry.className,
               entry.signal);
      continue;
    }
    auto relay = new BridgeRelay(entry.milestone, object);
    QObject::connect(object, meta->method(index), relay,
                     relay->metaObject()->method(
                         relay->metaObject()->indexOfSlot("fire()")));
  }
}

void GuiTestHub::takeSnapshot() { m_snapshot = m_count; }

int GuiTestHub::count(const QString &name, const QString &arg) const {
  return m_count.value(arg.isEmpty() ? name : name + kArgSeparator + arg);
}

bool GuiTestHub::reachedSinceSnapshot(const QString &name,
                                      const QString &arg) const {
  const QString key = arg.isEmpty() ? name : name + kArgSeparator + arg;
  return m_count.value(key) > m_snapshot.value(key);
}

void GuiTestHub::pollBusy() {
  const bool busy = QApplication::overrideCursor() != nullptr;
  if (busy == m_busy) return;
  m_busy = busy;
  record(busy ? "busy.on" : "busy.off", {});
}

void GuiTestHub::watchProcess(QObject *child) {
  // ChildAdded arrives while the child is still being constructed, so the
  // cast is only valid once control is back in the event loop.
  QPointer<QObject> guard{child};
  QTimer::singleShot(0, this, [this, guard]() {
    auto process = qobject_cast<QProcess *>(guard.data());
    if (!process) return;
    connect(process, &QProcess::started, this, [this, process]() {
      record("process.started", QFileInfo(process->program()).fileName());
    });
    connect(process, &QProcess::finished, this, [this, process]() {
      record("process.finished", QFileInfo(process->program()).fileName());
    });
  });
}

bool GuiTestHub::eventFilter(QObject *watched, QEvent *event) {
  switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
      if (auto w = qobject_cast<QWidget *>(watched); w && isTrackedWindow(w)) {
        const bool shown = event->type() == QEvent::Show;
        const QString prefix = qobject_cast<QDialog *>(w) ? "dialog" : "window";
        record(prefix + (shown ? ".shown" : ".closed"), w->objectName());
      }
      break;
    case QEvent::ChildAdded:
      watchProcess(static_cast<QChildEvent *>(event)->child());
      break;
    default:
      break;
  }
  pollBusy();
  return QObject::eventFilter(watched, event);
}

}  // namespace FOEDAG

#include "GuiTestHub.moc"
