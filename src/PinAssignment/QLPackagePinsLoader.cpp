#include "QLPackagePinsLoader.h"
#include "MainWindow/Session.h"

#include <Utils/QtUtils.h>
#include "IODirection.h"

#include <QSet>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QDebug>

#include <algorithm>

extern FOEDAG::Session* GlobalSession;

namespace FOEDAG {

QLPackagePinsLoader::QLPackagePinsLoader(PackagePinsModel *model, QObject *parent)
    : PackagePinsLoader(model, parent) {
}

void QLPackagePinsLoader::initHeader()
{
  if (!m_model) {
    return;
  }

  if (!m_model->header().empty()) {
    return;
  }

  bool visible = true;

  int id = 0;

  m_model->appendHeaderData(HeaderData{"Interface pin", "Interface pin", id++, visible});
  m_model->appendHeaderData(HeaderData{"Available", "How many pins are in the group", id++, visible});
  m_model->appendHeaderData(HeaderData{"Ports", "User defined ports", id++, visible});
}

void QLPackagePinsLoader::parseHeader(const QString &header)
{
  m_header.clear();
  const QStringList columns = header.split(",");
  for (const QString& column: columns) {
    m_header[column.toLower()] = m_header.size();
  }

  m_format = (m_header.contains(COLUMN_CUSTOMER_PIN_ALIAS) ||
              m_header.contains(COLUMN_PIN_DIRECTION))
             ? Format::New
             : Format::Old;
}

std::pair<bool, QString> QLPackagePinsLoader::load(const QString& pinTableFilePath) {
  m_portToPinMap.clear();
  m_pinToLocationMap.clear();

  initHeader();

  const auto &[success, content] = getFileContent(pinTableFilePath);
  if (!success) return std::make_pair(success, content);

  QStringList lines = QtUtils::StringSplit(content, '\n');
  if (lines.isEmpty()) {
    return std::make_pair(false, QString("empty file: %1").arg(pinTableFilePath));
  }
  parseHeader(lines.takeFirst());

  if (m_format == Format::New) {
    return loadNew(lines, pinTableFilePath);
  }
  return loadOld(lines, pinTableFilePath);
}

std::pair<bool, QString> QLPackagePinsLoader::loadOld(const QStringList &lines, const QString &pinTableFilePath) {
#ifdef UPSTREAM_PINPLANNER
  InternalPins &internalPins = m_model->internalPinsRef();
#endif

  if (!m_header.contains(COLUMN_ORIENTATION)) {
    return std::make_pair(false, QString("column %1 is missing, abort loading %2").arg(COLUMN_ORIENTATION).arg(pinTableFilePath));
  }
  if (!m_header.contains(COLUMN_MAPPED_PIN)) {
    return std::make_pair(false, QString("column %1 is missing, abort loading %2").arg(COLUMN_MAPPED_PIN).arg(pinTableFilePath));
  }
  if (!m_header.contains(COLUMN_PORT_NAME)) {
    return std::make_pair(false, QString("column %1 is missing, abort loading %2").arg(COLUMN_PORT_NAME).arg(pinTableFilePath));
  }

  const int columnOrientationIndex = m_header.value(COLUMN_ORIENTATION);
  const int columnMappedPinIndex = m_header.value(COLUMN_MAPPED_PIN);
  const int columnPortNameIndex = m_header.value(COLUMN_PORT_NAME);

  PackagePinGroup group{};
  QSet<QString> uniquePins;
  for (const auto &line: lines) {
    QStringList data = line.split(",");
    if (data.size() >= columnOrientationIndex) {
      QString orientation{data.at(columnOrientationIndex)};
      if (!orientation.isEmpty()) {
        if (!group.name.isEmpty() && (group.name != orientation)) {
          bool acceptGroup = false;
          if (!m_model->userGroups().empty()) {
            acceptGroup = m_model->userGroups().contains(group.name);
          } else {
            acceptGroup = true;
          }

          if (acceptGroup) {
            m_model->append(group);
          }
          group.pinData.clear();
          uniquePins.clear();
        }
        group.name = orientation;
      }
    } else {
      logWarning(QString("line [%1] doesn't contain column [%2]").arg(line).arg(COLUMN_ORIENTATION));
    }

    QStringList dataMod;
    for (int i=0; i<=InternalPinName; ++i) {
      dataMod.append("");
    }

    QString pinName;

    if (data.size() >= columnMappedPinIndex) {
      pinName = data.at(columnMappedPinIndex);
      dataMod[PinName] = pinName;
      dataMod[BallName] = pinName;
      dataMod[BallId] = pinName;
    } else {
      logWarning(QString("line [%1] doesn't contain column [%2]").arg(line).arg(COLUMN_MAPPED_PIN));
    }

    if (!pinName.isEmpty()) {
      if (uniquePins.contains(pinName)) {
        continue;
      }
      uniquePins.insert(pinName);

      static const QRegularExpression inputPortPattern{"[A-Z0-9]+_A2F\\[\\d+\\]"};
      static const QRegularExpression outputPortPattern{"[A-Z0-9]+_F2A\\[\\d+\\]"};

      QString dir;
      if (data.size() >= columnPortNameIndex) {
        QString portName = data.at(columnPortNameIndex);
        m_portToPinMap[portName] = pinName;
        if (!portName.isEmpty()) {
          if (portName.contains(inputPortPattern)) {
            dir = IODirection::INPUT;
          } else if (portName.contains(outputPortPattern)) {
            dir = IODirection::OUTPUT;
          }
        }
      } else {
        logWarning(QString("line [%1] doesn't contain column [%2]").arg(line).arg(COLUMN_PORT_NAME));
      }

      if (!dir.isEmpty()) {
        dataMod[Direction] = dir;
      }

      group.pinData.append({dataMod});
    }
  }
  if (!m_model->userGroups().contains(group.name)) {
    m_model->append(group);  // append last
  }
  m_model->initListModel();

  return std::make_pair(true, QString{});
}

std::pair<bool, QString> QLPackagePinsLoader::loadNew(const QStringList &lines, const QString &pinTableFilePath) {
  for (const QString &required : {COLUMN_NETLIST_NAME, COLUMN_PIN_TYPE,
                                  COLUMN_CUSTOMER_PIN_ALIAS, COLUMN_PIN_DIRECTION,
                                  COLUMN_ROW, COLUMN_COL, COLUMN_SUBTILE}) {
    if (!m_header.contains(required)) {
      return std::make_pair(false, QString("column %1 is missing, abort loading %2").arg(required).arg(pinTableFilePath));
    }
  }

  const int idxNetlist = m_header.value(COLUMN_NETLIST_NAME);
  const int idxPinType = m_header.value(COLUMN_PIN_TYPE);
  const int idxAlias   = m_header.value(COLUMN_CUSTOMER_PIN_ALIAS);
  const int idxDir     = m_header.value(COLUMN_PIN_DIRECTION);
  const int idxRow     = m_header.value(COLUMN_ROW);
  const int idxCol     = m_header.value(COLUMN_COL);
  const int idxSubtile = m_header.value(COLUMN_SUBTILE);

  const int neededMax = std::max({idxNetlist, idxPinType, idxAlias, idxDir, idxRow, idxCol, idxSubtile});

  struct ParsedRow { QString pinName; QString dir; int row; int col; int subtile; };
  QVector<ParsedRow> rows;
  int maxRow = 0, maxCol = 0;

  for (const auto &line: lines) {
    const QStringList data = line.split(",");
    if (data.size() <= neededMax) {
      continue;
    }

    if (data.at(idxPinType).trimmed() != QStringLiteral("GPIO")) {
      continue;
    }

    const QString netlistName = data.at(idxNetlist).trimmed();
    if (netlistName.isEmpty()) {
      continue;
    }

    const QString alias = data.at(idxAlias).trimmed();
    const QString pinName = alias.isEmpty() ? netlistName : alias;

    const QString dirStr = data.at(idxDir).trimmed().toLower();
    QString dir;
    if (dirStr == QStringLiteral("input"))       dir = IODirection::INPUT;
    else if (dirStr == QStringLiteral("output")) dir = IODirection::OUTPUT;
    else continue;

    bool okRow=false, okCol=false, okSub=false;
    const int r = data.at(idxRow).trimmed().toInt(&okRow);
    const int c = data.at(idxCol).trimmed().toInt(&okCol);
    const int s = data.at(idxSubtile).trimmed().toInt(&okSub);
    if (!okRow || !okCol || !okSub) {
      logWarning(QString("line [%1] has non-integer row/col/subtile").arg(line));
      continue;
    }

    if (r > maxRow) maxRow = r;
    if (c > maxCol) maxCol = c;

    rows.push_back({pinName, dir, r, c, s});
  }

  // Vertical sides win on corners
  auto classify = [maxRow, maxCol](int r, int c) -> QString {
    if (c == 0)      return QStringLiteral("LEFT");
    if (c == maxCol) return QStringLiteral("RIGHT");
    if (r == 0)      return QStringLiteral("BOTTOM");
    if (r == maxRow) return QStringLiteral("TOP");
    return QString();
  };

  QSet<QString> uniquePins;
  QMap<QString, PackagePinGroup> groupsBySide;
  QStringList sideOrder;

  for (const auto &row : rows) {
    const QString side = classify(row.row, row.col);
    if (side.isEmpty()) {
      logWarning(QString("pin %1 at (col=%2,row=%3) is not on any device edge, skipping").arg(row.pinName).arg(row.col).arg(row.row));
      continue;
    }
    if (uniquePins.contains(row.pinName)) {
      continue;
    }
    uniquePins.insert(row.pinName);

    QStringList dataMod;
    for (int i=0; i<=InternalPinName; ++i) {
      dataMod.append("");
    }
    dataMod[PinName] = row.pinName;
    dataMod[BallName] = row.pinName;
    dataMod[BallId] = row.pinName;
    dataMod[Direction] = row.dir;

    if (!groupsBySide.contains(side)) {
      groupsBySide[side].name = side;
      sideOrder.append(side);
    }
    groupsBySide[side].pinData.append({dataMod});

    m_pinToLocationMap[row.pinName] = QString("%1:%2:%3").arg(row.col).arg(row.row).arg(row.subtile);
  }

  for (const QString &side : sideOrder) {
    const PackagePinGroup &g = groupsBySide.value(side);
    const bool acceptGroup = m_model->userGroups().empty() ||
                             m_model->userGroups().contains(g.name);
    if (acceptGroup) {
      m_model->append(g);
    }
  }

  m_model->initListModel();
  return std::make_pair(true, QString{});
}

bool QLPackagePinsLoader::loadIOMap(const QString& ioMapFilePath)
{
  if (m_format == Format::New) {
    // m_pinToLocationMap is already populated by loadNew() from CSV columns;
    // no XML lookup is required.
    return true;
  }

  QFile file(ioMapFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    logWarning(QString("Failed to open the file %1").arg(ioMapFilePath));
    return false;
  }

  QXmlStreamReader xmlReader(&file);

  while (!xmlReader.atEnd() && !xmlReader.hasError()) {
    QXmlStreamReader::TokenType token = xmlReader.readNext();
    if (token == QXmlStreamReader::StartElement) {
      if (xmlReader.name() == "io") {
        QString port = xmlReader.attributes().value("pad").toString();
        QString x = xmlReader.attributes().value("x").toString();
        QString y = xmlReader.attributes().value("y").toString();
        QString z = xmlReader.attributes().value("z").toString();

        if (m_portToPinMap.contains(port)) {
          QString location = QString("%1:%2:%3").arg(x).arg(y).arg(z);
          QString pin{m_portToPinMap.value(port)};
          m_pinToLocationMap[pin] = location;
        }
      }
    }
  }

  if (xmlReader.hasError()) {
    logWarning(QString("%1 contains XML error: %2").arg(ioMapFilePath).arg(xmlReader.errorString()));
  }

  file.close();
  return true;
}

LocationCollisionDetectorPtr QLPackagePinsLoader::validateIOMap()
{
  LocationCollisionDetectorPtr collisionDetector = std::make_shared<LocationCollisionDetector>(m_pinToLocationMap);
  QMap<QString, QSet<QString>> overlappedLocationToPinsMap = collisionDetector->overlappedLocationToPinsMap();
  if (!overlappedLocationToPinsMap.isEmpty()) {
    logWarning(QString("%1 physical locations point to more than one pin.\nThese collisions are handled by the Pin Planner UI").arg(overlappedLocationToPinsMap.size()));
  }
  return collisionDetector;
}

void QLPackagePinsLoader::logWarning(const QString& msg) const
{
  GlobalSession->GetCompiler()->Message("[WARNING] QLPackagePinsLoader: " + msg.toStdString());
}

}  // namespace FOEDAG
