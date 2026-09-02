#include "TilesCfgParser.h"

#include <set>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>

#include <iostream>

namespace FOEDAG {

TilesCfgResult parseTilesCfg(const std::filesystem::path& xmlPath)
{
    TilesCfgResult tilesCfgResult;

    QFile file(QString::fromStdString(xmlPath.string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        tilesCfgResult.error = "Failed to open file:" + xmlPath.string();
        return tilesCfgResult;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        tilesCfgResult.error = "Failed to parse XML:" + xmlPath.string();
        return tilesCfgResult;
    }

    file.close();

    QDomElement root = doc.documentElement();
    QDomElement tilesElem = root.firstChildElement("tiles");
    if (tilesElem.isNull()) {
        tilesCfgResult.error = "No <tiles> element in XML:" + xmlPath.string();
        return tilesCfgResult;
    }

    QDomElement tileElem = tilesElem.firstChildElement("tile");
    while (!tileElem.isNull()) {
        QString name = tileElem.attribute("name");
        int width = tileElem.attribute("width", "1").toInt();
        int height = tileElem.attribute("height", "1").toInt();

        if (!name.isEmpty()) {
            tilesCfgResult.tiles_cfg[name.toStdString()] = std::make_pair(width, height);
        }

        tileElem = tileElem.nextSiblingElement("tile");
    }

    return tilesCfgResult;
}

}  // namespace FOEDAG
