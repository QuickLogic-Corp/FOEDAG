#pragma once

#include <QObject>
#include <QPoint>
#include <QPropertyAnimation>

namespace fp {

class PointAnimation : public QObject {
    Q_OBJECT
    Q_PROPERTY(QPointF point READ point WRITE setPoint NOTIFY pointChanged)

public:
    PointAnimation(QObject* parent);

    QPointF point() const { return m_point; }

    void start(const QPointF& currentPoint, const QPointF& targetPoint, int durationMs = 500);

signals:
    void pointChanged(QPointF);

private slots:
    void setPoint(const QPointF& p);

private:
    QPointF m_point;
    QPropertyAnimation* m_anim = nullptr;
};

}  // namespace fp
