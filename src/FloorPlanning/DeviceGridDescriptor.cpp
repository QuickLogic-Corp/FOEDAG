#include "DeviceGridDescriptor.h"
#include "Utils/FileUtils.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QDebug>

namespace fp {

DeviceGridDescriptor::DeviceGridDescriptor(const std::filesystem::path& architectureFile, const std::string& targetLayoutName)
{
    parse(architectureFile, targetLayoutName);
    validateFit();
    Tile::setDeviceRowsNum(m_rows);
}

bool DeviceGridDescriptor::parse(const std::filesystem::path& architectureFile, const std::string& targetLayoutName)
{
    m_error = "";

    QDomDocument doc;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto parseResult = doc.setContent(QByteArray::fromStdString(FOEDAG::FileUtils::GetFileContent(architectureFile)));
    if (!parseResult) {
        m_error = "XML parse error at " + QString::number(parseResult.errorLine) + ":" + QString::number(parseResult.errorColumn) + "-" + parseResult.errorMessage;
        return false;
    }
#else
    QString errMsg;
    int errLine = 0;
    int errCol = 0;
    if (!doc.setContent(QByteArray::fromStdString(FOEDAG::FileUtils::GetFileContent(architectureFile)), &errMsg, &errLine, &errCol)) {
        m_error = "XML parse error at " + QString::number(errLine) + ":" + QString::number(errCol) + "-" + errMsg;
        return false;
    }
#endif

    if (!parseLayout(doc, targetLayoutName)) {
        return false;
    }

    if (!parseTileSizes(doc)) {
        return false;
    }

    return true;
}

bool DeviceGridDescriptor::parseLayout(const QDomDocument& doc, const std::string& targetLayoutName)
{
    QDomElement layout = doc.documentElement().firstChildElement("layout");
    if (layout.isNull()) {
        m_error = "<layout></layout> element is not found";
        return false;
    }

    const int borderColumn = 1;
    const int borderRow = 1;

    for (QDomElement fixedLayout = layout.firstChildElement("fixed_layout");
         !fixedLayout.isNull();
         fixedLayout = fixedLayout.nextSiblingElement("fixed_layout"))
    {
        const QString layoutName = fixedLayout.attribute("name");
        if (layoutName.toStdString() == targetLayoutName) {
            std::optional<QSize> size = parseSize(fixedLayout.attribute("width"), fixedLayout.attribute("height"));
            if (!size) {
                return false;
            }
            m_columns = size->width() - 2*borderColumn;
            m_rows = size->height() - 2*borderRow;

            for (QDomElement region = fixedLayout.firstChildElement("region");
                 !region.isNull();
                 region = region.nextSiblingElement("region"))
            {
                const QString regionType = region.attribute("type");
                if ((regionType != "dsp") && (regionType != "bram")) {
                    continue;
                }
                const QString startx = region.attribute("startx");
                const QString endx = region.attribute("endx");
                //const QString starty = region.attribute("starty");
                //const QString endy = region.attribute("endy");

                bool ok = false;
                if (startx == endx) {
                    int column = startx.toInt(&ok);
                    if (ok) {
                        if (regionType == "dsp") {
                            m_dspColumns.insert(column);
                        } if (regionType == "bram") {
                            m_bramColumns.insert(column);
                        }
                    } else {
                        m_error = "startx attribute is not a numeric";
                        return false;
                    }
                } else {
                    m_error = "dsp and bram expected to be column aligned, not row aligned";
                    return false;
                }
            }
        }
    }

    return true;
}


bool DeviceGridDescriptor::parseTileSizes(const QDomDocument& doc)
{
    QDomElement tiles = doc.documentElement().firstChildElement("tiles");
    if (tiles.isNull()) {
        m_error = "<tiles>...</tiles> element is not found";
        return false;
    }

    for (QDomElement tile = tiles.firstChildElement("tile");
         !tile.isNull();
         tile = tile.nextSiblingElement("tile"))
    {
        QString name = tile.attribute("name");
        if ((name != "dsp") && (name != "bram")) {
            continue;
        }
        std::optional<QSize> size = parseSize(tile.attribute("width"), tile.attribute("height"));
        if (!size) {
            return false;
        }
        if (name == "dsp") {
            m_dspSize = size.value();
        } else if (name == "bram") {
            m_bramSize = size.value();
        }
    }
    return true;
}

std::optional<QSize> DeviceGridDescriptor::parseSize(const QString& width, const QString& height)
{
    bool ok = false;
    int w = width.toInt(&ok);
    if (!ok) {
        m_error = QString("cannot parse int width from str %1").arg(width);
        return std::nullopt;
    }
    int h = height.toInt(&ok);
    if (!ok) {
        m_error = QString("cannot parse int height from str %1").arg(width);
        return std::nullopt;
    }
    return QSize(w, h);
}

bool DeviceGridDescriptor::validateFit()
{
    int ioRow = 1;

    const int nonIoRows = m_rows - 2*ioRow;
    if (nonIoRows <= 0) {
        m_error = "number of effective clbs cannot be less than or equal to 0";
        return false;
    }

    if (m_dspSize.height() <= 0) {
        m_error = "dsp height cannot be less than or equal to 0";
        return false;
    }
    if (m_bramSize.height() <= 0) {
        m_error = "bram height cannot be less than or equal to 0";
        return false;
    }

    if (nonIoRows % m_dspSize.height() != 0) {
        m_error = "cannot fit required number of dsp blocks into a column";
        return false;
    }
    if (nonIoRows % m_bramSize.height() != 0) {
        m_error = "cannot fit required number of bram blocks into a column";
        return false;
    }

    return true;
}

}  // namespace fp
