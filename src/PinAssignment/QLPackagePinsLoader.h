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
  // Old-format columns
  const QString COLUMN_ORIENTATION = "orientation";
  const QString COLUMN_PORT_NAME = "port_name";
  const QString COLUMN_MAPPED_PIN = "mapped_pin";

  // New-format columns
  const QString COLUMN_NETLIST_NAME = "netlist_name";
  const QString COLUMN_PIN_TYPE = "pin_type";
  const QString COLUMN_CUSTOMER_PIN_ALIAS = "customer_pin_alias";
  const QString COLUMN_PIN_DIRECTION = "pin_direction";
  const QString COLUMN_ROW = "row";
  const QString COLUMN_COL = "col";
  const QString COLUMN_SUBTILE = "subtile";

 public:
  enum class Format { Old, New };

  QLPackagePinsLoader(PackagePinsModel *model, QObject *parent = nullptr);
  std::pair<bool, QString> load(const QString& pinTableFilePath) override final;

  // Populate m_pinToLocationMap from the fpga_io_map.xml. Only meaningful for
  // the old CSV format; for the new format the map is already filled by
  // loadNew() from CSV columns, so this is a no-op. Returns false if the XML
  // file cannot be opened.
  bool loadIOMap(const QString& ioMapFilePath);

  // Build a LocationCollisionDetector from m_pinToLocationMap. Works for both
  // formats: call loadIOMap() first for the old format; the new format
  // populates m_pinToLocationMap during load().
  LocationCollisionDetectorPtr validateIOMap();

private:
  void initHeader();
  void parseHeader(const QString &header);
  void checkContent();

  std::pair<bool, QString> loadOld(const QStringList &lines, const QString &filePath);
  std::pair<bool, QString> loadNew(const QStringList &lines, const QString &filePath);

  QMap<QString, int> m_header;
  QMap<QString, QString> m_portToPinMap;     // old format: port_name -> pin (XML lookup key)
  QMap<QString, QString> m_pinToLocationMap; // pin -> "x:y:z"; populated by loadNew() (new format)
                                             // or loadIOMap() (old format)
  Format m_format{Format::Old};

  void logWarning(const QString& msg) const;
};

}  // namespace FOEDAG
