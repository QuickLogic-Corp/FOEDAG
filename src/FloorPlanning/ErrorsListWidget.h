#pragma once

#include <QWidget>
#include <QIcon>

#include <unordered_map>
#include <string>

class QListWidget;
class QLabel;

namespace fp {

class ErrorsListWidget final : public QWidget {
    Q_OBJECT
public:
    ErrorsListWidget(QWidget* parent = nullptr);
    void setIssues(const std::unordered_map<std::string, std::string>& errors, const std::unordered_map<std::string, std::string>& warnings);

    void clear();

private:
    QIcon m_errorIcon;
    QIcon m_warningIcon;
    QLabel* m_lbErrors{nullptr};
    QLabel* m_lbWarnings{nullptr};
    QListWidget* m_lwWarnings{nullptr};
    QListWidget* m_lwErrors{nullptr};

    void updateVisibility(bool hasErrors, bool hasWarnings);
};

}  // namespace fp
