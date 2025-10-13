#include "TilesCfgParser.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFile>

#include <iostream>

namespace FOEDAG {

std::map<std::string, std::pair<int, int>> parseTilesCfg(const std::filesystem::path& xmlPath)
{
    std::map<std::string, std::pair<int, int>> tilesCfg;

    QFile file(QString::fromStdString(xmlPath.string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cout << "Failed to open file:" << xmlPath << std::endl;
        return tilesCfg;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        std::cout << "Failed to parse XML:" << xmlPath << std::endl;
        return tilesCfg;
    }

    file.close();

    QDomElement root = doc.documentElement();
    QDomElement tilesElem = root.firstChildElement("tiles");
    if (tilesElem.isNull()) {
        std::cout << "No <tiles> element in XML" << std::endl;
        return tilesCfg;
    }

    QDomElement tileElem = tilesElem.firstChildElement("tile");
    while (!tileElem.isNull()) {
        QString name = tileElem.attribute("name");
        int width = tileElem.attribute("width", "1").toInt();
        int height = tileElem.attribute("height", "1").toInt();

        if (!name.isEmpty()) {
            tilesCfg[name.toStdString()] = std::make_pair(width, height);
            std::cout << name.toStdString() << " " << width << " " << height << std::endl;
        }

        tileElem = tileElem.nextSiblingElement("tile");
    }

    return tilesCfg;   
}

}  // namespace FOEDAG
