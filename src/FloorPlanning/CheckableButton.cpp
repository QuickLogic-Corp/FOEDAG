#include "CheckableButton.h"

#include <QPainter>

namespace fp {

CheckableButton::CheckableButton(const QIcon& icon, QWidget* parent)
: QPushButton(icon, "", parent),
  m_hightLightWhenChecked(true)
{
  setCheckable(true);

  m_hightLightPen.setColor(m_hightLightColor);
  m_hightLightPen.setWidthF(m_lineWidth);
  m_hightLightPen.setJoinStyle(Qt::RoundJoin);
  m_hightLightPen.setCapStyle(Qt::RoundCap);
}

CheckableButton::CheckableButton(const QIcon& uncheckedIcon, const QIcon& checkedIcon, QWidget* parent)
: QPushButton(uncheckedIcon, "", parent),
  m_uncheckedIcon(uncheckedIcon),
  m_checkedIcon(checkedIcon),
  m_hightLightWhenChecked(false)
{
  setCheckable(true);
  connect(this, &QPushButton::toggled, this, [this](bool checked) {
    if (checked) {
      setIcon(m_checkedIcon);
    } else {
      setIcon(m_uncheckedIcon);
    }
  });
}

void CheckableButton::paintEvent(QPaintEvent* event)
{
  QPushButton::paintEvent(event);

  if (m_hightLightWhenChecked) {
    if (isChecked()) {
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing, true);

      p.setBrush(Qt::NoBrush);
      p.setPen(m_hightLightPen);

      QRectF r = QRectF(rect());
      r.adjust(m_lineWidth * 0.5, m_lineWidth * 0.5, -m_lineWidth * 0.5, -m_lineWidth * 0.5);
      p.drawRect(r);
    }
  }
}

} // namespace fp
