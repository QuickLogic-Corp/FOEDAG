#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>

#include "PackagePinsLoader.h"

namespace FOEDAG {

  class LocationCollisionDetector {
  public:
    LocationCollisionDetector(const QMap<QString, QString>& pinToLocationMap) {
      QMap<QString, QList<QString>> locationToPinsMap;
      for (auto it = pinToLocationMap.begin(); it != pinToLocationMap.end(); ++it) {
        QString pin{it.key()};
        QString location{it.value()};
        if (!locationToPinsMap.contains(location)) {
          locationToPinsMap[location] = QList<QString>();
        } else {
          m_overlappedPinToLocationMap[pin] = location;
        }
        locationToPinsMap[location].append(pin);
      }

      for (auto it = locationToPinsMap.begin(); it != locationToPinsMap.end(); ++it) {
        if (it.value().size() > 1) {
          m_overlappedLocationToPinsMap[it.key()] = it.value();
        }
      }
    }

    const QMap<QString, QList<QString>>& overlappedLocationToPinsMap() const { return m_overlappedLocationToPinsMap; }

    QList<QString> getOverlappedPins(const QString& pin) {
      if (m_overlappedPinToLocationMap.contains(pin)) {
        QString location{m_overlappedPinToLocationMap.value(pin)};
        return m_overlappedLocationToPinsMap.value(location);
      }
      return QList<QString>{};
    }

  private:
    QMap<QString, QString> m_overlappedPinToLocationMap;
    QMap<QString, QList<QString>> m_overlappedLocationToPinsMap;
  };

class QLPackagePinsLoader : public PackagePinsLoader {
  const QString COLUMN_ORIENTATION = "orientation";
  const QString COLUMN_PORT_NAME = "port_name";
  const QString COLUMN_MAPPED_PIN = "mapped_pin";

 public:
  QLPackagePinsLoader(PackagePinsModel *model, QObject *parent = nullptr);
  std::pair<bool, QString> load(const QString& pinTableFilePath) override final;
  void validateIOMap(const QString& ioMapFilePath) override final;

private:
  void initHeader();
  void parseHeader(const QString &header);
  void checkContent();

  QMap<QString, int> m_header;
  QMap<QString, QString> m_portToPinMap;
};

}  // namespace FOEDAG
