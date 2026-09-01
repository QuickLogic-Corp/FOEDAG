#include "TilesCfgParser.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFile>

#include <iostream>

namespace FOEDAG {

namespace {

// The block columns of one <fixed_layout>. add_layout.py emits one
// <region type="bram"|"dsp"> per column with startx == endx; a hand-written
// architecture may use a <col> instead. Anything whose extent is not two plain
// numbers is recorded as -1 rather than counted, because undercounting here
// understates the fabric silently.
//
// The floor planner parses the same columns in fp::DeviceGridDescriptor. It is not
// reused: its constructor mutates shared fp::Tile state and rejects an array its
// block height does not divide evenly, neither of which belongs in a caller that
// only wants a count.
void countLayoutColumns(const QDomElement& fixedLayout, TilesCfgResult& result)
{
    const auto record = [&result](const QString& type, int columns) {
        auto [it, inserted] = result.layout_columns.try_emplace(type.toStdString(), columns);
        if (!inserted) {
            it->second = ((it->second < 0) || (columns < 0)) ? -1 : (it->second + columns);
        }
    };

    for (QDomElement region = fixedLayout.firstChildElement("region"); !region.isNull();
         region = region.nextSiblingElement("region")) {
        const QString type = region.attribute("type");
        if (type.isEmpty()) {
            continue;
        }
        bool startOk = false;
        bool endOk = false;
        const int startx = region.attribute("startx").toInt(&startOk);
        const int endx = region.attribute("endx").toInt(&endOk);
        record(type, (startOk && endOk && (endx >= startx)) ? (endx - startx + 1) : -1);
    }

    for (QDomElement col = fixedLayout.firstChildElement("col"); !col.isNull();
         col = col.nextSiblingElement("col")) {
        const QString type = col.attribute("type");
        if (type.isEmpty()) {
            continue;
        }
        // repeatx spans an unknown number of columns without evaluating the layout's
        // W/H expressions, which this parser does not do.
        record(type, col.hasAttribute("repeatx") ? -1 : 1);
    }
}

void parseLayoutColumns(const QDomElement& root, const std::string& layoutName,
                        const std::filesystem::path& xmlPath, TilesCfgResult& result)
{
    QDomElement layoutElem = root.firstChildElement("layout");
    if (layoutElem.isNull()) {
        result.error = "No <layout> element in XML:" + xmlPath.string();
        return;
    }

    QDomElement only;
    int fixedLayoutCount = 0;
    for (QDomElement fixedLayout = layoutElem.firstChildElement("fixed_layout");
         !fixedLayout.isNull();
         fixedLayout = fixedLayout.nextSiblingElement("fixed_layout")) {
        fixedLayoutCount++;
        only = fixedLayout;
        if (fixedLayout.attribute("name").toStdString() == layoutName) {
            countLayoutColumns(fixedLayout, result);
            return;
        }
    }

    if (fixedLayoutCount == 1) {
        countLayoutColumns(only, result);
        return;
    }

    result.error = (fixedLayoutCount == 0)
                       ? ("No <fixed_layout> element in XML:" + xmlPath.string())
                       : ("No <fixed_layout name=\"" + layoutName + "\"> among the " +
                          std::to_string(fixedLayoutCount) + " in XML:" + xmlPath.string());
}

}  // namespace


TilesCfgResult parseTilesCfg(const std::filesystem::path& xmlPath, const std::string& layoutName)
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

    parseLayoutColumns(root, layoutName, xmlPath, tilesCfgResult);
    if (!tilesCfgResult.error.empty()) {
        return tilesCfgResult;
    }

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
