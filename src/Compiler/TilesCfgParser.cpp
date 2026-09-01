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


LayoutGeometryResult parseLayoutGeometry(const std::filesystem::path& xmlPath,
                                         const std::string& layoutName)
{
    LayoutGeometryResult result;

    QFile file(QString::fromStdString(xmlPath.string()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = "Failed to open file:" + xmlPath.string();
        return result;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        result.error = "Failed to parse XML:" + xmlPath.string();
        return result;
    }
    file.close();

    QDomElement layoutElem = doc.documentElement().firstChildElement("layout");
    if (layoutElem.isNull()) {
        result.error = "No <layout> element in XML:" + xmlPath.string();
        return result;
    }

    QDomElement chosen;
    for (QDomElement fixedLayout = layoutElem.firstChildElement("fixed_layout");
         !fixedLayout.isNull();
         fixedLayout = fixedLayout.nextSiblingElement("fixed_layout")) {
        if (chosen.isNull()) {
            chosen = fixedLayout;
        }
        if (fixedLayout.attribute("name").toStdString() == layoutName) {
            chosen = fixedLayout;
            break;
        }
    }
    if (chosen.isNull()) {
        result.error = "No <fixed_layout> element in XML:" + xmlPath.string();
        return result;
    }

    bool widthOk = false;
    bool heightOk = false;
    const int width = chosen.attribute("width").toInt(&widthOk);
    const int height = chosen.attribute("height").toInt(&heightOk);

    // the array is the layout less one io and one empty tile per side
    const int LAYOUT_RING_TILES = 4;
    if (!widthOk || !heightOk || (width <= LAYOUT_RING_TILES) || (height <= LAYOUT_RING_TILES)) {
        result.error = "<fixed_layout> of " + xmlPath.string() + " has no usable width/height";
        return result;
    }
    result.array_x = width - LAYOUT_RING_TILES;
    result.array_y = height - LAYOUT_RING_TILES;

    const auto columnsOfType = [&chosen](const QString& type) {
        std::set<int> columns;
        for (QDomElement region = chosen.firstChildElement("region"); !region.isNull();
             region = region.nextSiblingElement("region")) {
            if (region.attribute("type") != type) {
                continue;
            }
            bool ok = false;
            const int startx = region.attribute("startx").toInt(&ok);
            if (ok) {
                // BRAM_COLS/DSP_COLS are one lower than the region they produce
                columns.insert(startx - 1);
            }
        }
        std::string text;
        for (int column : columns) {
            if (!text.empty()) {
                text += ",";
            }
            text += std::to_string(column);
        }
        return text;
    };

    result.bram_cols = columnsOfType("bram");
    result.dsp_cols = columnsOfType("dsp");

    return result;
}

}  // namespace FOEDAG
