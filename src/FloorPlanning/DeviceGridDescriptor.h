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
    DeviceGridDescriptor(const std::filesystem::path& deviceConfigFile);

    bool hasError() const { return !m_error.isEmpty(); }
    const QString& error() const { return m_error; }

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    const QSize& dspSize() const { return m_dspSize; }
    const QSize& bramSize() const { return m_bramSize; }

    bool isDspColumn(int column) const { return m_dspColumns.find(column) != m_dspColumns.end(); }
    bool isBramColumn(int column) const { return m_bramColumns.find(column) != m_bramColumns.end(); }

    QSize elementSize(Tile::Type type) const {
        QSize minSize(1,1);
        switch(type) {
        case Tile::Type::Io: return minSize;
        case Tile::Type::Clb: return minSize;
        case Tile::Type::Bram: return m_bramSize;
        case Tile::Type::Dsp: return m_dspSize;
        default: return minSize;
        }
        return minSize;
    }

    bool validateFit();

    // Number of border (IO) cells the displayed grid adds around the device
    // core on each side. config.json's DEVICE_SIZE is the core grid; the grid
    // wraps it with one IO ring, so columns()/rows() = core + 2*kBorder and the
    // 1-based DSP_COLS/BRAM_COLS core columns are shifted by kBorder into grid
    // coordinates.
    static constexpr int kBorder = 1;

private:
    QString m_error;
    // Path of the parsed config.json, so validateFit() can name it too.
    QString m_configPath;
    int m_columns = -1;
    int m_rows = -1;
    std::set<int> m_dspColumns;
    std::set<int> m_bramColumns;
    QSize m_dspSize;
    QSize m_bramSize;

    bool parse(const std::filesystem::path& deviceConfigFile);

    std::optional<QSize> parseSize(const QString& sizeStr, const QString& key);
    bool parseColumns(const QString& csv, std::set<int>& columns,
                      const QString& key);
};
using DeviceGridDescriptorPtr = std::shared_ptr<DeviceGridDescriptor>;

}  // namespace fp
