#include "location_form.h"

#include <QFileDialog>
#include <QCompleter>
#include <QFileSystemModel>

#include "ui_location_form.h"
using namespace FOEDAG;

locationForm::locationForm(const QString &defaultPath, QWidget *parent)
    : QWidget(parent), ui(new Ui::locationForm) {
  ui->setupUi(this);
  int count = 1;
  QString project_prefix = "project_";

  QString workPath = QDir::homePath();

  QString imageWorkPath = QString::fromUtf8(qgetenv("AURORA_IMAGE_WORKDIR"));
  if (!imageWorkPath.isEmpty() && QDir(imageWorkPath).exists()) {
    workPath = imageWorkPath; // we want to use permanent storage for case when app is running within docker container
  }

  QString homePath = defaultPath.isEmpty() ? workPath : defaultPath;
  QString projectPathPrefix = homePath + QDir::separator() + project_prefix;
  while (QDir(projectPathPrefix + QString::number(count)).exists()) {
    count++;
  }

  ui->m_labelTitle->setText(tr("Project Directory"));
  ui->m_labelText->setText(
      tr("Specify the project name and directory location for your project."));
  ui->m_labelPname->setText(tr("Project Name:"));
  ui->m_labelPpath->setText(tr("Project Directory:"));
  ui->m_checkBox->setText(tr("Create Project Subdirectory"));
  ui->m_labelPath0->setText(tr("Project will be created at:"));
  ui->m_btnBrowse->setText(tr("Browse..."));
  ui->m_lineEditPname->setText(project_prefix + QString::number(count));
  ui->m_lineEditPpath->setText(homePath);
  ui->m_labelPath1->setText(ui->m_lineEditPpath->text());
  ui->m_checkBox->setCheckState(Qt::CheckState::Checked);

  QCompleter* completer = new QCompleter;
  m_fsModel = new QFileSystemModel;
  m_fsModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
  m_fsModel->setRootPath(homePath);
  completer->setModel(m_fsModel);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  ui->m_lineEditPpath->setCompleter(completer);
}

locationForm::~locationForm() { delete ui; }
QString locationForm::getProjectName() { return ui->m_lineEditPname->text(); }
QString locationForm::getProjectPath() { return ui->m_labelPath1->text(); }
bool locationForm::IsCreateDir() {
  return ui->m_checkBox->checkState() == Qt::CheckState::Checked ? true : false;
}

bool locationForm::IsProjectNameExit() {
  QString strName = ui->m_lineEditPname->text();
  QString strPath = ui->m_labelPath1->text();
  if (Qt::CheckState::Checked == ui->m_checkBox->checkState()) {
    QFileInfo fileInfo(strPath);
    if (fileInfo.exists()) {
      return true;
    }
  } else {
    QString strFile = strPath + "/" + strName + ".ospr";
    QFileInfo fileInfo(strFile);
    if (fileInfo.exists()) {
      return true;
    }
  }
  return false;
}

void locationForm::on_m_btnBrowse_clicked() {
  QString path = QFileDialog::getExistingDirectory(
      this, tr("Select Directory"),
      ui->m_lineEditPpath->text() == "" ? QDir::homePath()
                                        : ui->m_lineEditPpath->text(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if ("" == path) {
    return;
  }
  ui->m_lineEditPpath->setText(path);
  m_fsModel->setRootPath(path);

  QString name = ui->m_lineEditPname->text();
  Qt::CheckState state = ui->m_checkBox->checkState();
  updateProjectLocation(name, path, Qt::CheckState::Checked == state);
}

void locationForm::on_m_checkBox_stateChanged(int state) {
  QString name = ui->m_lineEditPname->text();
  QString path = ui->m_lineEditPpath->text();
  updateProjectLocation(name, path, Qt::CheckState::Checked == state);
}

void locationForm::on_m_lineEditPname_textChanged(const QString &name) {
  QString path = ui->m_lineEditPpath->text();
  Qt::CheckState state = ui->m_checkBox->checkState();
  updateProjectLocation(name, path, Qt::CheckState::Checked == state);
}

void locationForm::on_m_lineEditPpath_textChanged(const QString &path)
{
  QString name = ui->m_lineEditPname->text();
  Qt::CheckState state = ui->m_checkBox->checkState();
  updateProjectLocation(name, path, Qt::CheckState::Checked == state);
}

void locationForm::updateProjectLocation(const QString& name, const QString& path, bool createSubDir)
{
  if (createSubDir && "" != name && "" != path) {
    ui->m_labelPath1->setText(path + "/" + name);
  } else {
    ui->m_labelPath1->setText(path);
  }
}