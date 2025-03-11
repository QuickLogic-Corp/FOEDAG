#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QSet>

#include <memory>

namespace FOEDAG {

class LocationCollisionDetector {
public:
  LocationCollisionDetector(const QMap<QString, QString>& pinToLocationMap);

  const QMap<QString, QSet<QString>>& overlappedLocationToPinsMap() const { return m_overlappedLocationToPinsMap; }
  QSet<QString> getOverlappedPins(const QString& pin) const;
  QString getPhysicalLocation(const QString& pin) const;

private:
  QMap<QString, QString> m_overlappedPinToLocationMap;
  QMap<QString, QSet<QString>> m_overlappedLocationToPinsMap;
};
using LocationCollisionDetectorPtr = std::shared_ptr<LocationCollisionDetector>;

}  // namespace FOEDAG
