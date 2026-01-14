#pragma once

#include "Tile.h"

#include <QSize>

#include <set>
#include <memory>

namespace fp {

class DeviceDescriptor {
public:
    DeviceDescriptor(int columns, int rows, const std::set<int>& dspColumns, const std::set<int>& bramColumns, const QSize& dspSize, const QSize& bramSize)
        :
        m_columns(columns)
        , m_rows(rows)
        , m_dspColumns(dspColumns)
        , m_bramColumns(bramColumns)
        , m_dspSize(dspSize)
        , m_bramSize(bramSize)
    {
        Tile::setDeviceRowsNum(m_rows);
    }

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

private:
    int m_columns = -1;
    int m_rows = -1;
    std::set<int> m_dspColumns;
    std::set<int> m_bramColumns;
    QSize m_dspSize;
    QSize m_bramSize;
};
using DeviceDescriptorPtr = std::shared_ptr<DeviceDescriptor>;

}  // namespace fp
