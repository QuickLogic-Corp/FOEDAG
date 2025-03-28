/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QSet>

#include "LocationCollisionDetector.h"
#include "PackagePinsModel.h"
#include "PortsModel.h"

#define PINPLANNER_EXCLUDE_USED_ITEMS

namespace FOEDAG {

class PinsBaseModel : public QObject {
  Q_OBJECT

  struct ConnectionFrame {
    QString port;
    QString pin;
  };
  
 public:
  PinsBaseModel(QObject *parent = nullptr);

  bool exists(const QString &port, const QString &pin) const;
  void update(const QString &port, const QString &pin);
  void remove(const QString &port, const QString &pin);
  QString getPort(const QString &pin) const;

  PackagePinsModel *packagePinModel() const;
  void setPackagePinModel(PackagePinsModel *newPackagePinModel);

  PortsModel *portsModel() const;
  void setPortsModel(PortsModel *newPortsModel);

  const QMap<QString, QString> &pinMap() const;

 signals:
  void portAssignmentChanged(const QString &port, const QString &pin);

  // it's hard to use portAssignmentChanged without making regressions, so let's add new signals to fix problem in isolation.
  void portAssignmentRemoved(const QString &port);
  void pinAssignmentRemoved(const QString &pin);

 private:
  QMap<QString, QString> m_pinsMap;  // key - port, value - pin
  PackagePinsModel *m_packagePinModel;
  PortsModel *m_portsModel;

  QList<ConnectionFrame> findConnectionsForPins(const QSet<QString>& pins);

public:
  void setCollisionDetector(const LocationCollisionDetectorPtr& detector) { m_collisionDetector = detector; }
private:
  LocationCollisionDetectorPtr m_collisionDetector;
#ifdef PINPLANNER_EXCLUDE_USED_ITEMS
  void invalidate();
  void invalidatePortsModel(const QSet<QString>& busyPorts);
  void invalidatePackagePinsModel(const QSet<QString>& busyPins);
  void setListModelSilently(QStringListModel* model, const QStringList& list);
#endif
};

}  // namespace FOEDAG
