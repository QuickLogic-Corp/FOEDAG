#pragma once

#include <QRect>
#include <QColor>

#include <cstdint>
#include <memory>

namespace fp {

class Tile {
    static int s_deviceRowsNum; // needed to align Qt and VPR coordinate systems (Y axis up)
public:
    struct Index {
        Index(): col(-1), row(-1) {}
        Index(int col, int row): col(col), row(row) {}
        bool operator==(const Index& rhs) const {
            return col == rhs.col && row == rhs.row;
        }

        void reset() {
            col = -1;
            row = -1;
        }

        int col;
        int row;
    };

    enum class Type { Empty, Clb, Io, Bram, Dsp };
    Tile(Type type, int x, int y, int w, int h, const QString& label);
    Tile(): m_index(-1, -1) {} // required for std::unordered_map

    static void setDeviceRowsNum(int deviceRowsNum) { s_deviceRowsNum = deviceRowsNum; }
    static int deviceRowsNum() { return s_deviceRowsNum; }

    Type type() const { return m_type; }
    const QColor& color() const;
    static const QColor& color(Type);

    void setVisible(bool isVisible) { m_isVisible = isVisible; }

    static float unitPx() { return s_unitPx; }
    static float borderPx() { return s_borderPx; }

    const Index& index() const { return m_index; }

    const QString& name() const { return m_name; }
    const QRectF& rect() const { return m_rect; }

    bool isVisible() const { return m_isVisible; }
    bool isInArea(const QRectF& area) const;

    static QRectF buildRect(const Tile::Index&, int w=1, int h=1);

private:
    bool m_isVisible = true;
    Type m_type;
    QRectF m_rect;
    QString m_name;

    static float s_unitPx;
    static float s_borderPx;

    Index m_index;
    int m_cols{1};
    int m_rows{1};

    static QColor s_clbColor;
    static QColor s_ioColor;
    static QColor s_bramColor;
    static QColor s_dspColor;
    static QColor s_emptyColor;

    void buildRect();
};
using TilePtr = std::shared_ptr<Tile>;

}  // namespace fp

// required to use Tile::Index in std::unordered_set
namespace std {
template<>
struct hash<fp::Tile::Index> {
    size_t operator()(const fp::Tile::Index& i) const noexcept {
        std::uint64_t key =
            (std::uint64_t(std::uint32_t(i.row)) << 32) ^
            std::uint64_t(std::uint32_t(i.col));
        return std::hash<std::uint64_t>{}(key);
    }
};
}
