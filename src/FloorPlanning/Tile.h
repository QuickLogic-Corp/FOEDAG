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

#pragma once

#include <QRect>
#include <QColor>

namespace FOEDAG {

class Tile {
public:
    enum class Type { Empty, Clb, Io, Bram, Dsp };
    Tile(Type type, int x, int y, int w, int h);

    Type type() const { return m_type; }
    const QColor& color() const;
    static const QColor& color(Type);

    static float unitPx() { return s_unitPx; }
    static float borderPx() { return s_borderPx; }

    const QString& label() const { return m_label; }
    const QRectF& rect() const { return m_rect; }
    bool isVisible(const QRectF& screen) const;

private:
    Type m_type;
    QRectF m_rect;
    QString m_label;

    static float s_unitPx;
    static float s_borderPx;

    int m_x{0};
    int m_y{0};
    int m_w{1};
    int m_h{1};

    static QColor s_clbColor;
    static QColor s_ioColor;
    static QColor s_bramColor;
    static QColor s_dspColor;
    static QColor s_emptyColor;

    void buildRect();
    void buildLabel();
};


}  // namespace FOEDAG
