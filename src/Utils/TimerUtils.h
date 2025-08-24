#pragma once

#include <QElapsedTimer>
#include <QDebug>

class ScopedTimer {
public:
    ScopedTimer(const QString& scopeName)
        : m_name(scopeName), m_timer() {
        m_timer.start();
    }

    ~ScopedTimer() {
        qDebug() << m_name << "took" << m_timer.elapsed() << "ms";
    }

private:
    QString m_name;
    QElapsedTimer m_timer;
};


class PointTimer {
public:
    PointTimer()
        : m_timer() {
        m_timer.start();
    }

    ~PointTimer() {
    }

    void measure(const QString& name) {
        qDebug() << name << "took" << m_timer.elapsed() << "ms";
        m_timer.restart();
    }

private:
    QElapsedTimer m_timer;
};
