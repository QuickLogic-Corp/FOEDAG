#include "CheckableButton.h"

#include <QPainter>

namespace fp {

CheckableButton::CheckableButton(QIcon icon, QWidget* parent)
: QPushButton(icon, "", parent)
{
  setCheckable(true);
}

void CheckableButton::paintEvent(QPaintEvent* event)
{
  QPushButton::paintEvent(event);

  if (isChecked()) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal line = 3.0;
    const QColor color(0, 100, 0, 100);

    QPen pen(color);
    pen.setWidthF(line);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QRectF r = QRectF(rect());
    r.adjust(line * 0.5, line * 0.5, -line * 0.5, -line * 0.5);
    p.drawRect(r);
  }
}

} // namespace fp
