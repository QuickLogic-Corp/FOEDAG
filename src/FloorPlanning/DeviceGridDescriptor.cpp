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

    const QByteArray content = QByteArray::fromStdString(
        FOEDAG::FileUtils::GetFileContent(deviceConfigFile));

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(content, &jsonError);
    if (doc.isNull() || !doc.isObject()) {
        m_error = "config.json parse error: " + jsonError.errorString();
        return false;
    }
    const QJsonObject config = doc.object();

    auto stringValue = [&](const QString& key, QString& out) -> bool {
        const QJsonValue value = config.value(key);
        if (!value.isString()) {
            m_error = QString("`%1` string key not found in config.json").arg(key);
            return false;
        }
        out = value.toString();
        return true;
    };

    QString deviceSizeStr;
    QString dspSizeStr;
    QString bramSizeStr;
    QString dspColsStr;
    QString bramColsStr;
    if (!stringValue("DEVICE_SIZE", deviceSizeStr)) return false;
    if (!stringValue("DSP_SIZE", dspSizeStr)) return false;
    if (!stringValue("BRAM_SIZE", bramSizeStr)) return false;
    if (!stringValue("DSP_COLS", dspColsStr)) return false;
    if (!stringValue("BRAM_COLS", bramColsStr)) return false;

    // DEVICE_SIZE is the core grid; the displayed grid wraps it with one IO
    // ring on each side.
    const std::optional<QSize> coreSize = parseSize(deviceSizeStr);
    if (!coreSize) {
        return false;
    }
    // Add the IO border on each side (low + core + high).
    m_columns = kBorder + coreSize->width() + kBorder;
    m_rows = kBorder + coreSize->height() + kBorder;

    const std::optional<QSize> dspSize = parseSize(dspSizeStr);
    if (!dspSize) {
        return false;
    }
    m_dspSize = dspSize.value();

    const std::optional<QSize> bramSize = parseSize(bramSizeStr);
    if (!bramSize) {
        return false;
    }
    m_bramSize = bramSize.value();

    if (!parseColumns(dspColsStr, m_dspColumns)) {
        return false;
    }
    if (!parseColumns(bramColsStr, m_bramColumns)) {
        return false;
    }

    return true;
}

std::optional<QSize> DeviceGridDescriptor::parseSize(const QString& sizeStr)
{
    const QStringList parts = sizeStr.split("x");
    if (parts.size() != 2) {
        m_error = QString("cannot parse size from `%1`").arg(sizeStr);
        return std::nullopt;
    }

    bool okWidth = false;
    bool okHeight = false;
    const int width = parts.at(0).trimmed().toInt(&okWidth);
    const int height = parts.at(1).trimmed().toInt(&okHeight);
    if (!okWidth || !okHeight) {
        m_error = QString("cannot parse size from `%1`").arg(sizeStr);
        return std::nullopt;
    }

    return QSize(width, height);
}

bool DeviceGridDescriptor::parseColumns(const QString& csv, std::set<int>& columns)
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
            m_error = QString("cannot parse column index from `%1`").arg(part);
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
