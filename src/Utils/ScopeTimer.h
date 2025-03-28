#pragma once

#include <QElapsedTimer>
#include <QDebug>

class ScopeTimer {
public:
    ScopeTimer(const QString& scopeName)
        : m_name(scopeName), m_timer() {
        m_timer.start();
    }

    ~ScopeTimer() {
        qDebug() << m_name << "took" << m_timer.elapsed() << "ms";
    }

private:
    QString m_name;
    QElapsedTimer m_timer;
};

