#pragma once

#include <QWidget>
#include <QIcon>

#include <unordered_set>
#include <string>

class QListWidget;

namespace fp {

class ErrorsListWidget final : public QWidget {
    Q_OBJECT
public:
    ErrorsListWidget(QWidget* parent = nullptr);
    void setErrors(const std::unordered_set<std::string>& errors);

    void clear();

private:
    QIcon m_errorIcon;
    QListWidget* m_lwErrors{nullptr};
};

}  // namespace fp
