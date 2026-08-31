#include "DeviceGridDescriptor.h"
#include "Utils/FileUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace fp {

DeviceGridDescriptor::DeviceGridDescriptor(const std::filesystem::path& deviceConfigFile)
{
    if (parse(deviceConfigFile)) {
        validateFit();
    }
    Tile::setDeviceRowsNum(m_rows);
}

bool DeviceGridDescriptor::parse(const std::filesystem::path& deviceConfigFile)
{
    m_error = "";

    // Every failure below names the file, and the key when there is one: this
    // string is what the user is shown when floorplanning refuses to start.
    m_configPath = QString::fromStdString(deviceConfigFile.string());
    const QString& configPath = m_configPath;

    bool readOk = false;
    const QByteArray content = QByteArray::fromStdString(
        FOEDAG::FileUtils::GetFileContent(deviceConfigFile, &readOk));
    if (!readOk) {
        m_error = QString("cannot read %1").arg(configPath);
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(content, &jsonError);
    if (doc.isNull()) {
        // Not folded in with the is-not-an-object case below: valid JSON that is
        // not an object leaves jsonError saying "no error occurred".
        m_error = QString("%1: %2").arg(configPath, jsonError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        m_error = QString("%1: root is not a JSON object").arg(configPath);
        return false;
    }
    const QJsonObject config = doc.object();

    auto stringValue = [&](const QJsonObject& object, const QString& key,
                           const QString& where, QString& out) -> bool {
        const QJsonValue value = object.value(key);
        if (!value.isString()) {
            m_error = QString("`%1` string key not found in %2").arg(key, where);
            return false;
        }
        out = value.toString();
        return true;
    };

    // A resizable device (`DEVICE_TYPE: CUSTOM`) states its geometry under
    // DEVICE_TYPE_SETTINGS.CUSTOM, and that block is what a per-layout
    // config.json carries. The flat keys are the older spelling and still the
    // only one on 22 of device_data's 24 devices, so they stay the fallback
    // until every device carries a CUSTOM block. This mirrors
    // get_core_dimensions() in generate_floorplanning.py -- the widget and the
    // constraints it produces must be sized off the same block.
    const QJsonObject deviceTypeSettings =
        config.value("DEVICE_TYPE_SETTINGS").toObject();
    const bool hasCustom = deviceTypeSettings.contains("CUSTOM");
    const QJsonObject custom = deviceTypeSettings.value("CUSTOM").toObject();
    const QString customWhere = "`DEVICE_TYPE_SETTINGS.CUSTOM`";

    QString dspSizeStr;
    QString bramSizeStr;
    QString dspColsStr;
    QString bramColsStr;
    std::optional<QSize> coreSize;

    if (hasCustom) {
        // Present but incomplete is malformed, not old: report the key rather
        // than falling back to a stale DEVICE_SIZE.
        QString arrayX;
        QString arrayY;
        if (!stringValue(custom, "ARRAY_X", customWhere, arrayX)) return false;
        if (!stringValue(custom, "ARRAY_Y", customWhere, arrayY)) return false;
        if (!stringValue(custom, "DSP_COLS", customWhere, dspColsStr)) return false;
        if (!stringValue(custom, "BRAM_COLS", customWhere, bramColsStr)) return false;
        coreSize = parseSize(arrayX + "x" + arrayY, "ARRAY_X`/`ARRAY_Y");
    } else {
        QString deviceSizeStr;
        if (!stringValue(config, "DEVICE_SIZE", "config.json", deviceSizeStr)) return false;
        if (!stringValue(config, "DSP_COLS", "config.json", dspColsStr)) return false;
        if (!stringValue(config, "BRAM_COLS", "config.json", bramColsStr)) return false;
        coreSize = parseSize(deviceSizeStr, "DEVICE_SIZE");
    }
    if (!coreSize) {
        return false;
    }

    // Tile sizes are a property of the tile, not of the layout, so they stay
    // top-level whichever spelling supplied the core size.
    if (!stringValue(config, "DSP_SIZE", "config.json", dspSizeStr)) return false;
    if (!stringValue(config, "BRAM_SIZE", "config.json", bramSizeStr)) return false;

    // The core grid; the displayed grid wraps it with one IO ring on each side.
    // Add the IO border on each side (low + core + high).
    m_columns = kBorder + coreSize->width() + kBorder;
    m_rows = kBorder + coreSize->height() + kBorder;

    const std::optional<QSize> dspSize = parseSize(dspSizeStr, "DSP_SIZE");
    if (!dspSize) {
        return false;
    }
    m_dspSize = dspSize.value();

    const std::optional<QSize> bramSize = parseSize(bramSizeStr, "BRAM_SIZE");
    if (!bramSize) {
        return false;
    }
    m_bramSize = bramSize.value();

    if (!parseColumns(dspColsStr, m_dspColumns, "DSP_COLS")) {
        return false;
    }
    if (!parseColumns(bramColsStr, m_bramColumns, "BRAM_COLS")) {
        return false;
    }

    return true;
}

std::optional<QSize> DeviceGridDescriptor::parseSize(const QString& sizeStr,
                                                     const QString& key)
{
    const QStringList parts = sizeStr.split("x");
    if (parts.size() != 2) {
        m_error = QString("cannot parse `%1` from `%2`").arg(key, sizeStr);
        return std::nullopt;
    }

    bool okWidth = false;
    bool okHeight = false;
    const int width = parts.at(0).trimmed().toInt(&okWidth);
    const int height = parts.at(1).trimmed().toInt(&okHeight);
    if (!okWidth || !okHeight) {
        m_error = QString("cannot parse `%1` from `%2`").arg(key, sizeStr);
        return std::nullopt;
    }

    return QSize(width, height);
}

bool DeviceGridDescriptor::parseColumns(const QString& csv,
                                        std::set<int>& columns,
                                        const QString& key)
{
    const QStringList parts = csv.split(",");
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) {
            continue;
        }
        bool ok = false;
        const int column = part.toInt(&ok);
        if (!ok) {
            m_error =
                QString("cannot parse `%1` column index from `%2`").arg(key, part);
            return false;
        }
        // DSP_COLS/BRAM_COLS are 1-based core columns; shift into grid
        // coordinates which include the leading IO border column.
        columns.insert(column + kBorder);
    }
    return true;
}

bool DeviceGridDescriptor::validateFit()
{
    int ioRow = 1;

    const int nonIoRows = m_rows - 2*ioRow;
    if (nonIoRows <= 0) {
        m_error = QString("%1: number of effective clbs cannot be less than or equal to 0").arg(m_configPath);
        return false;
    }

    if (m_dspSize.height() <= 0) {
        m_error = QString("%1: dsp height cannot be less than or equal to 0").arg(m_configPath);
        return false;
    }
    if (m_bramSize.height() <= 0) {
        m_error = QString("%1: bram height cannot be less than or equal to 0").arg(m_configPath);
        return false;
    }

    if (nonIoRows % m_dspSize.height() != 0) {
        m_error = QString("%1: cannot fit required number of dsp blocks into a column").arg(m_configPath);
        return false;
    }
    if (nonIoRows % m_bramSize.height() != 0) {
        m_error = QString("%1: cannot fit required number of bram blocks into a column").arg(m_configPath);
        return false;
    }

    return true;
}

}  // namespace fp
