#include "Tile.h"

namespace fp {

QColor Tile::s_clbColor = QColor{200, 200, 0};
QColor Tile::s_ioColor = QColor{20, 20, 20, 50};
QColor Tile::s_bramColor = QColor{100, 100, 200};
QColor Tile::s_dspColor = QColor{100, 200, 100};
QColor Tile::s_emptyColor = QColor{0, 0, 0, 0};

float Tile::s_unitPx{32.0};
float Tile::s_borderPx{10.0};

Tile::Tile(Type type, int col, int row, int w, int h, const QString& name)
    : m_type(type),
    m_index(col, row),
    m_w(w),
    m_h(h),
    m_name(name)
{
    buildRect();
}

const QColor& Tile::color() const { return Tile::color(m_type); }

QRectF Tile::buildRect(const Tile::Index& index, int w, int h)
{
    const double pitch = s_unitPx + s_borderPx;   // distance between cell origins

    const double x = index.col * pitch;
    // flip vertically
    const double y = (DEVICE_SIZE - (index.row + h - 1)) * pitch;

    double tileWidth = w * s_unitPx + (w - 1) * s_borderPx;
    double tileHeight = h * s_unitPx + (h - 1) * s_borderPx;
    return QRectF(x, y, tileWidth, tileHeight).normalized();
}

void Tile::buildRect()
{
    m_rect = buildRect(m_index, m_w, m_h);
}

const QColor& Tile::color(Type type) {
    switch(type) {
    case Type::Clb:   return s_clbColor;
    case Type::Io:    return s_ioColor;
    case Type::Bram:  return s_bramColor;
    case Type::Dsp:   return s_dspColor;
    case Type::Empty: default: return s_emptyColor;
    }
}

bool Tile::isLocated(const QRectF& area) const
{
    QRectF outer = area.normalized();
    QRectF inner = m_rect.normalized();

    bool inside =
        outer.left()   <= inner.left()  &&
        outer.right()  >= inner.right() &&
        outer.top()    <= inner.top()   &&
        outer.bottom() >= inner.bottom();

    return inside;
}

} // namespace fp
