#pragma once

#include <QDialog>

#include <unordered_set>
#include <string>

class QLineEdit;
class QLabel;
class QDialogButtonBox;

namespace fp {

class NewUniqueNameDialog final : public QDialog {
    Q_OBJECT
public:
    NewUniqueNameDialog(const QString& title, QWidget* parent = nullptr);
    void setExistedNames(const std::unordered_set<std::string>& existedNames) { m_existedNames = existedNames; }

    std::string name() const { return m_name; }

private slots:
    void validate(const QString& candidate);

private:
    std::string m_name;
    std::unordered_set<std::string> m_existedNames;

    QLineEdit* m_edit = nullptr;
    QLabel* m_error = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_ok = nullptr;
};

}  // namespace fp
