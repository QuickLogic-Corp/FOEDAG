#include "DeviceGridDescriptor.h"
#include "Utils/FileUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace fp {

namespace {

// device_layout.json keys (QLDeviceLayoutInfo::writeDeviceLayoutJSON()). Already
// resolved and flat -- no DEVICE_TYPE_SETTINGS.CUSTOM-vs-flat-DEVICE_SIZE
// ambiguity left for this class to handle; QLDeviceLayoutInfo picked the right
// one, by device layout mode, before writing this file.
constexpr auto kArrayX = "array_x";
constexpr auto kArrayY = "array_y";
constexpr auto kDspSize = "dsp_size";
constexpr auto kBramSize = "bram_size";
constexpr auto kDspCols = "dsp_cols";
constexpr auto kBramCols = "bram_cols";

constexpr auto kInDeviceLayoutJson = "device_layout.json";

}  // namespace


DeviceGridDescriptor::DeviceGridDescriptor(const std::filesystem::path& deviceLayoutFile)
{
    if (parse(deviceLayoutFile)) {
        validateFit();
        Tile::setDeviceRowsNum(m_rows);
    }
}

bool DeviceGridDescriptor::parse(const std::filesystem::path& deviceLayoutFile)
{
    m_error = "";

    // Every failure below names the file, and the key when there is one: this
    // string is what the user is shown when floorplanning refuses to start.
    m_layoutPath = QString::fromStdString(deviceLayoutFile.string());
    const QString& layoutPath = m_layoutPath;

    bool readOk = false;
    const QByteArray content = QByteArray::fromStdString(
        FOEDAG::FileUtils::GetFileContent(deviceLayoutFile, &readOk));
    if (!readOk) {
        m_error = QString("cannot read %1").arg(layoutPath);
        return false;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(content, &jsonError);
    if (doc.isNull()) {
        // Not folded in with the is-not-an-object case below: valid JSON that is
        // not an object leaves jsonError saying "no error occurred".
        m_error = QString("%1: %2").arg(layoutPath, jsonError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        m_error = QString("%1: root is not a JSON object").arg(layoutPath);
        return false;
    }
    const QJsonObject layout = doc.object();

    auto stringValue = [&](const QJsonObject& object, const QString& key, QString& out) -> bool {
        const QJsonValue value = object.value(key);
        if (!value.isString()) {
            m_error = QString("`%1` string key not found in %2").arg(key, kInDeviceLayoutJson);
            return false;
        }
        out = value.toString();
        return true;
    };
    auto intValue = [&](const QJsonObject& object, const QString& key, int& out) -> bool {
        const QJsonValue value = object.value(key);
        if (!value.isDouble()) {
            m_error = QString("`%1` numeric key not found in %2").arg(key, kInDeviceLayoutJson);
            return false;
        }
        out = value.toInt();
        return true;
    };

    int arrayX = 0;
    int arrayY = 0;
    if (!intValue(layout, kArrayX, arrayX)) return false;
    if (!intValue(layout, kArrayY, arrayY)) return false;

    QString dspSizeStr;
    QString bramSizeStr;
    QString dspColsStr;
    QString bramColsStr;
    if (!stringValue(layout, kDspSize, dspSizeStr)) return false;
    if (!stringValue(layout, kBramSize, bramSizeStr)) return false;
    if (!stringValue(layout, kDspCols, dspColsStr)) return false;
    if (!stringValue(layout, kBramCols, bramColsStr)) return false;

    // The core grid; the displayed grid wraps it with one IO ring on each side.
    // Add the IO border on each side (low + core + high).
    m_columns = kBorder + arrayX + kBorder;
    m_rows = kBorder + arrayY + kBorder;

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

    const std::optional<std::set<int>> dspColumns = parseColumns(dspColsStr, kDspCols);
    if (!dspColumns) {
        return false;
    }
    m_dspColumns = dspColumns.value();

    const std::optional<std::set<int>> bramColumns = parseColumns(bramColsStr, kBramCols);
    if (!bramColumns) {
        return false;
    }
    m_bramColumns = bramColumns.value();

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

std::optional<std::set<int>> DeviceGridDescriptor::parseColumns(const QString& csv,
                                                                 const QString& key)
{
    // An empty value is the writer's spelling for "this device has zero of
    // these columns" (CompilerOpenFPGA_ql.cpp writes it out rather than
    // omitting the key), not a malformed list -- handle it as its own case
    // so the loop below only ever sees entries that must parse.
    if (csv.trimmed().isEmpty()) {
        return std::set<int>();
    }

    std::set<int> columns;
    const QStringList parts = csv.split(",");
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) {
            m_error = QString("cannot parse `%1` column index from `%2`").arg(key, csv);
            return std::nullopt;
        }
        bool ok = false;
        const int column = part.toInt(&ok);
        if (!ok) {
            m_error =
                QString("cannot parse `%1` column index from `%2`").arg(key, part);
            return std::nullopt;
        }
        // DSP_COLS/BRAM_COLS are 1-based core columns; shift into grid
        // coordinates which include the leading IO border column.
        columns.insert(column + kBorder);
    }
    return columns;
}

bool DeviceGridDescriptor::validateFit()
{
    const int nonIoRows = m_rows - 2*kBorder;
    if (nonIoRows <= 0) {
        m_error = QString("%1: number of effective clbs cannot be less than or equal to 0").arg(m_layoutPath);
        return false;
    }

    if (m_dspSize.height() <= 0) {
        m_error = QString("%1: dsp height cannot be less than or equal to 0").arg(m_layoutPath);
        return false;
    }
    if (m_bramSize.height() <= 0) {
        m_error = QString("%1: bram height cannot be less than or equal to 0").arg(m_layoutPath);
        return false;
    }

    if (nonIoRows % m_dspSize.height() != 0) {
        m_error = QString("%1: cannot fit required number of dsp blocks into a column").arg(m_layoutPath);
        return false;
    }
    if (nonIoRows % m_bramSize.height() != 0) {
        m_error = QString("%1: cannot fit required number of bram blocks into a column").arg(m_layoutPath);
        return false;
    }

    return true;
}

}  // namespace fp
