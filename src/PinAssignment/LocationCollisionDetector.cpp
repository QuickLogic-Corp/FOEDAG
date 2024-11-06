#include "LocationCollisionDetector.h"

namespace FOEDAG {

LocationCollisionDetector::LocationCollisionDetector(const QMap<QString, QString>& pinToLocationMap) {
  QMap<QString, QSet<QString>> locationToPinsMap;
  for (auto it = pinToLocationMap.begin(); it != pinToLocationMap.end(); ++it) {
    QString pin{it.key()};
    QString location{it.value()};
    if (!locationToPinsMap.contains(location)) {
      locationToPinsMap[location] = QSet<QString>();
    } else {
      m_overlappedPinToLocationMap[pin] = location;
    }
    locationToPinsMap[location].insert(pin);
  }

  for (auto it = locationToPinsMap.begin(); it != locationToPinsMap.end(); ++it) {
    if (it.value().size() > 1) {
      m_overlappedLocationToPinsMap[it.key()] = it.value();
    }
  }
}

QSet<QString> LocationCollisionDetector::getOverlappedPins(const QString& pin) {
  QSet<QString> overlappedPins;
  if (m_overlappedPinToLocationMap.contains(pin)) {
    QString location{m_overlappedPinToLocationMap.value(pin)};
    overlappedPins = m_overlappedLocationToPinsMap.value(location);
  }
  if (!overlappedPins.isEmpty()) {
    overlappedPins.remove(pin); // to not discard current connection
  }
  return overlappedPins;
}

}  // namespace FOEDAG
