#pragma once

#include "Tile.h"

#include <QSize>
#include <QString>

#include <set>
#include <memory>
#include <filesystem>
#include <optional>

namespace fp {

class DeviceGridDescriptor {
public:
    DeviceGridDescriptor(const std::filesystem::path& deviceLayoutFile);

    bool hasError() const { return !m_error.isEmpty(); }
    const QString& error() const { return m_error; }

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    // Unset when the device carries no such block: the package states no
    // footprint for a block that is not there.
    const std::optional<QSize>& dspSize() const { return m_dspSize; }
    const std::optional<QSize>& bramSize() const { return m_bramSize; }

    bool isDspColumn(int column) const { return m_dspColumns.find(column) != m_dspColumns.end(); }
    bool isBramColumn(int column) const { return m_bramColumns.find(column) != m_bramColumns.end(); }

    // Whether the fabric carries the block at all. Check before reading its
    // footprint: the size is known only for a block that is there.
    bool isDspSupported() const { return m_dspSize.has_value(); }
    bool isBramSupported() const { return m_bramSize.has_value(); }

    QSize elementSize(Tile::Type type) const {
        QSize minSize(1,1);
        switch(type) {
        case Tile::Type::Io: return minSize;
        case Tile::Type::Clb: return minSize;
        // Only ever asked for a block the device has - callers gate on
        // isBramSupported()/isDspSupported() - so the fallback is unreachable
        // rather than a size claim.
        case Tile::Type::Bram: return m_bramSize.value_or(minSize);
        case Tile::Type::Dsp: return m_dspSize.value_or(minSize);
        default: return minSize;
        }
        return minSize;
    }

    bool validateFit();

    // Number of border (IO) cells the displayed grid adds around the device
    // core on each side. device_layout.json's array_x/array_y are the core
    // grid; the grid wraps it with one IO ring, so columns()/rows() = core +
    // 2*kBorder and the 1-based bram_cols/dsp_cols core columns are shifted by
    // kBorder into grid coordinates.
    static constexpr int kBorder = 1;

private:
    QString m_error;
    // Path of the parsed device_layout.json, so validateFit() can name it too.
    QString m_layoutPath;
    int m_columns = -1;
    int m_rows = -1;
    std::set<int> m_dspColumns;
    std::set<int> m_bramColumns;
    std::optional<QSize> m_dspSize;
    std::optional<QSize> m_bramSize;

    bool parse(const std::filesystem::path& deviceLayoutFile);

    std::optional<QSize> parseSize(const QString& sizeStr, const QString& key);
    std::optional<std::set<int>> parseColumns(const QString& csv, const QString& key);
};
using DeviceGridDescriptorPtr = std::shared_ptr<DeviceGridDescriptor>;

}  // namespace fp
