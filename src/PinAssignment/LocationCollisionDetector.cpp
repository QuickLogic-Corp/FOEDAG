#include "LocationCollisionDetector.h"
namespace FOEDAG {

LocationCollisionDetector::LocationCollisionDetector(const QMap<QString, QString>& pinToLocationMap) {
  QMap<QString, QSet<QString>> locationToPinsMap;
  for (auto it = pinToLocationMap.begin(); it != pinToLocationMap.end(); ++it) {
    QString pin{it.key()};
    QString location{it.value()};

    if (!locationToPinsMap.contains(location)) {
      locationToPinsMap[location] = QSet<QString>();
    }
    locationToPinsMap[location].insert(pin);
  }

  for (auto it = locationToPinsMap.begin(); it != locationToPinsMap.end(); ++it) {
    if (it.value().size() > 1) {
      QString location{it.key()};
      QSet<QString> pins{it.value()};
      m_overlappedLocationToPinsMap[location] = pins;
      for (const QString& pin: pins) {
        m_overlappedPinToLocationMap[pin] = location;
      }
    }
  }
}

QSet<QString> LocationCollisionDetector::getOverlappedPins(const QString& pin) const {
  QSet<QString> overlappedPins;
  if (m_overlappedPinToLocationMap.contains(pin)) {
    QString location{m_overlappedPinToLocationMap.value(pin)};
    overlappedPins = m_overlappedLocationToPinsMap.value(location);
  }
  return overlappedPins;
}

QString LocationCollisionDetector::getOverlappedLocation(const QString& pin) const {
  QString location{};
  if (m_overlappedPinToLocationMap.contains(pin)) {
    location = m_overlappedPinToLocationMap.value(pin);
  }
  return location;
}

}  // namespace FOEDAG
