#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>

#include "LocationCollisionDetector.h"
#include "PackagePinsLoader.h"

namespace FOEDAG {

class QLPackagePinsLoader : public PackagePinsLoader {
  const QString COLUMN_ORIENTATION = "orientation";
  const QString COLUMN_PORT_NAME = "port_name";
  const QString COLUMN_MAPPED_PIN = "mapped_pin";

 public:
  QLPackagePinsLoader(PackagePinsModel *model, QObject *parent = nullptr);
  std::pair<bool, QString> load(const QString& pinTableFilePath) override final;
  LocationCollisionDetectorPtr validateIOMap(const QString& ioMapFilePath);

private:
  void initHeader();
  void parseHeader(const QString &header);
  void checkContent();

  QMap<QString, int> m_header;
  QMap<QString, QString> m_portToPinMap;

  void logWarning(const QString& msg) const;
};

}  // namespace FOEDAG
