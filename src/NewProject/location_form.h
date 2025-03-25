#ifndef LOCATIONFORM_H
#define LOCATIONFORM_H

#include <QWidget>

namespace Ui {
class locationForm;
}

class QFileSystemModel;

namespace FOEDAG {

class locationForm : public QWidget {
  Q_OBJECT

 public:
  explicit locationForm(const QString &defaultPath = QString{},
                        QWidget *parent = nullptr);
  ~locationForm();

  QString getProjectName();
  QString getProjectPath();
  bool IsCreateDir();
  bool IsProjectNameExit();
 private slots:
  void on_m_btnBrowse_clicked();
  void on_m_checkBox_stateChanged(int state);
  void on_m_lineEditPname_textChanged(const QString &name);
  void on_m_lineEditPpath_textChanged(const QString &path);

 private:
  QFileSystemModel* m_fsModel{nullptr};
  Ui::locationForm *ui;
  void updateProjectLocation(const QString& name, const QString& path, bool createSubDir);
};
}  // namespace FOEDAG
#endif  // LOCATIONFORM_H
