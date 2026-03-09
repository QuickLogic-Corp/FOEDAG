#include "PointAnimation.h"

namespace fp {

PointAnimation::PointAnimation(QObject* parent): QObject(parent)
{

}

void PointAnimation::start(const QPointF& currentPoint, const QPointF& targetPoint, int durationMs)
{
    if (!m_anim) {
        m_anim = new QPropertyAnimation(this, "point", this);
        m_anim->setEasingCurve(QEasingCurve::InOutCubic);
    }

    m_anim->stop();
    m_anim->setDuration(durationMs);
    m_anim->setStartValue(currentPoint);
    m_anim->setEndValue(targetPoint);
    m_anim->start();
}

void PointAnimation::setPoint(const QPointF& point) {
    if (point == m_point) {
        return;
    }
    m_point = point;
    emit pointChanged(m_point);
}

} // namespace fp
