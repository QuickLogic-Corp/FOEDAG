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
#include "PinsBaseModel.h"

#include "IODirection.h"
#include <QSet>
#include <QDebug>

#define RESOLVE_LOCATION_COLLISIONS

namespace FOEDAG {

PinsBaseModel::PinsBaseModel(QObject *parent) : QObject(parent) {}

bool PinsBaseModel::exists(const QString &port, const QString &pin) const {
  auto it = m_pinsMap.find(port);
  return (it != m_pinsMap.end()) && (it.value() == pin);
}

void PinsBaseModel::update(const QString &port, const QString &pin) {
  if (port.isEmpty() && pin.isEmpty()) {
    return;
  }
#ifdef RESOLVE_LOCATION_COLLISIONS
  if (m_collisionDetector && !port.isEmpty()) {
    QSet<QString> pinsToReset = m_collisionDetector->getOverlappedPins(pin);
    if (!pinsToReset.isEmpty()) {
      pinsToReset.remove(pin); // to not discard current connection
      auto connections = findConnectionsForPins(pinsToReset);
      for (const auto& connection: connections) {
        qWarning() << "WARNING: Conflicting pin usage detected: Two different pins are attempting to occupy the same location."
                  << "The existing connection at port" << connection.port
                  << "and pin" << connection.pin
                  << "will be cleared to free up the location for the new pin" << pin;

        m_pinsMap.remove(connection.port);
        emit portAssignmentRemoved(connection.port);
        emit pinAssignmentRemoved(connection.pin);
      }
    }
  }
#endif // RESOLVE_LOCATION_COLLISIONS

  if (pin.isEmpty()) {
    auto value = m_pinsMap.value(port);
    m_pinsMap.remove(port);
    emit portAssignmentChanged(port, value);
  } else if (port.isEmpty()) {
    for (const QString& _port: m_pinsMap.keys()) {
      auto _pin = m_pinsMap.value(_port);
      if (_pin == pin) {
        m_pinsMap.remove(_port);
        emit portAssignmentChanged(_port, _pin);
        break;
      }
    }
  } else {
    if (m_pinsMap.value(port) != pin) {
      m_pinsMap.insert(port, pin);
      emit portAssignmentChanged(port, pin);
    }
  }
#ifdef PINPLANNER_EXCLUDE_USED_ITEMS
  invalidate();
#endif
}

QList<PinsBaseModel::ConnectionFrame> PinsBaseModel::findConnectionsForPins(const QSet<QString>& pins)
{
  QList<PinsBaseModel::ConnectionFrame> connections;
  for (const QString& pin: pins) {
    auto it = m_pinsMap.find(pin);
    if (it != m_pinsMap.end()) {
      connections.append(PinsBaseModel::ConnectionFrame{it.key(), pin});
    }
  }
  return connections;
}

void PinsBaseModel::remove(const QString &port, const QString &pin) {
  m_pinsMap.remove(port);
  emit portAssignmentChanged(port, QString{});
}

QString PinsBaseModel::getPort(const QString &pin) const {
  for (auto it{m_pinsMap.begin()}; it != m_pinsMap.end(); ++it) {
    if (it.value() == pin) return it.key();
  }
  return QString{};
}

PackagePinsModel *PinsBaseModel::packagePinModel() const {
  return m_packagePinModel;
}

void PinsBaseModel::setPackagePinModel(PackagePinsModel *newPackagePinModel) {
  m_packagePinModel = newPackagePinModel;
}

PortsModel *PinsBaseModel::portsModel() const { return m_portsModel; }

void PinsBaseModel::setPortsModel(PortsModel *newPortsModel) {
  m_portsModel = newPortsModel;
}

const QMap<QString, QString> &PinsBaseModel::pinMap() const {
  return m_pinsMap;
}

#ifdef PINPLANNER_EXCLUDE_USED_ITEMS
void PinsBaseModel::invalidate()
{
  QSet<QString> busyPorts;
  QSet<QString> busyPins;
  for (auto it = m_pinsMap.constBegin(); it != m_pinsMap.constEnd(); ++it) {
    QString connectedPort{it.key()};
    QString connectedPin{it.value()};
    busyPorts.insert(connectedPort);
    busyPins.insert(connectedPin);
    QSet<QString> overllapedPins = m_collisionDetector->getOverlappedPins(connectedPin);
    busyPins.unite(overllapedPins);
  }
  // qInfo() << "~~~ invalidate, busyPorts=" << busyPorts << ", busyPins=" << busyPins;

  invalidatePortsModel(busyPorts);
  invalidatePackagePinsModel(busyPins);
}

void PinsBaseModel::invalidatePortsModel(const QSet<QString>& busyPorts)
{
 const QStringList& inputPortsOrig = m_portsModel->inputPortsOrig();
  QStringList freeInputPorts;
  for (const QString& port: inputPortsOrig) {
    if (!busyPorts.contains(port)) {
      freeInputPorts.append(port);
    }
  }

  const QStringList& outputPortsOrig = m_portsModel->outputPortsOrig();
  QStringList freeOutputPorts;
  for (const QString& port: outputPortsOrig) {
    if (!busyPorts.contains(port)) {
      freeOutputPorts.append(port);
    }
  }

  setListModelSilently(m_portsModel->listModel(IODirection::INPUT), freeInputPorts);
  setListModelSilently(m_portsModel->listModel(IODirection::OUTPUT), freeOutputPorts);
}

void PinsBaseModel::invalidatePackagePinsModel(const QSet<QString>& busyPins)
{
 const QStringList& inputPinsOrig = m_packagePinModel->inputPinsOrig();
  QStringList freeInputPins;
  for (const QString& pin: inputPinsOrig) {
    if (!busyPins.contains(pin)) {
      freeInputPins.append(pin);
    }
  }

  const QStringList& outputPinsOrig = m_packagePinModel->outputPinsOrig();
  QStringList freeOutputPins;
  for (const QString& pin: outputPinsOrig) {
    if (!busyPins.contains(pin)) {
      freeOutputPins.append(pin);
    }
  }

  setListModelSilently(m_packagePinModel->listModel(IODirection::INPUT), freeInputPins);
  setListModelSilently(m_packagePinModel->listModel(IODirection::OUTPUT), freeOutputPins);
}

void PinsBaseModel::setListModelSilently(QStringListModel* model, const QStringList& list)
{
  model->blockSignals(true);
  model->setStringList(list);
  model->blockSignals(false);
}
#endif // PINPLANNER_EXCLUDE_USED_ITEMS

}  // namespace FOEDAG
