/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Tile.h"

namespace FOEDAG {

QColor Tile::s_clbColor = QColor{200, 200, 0};
QColor Tile::s_ioColor = QColor{20, 20, 20, 50};
QColor Tile::s_bramColor = QColor{100, 100, 200};
QColor Tile::s_dspColor = QColor{100, 200, 100};
QColor Tile::s_emptyColor = QColor{0, 0, 0, 0};

float Tile::s_unitPx{32.0};
float Tile::s_borderPx{10.0};

Tile::Tile(Type type, int x, int y, int w, int h, const QString& label)
    : m_type(type),
    m_index(x, y),
    m_w(w),
    m_h(h),
    m_label(label)
{
    buildRect();
}

const QColor& Tile::color() const { return Tile::color(m_type); }

void Tile::buildRect() {
    const double pitch = s_unitPx + s_borderPx;   // distance between cell origins

    const double x = m_index.col * pitch;
    const double y = m_index.row * pitch;

    double w = m_w * s_unitPx + (m_w - 1) * s_borderPx;
    double h = m_h * s_unitPx + (m_h - 1) * s_borderPx;
    m_rect = QRectF(x, y, w, h);
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

bool Tile::isVisible(const QRectF& visibleArea) const
{
    return m_rect.intersects(visibleArea);
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

} // namespace FOEDAG
