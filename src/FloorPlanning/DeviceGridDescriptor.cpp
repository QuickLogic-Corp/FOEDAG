#include "DeviceGridDescriptor.h"
#include "Utils/FileUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace fp {

namespace {

// config.json keys. Named once because several are read under both spellings --
// DSP_COLS and BRAM_COLS live either at the top level or inside the CUSTOM
// block -- and a typo in one of a matched pair is the kind of thing that reads
// as "device has no DSP columns" rather than as an error.
constexpr auto kDeviceTypeSettings = "DEVICE_TYPE_SETTINGS";
constexpr auto kCustom = "CUSTOM";
constexpr auto kArrayX = "ARRAY_X";
constexpr auto kArrayY = "ARRAY_Y";
constexpr auto kDeviceSize = "DEVICE_SIZE";
constexpr auto kDspSize = "DSP_SIZE";
constexpr auto kBramSize = "BRAM_SIZE";
constexpr auto kDspCols = "DSP_COLS";
constexpr auto kBramCols = "BRAM_COLS";

// Where a key was looked for, for the error message. Plain prose: the message
// format quotes the key, not the place.
constexpr auto kInConfigJson = "config.json";
constexpr auto kInCustomBlock = "DEVICE_TYPE_SETTINGS.CUSTOM";

}  // namespace


DeviceGridDescriptor::DeviceGridDescriptor(const std::filesystem::path& deviceConfigFile)
{
    if (parse(deviceConfigFile)) {
        validateFit();
        Tile::setDeviceRowsNum(m_rows);
    }
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
        config.value(kDeviceTypeSettings).toObject();
    const bool hasCustom = deviceTypeSettings.contains(kCustom);
    const QJsonObject custom = deviceTypeSettings.value(kCustom).toObject();

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
        if (!stringValue(custom, kArrayX, kInCustomBlock, arrayX)) return false;
        if (!stringValue(custom, kArrayY, kInCustomBlock, arrayY)) return false;
        if (!stringValue(custom, kDspCols, kInCustomBlock, dspColsStr)) return false;
        if (!stringValue(custom, kBramCols, kInCustomBlock, bramColsStr)) return false;
        coreSize = parseSize(arrayX + "x" + arrayY,
                             QString("%1/%2").arg(kArrayX, kArrayY));
    } else {
        QString deviceSizeStr;
        if (!stringValue(config, kDeviceSize, kInConfigJson, deviceSizeStr)) return false;
        if (!stringValue(config, kDspCols, kInConfigJson, dspColsStr)) return false;
        if (!stringValue(config, kBramCols, kInConfigJson, bramColsStr)) return false;
        coreSize = parseSize(deviceSizeStr, kDeviceSize);
    }
    if (!coreSize) {
        return false;
    }

    // Tile sizes are a property of the tile, not of the layout, so they stay
    // top-level whichever spelling supplied the core size.
    if (!stringValue(config, kDspSize, kInConfigJson, dspSizeStr)) return false;
    if (!stringValue(config, kBramSize, kInConfigJson, bramSizeStr)) return false;

    // The core grid; the displayed grid wraps it with one IO ring on each side.
    // Add the IO border on each side (low + core + high).
    m_columns = kBorder + coreSize->width() + kBorder;
    m_rows = kBorder + coreSize->height() + kBorder;

    const std::optional<QSize> dspSize = parseSize(dspSizeStr, kDspSize);
    if (!dspSize) {
        return false;
    }
    m_dspSize = dspSize.value();

    const std::optional<QSize> bramSize = parseSize(bramSizeStr, kBramSize);
    if (!bramSize) {
        return false;
    }
    m_bramSize = bramSize.value();

    if (!parseColumns(dspColsStr, m_dspColumns, kDspCols)) {
        return false;
    }
    if (!parseColumns(bramColsStr, m_bramColumns, kBramCols)) {
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
    // An empty value is the writer's spelling for "this device has zero of
    // these columns" (CompilerOpenFPGA_ql.cpp writes it out rather than
    // omitting the key), not a malformed list -- handle it as its own case
    // so the loop below only ever sees entries that must parse.
    if (csv.trimmed().isEmpty()) {
        return true;
    }

    const QStringList parts = csv.split(",");
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) {
            m_error = QString("cannot parse `%1` column index from `%2`").arg(key, csv);
            return false;
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
    const int nonIoRows = m_rows - 2*kBorder;
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
