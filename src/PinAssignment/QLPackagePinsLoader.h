#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>

#include "PackagePinsLoader.h"

namespace FOEDAG {

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
  QSet<QString> m_portNames;
};

}  // namespace FOEDAG
