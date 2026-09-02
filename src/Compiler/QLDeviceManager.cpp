#include "QLDeviceManager.h"

#include <cstdlib>   // std::getenv
#include <cctype>    // std::isspace

#include <QObject>
#include <QWidget>
#include <QFont>
#include <QSpacerItem>
#include <QPalette>
#include <QDialog>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QTextEdit>
#include <QDebug>
#include <QDomDocument>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QDirIterator>
#include <QProcess>

#include <iostream>
#include <set>
#include <regex>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <qlcrypt/qlcrypt.hpp>
#include "CompilerOpenFPGA_ql.h"
#include "QLDeviceLayoutInfo.h"
#include "Compiler/Compiler.h"
#include "Utils/FileUtils.h"
#include "Utils/LogUtils.h"
#include "Utils/StringUtils.h"
#include "MainWindow/Session.h"
#include "QLSettingsManager.h"
#include "NewProject/ProjectManager/project_manager.h"

extern FOEDAG::Session* GlobalSession;

// defined at global scope in foedag_version_number_ql.cpp. declared here rather than inside
// namespace FOEDAG below: a declaration in the namespace names a different symbol and fails
// to link.
extern const char* foedag_version_number;

namespace FOEDAG {


// singleton init
QLDeviceManager* QLDeviceManager::instance = nullptr;


// static access function
QLDeviceManager* QLDeviceManager::getInstance(bool initialize) {

  // creation
  if(instance == nullptr) {
    // std::cout << "create new QLDeviceManager()" << std::endl;
    instance = new QLDeviceManager();
  }

  // init, if needed
  if(initialize == true) {
    instance->parseDeviceData();
  }

  return instance;
}


bool QLDeviceManager::compareLayouts(const std::string& layout_1, const std::string& layout_2) {

  bool firstIsLesser = true;

  // assumption: layout_names are always "widthxheight" -> if not, then default to layout_1 < layout_2
  // return true if layout_1 < layout_2, else false
  try {

    std::string layout_1_lc = StringUtils::toLower(layout_1);
    std::string layout_2_lc = StringUtils::toLower(layout_2);

    std::vector<std::string> tokens_layout_1;
    StringUtils::tokenize(layout_1_lc, "x", tokens_layout_1);
    
    std::vector<std::string> tokens_layout_2;
    StringUtils::tokenize(layout_2_lc, "x", tokens_layout_2);

    if( (tokens_layout_1.size() == 2) && (tokens_layout_2.size() == 2) ) {
      int layout_1_width = std::stoi(tokens_layout_1[0]);
      int layout_1_height = std::stoi(tokens_layout_1[1]);

      int layout_2_width = std::stoi(tokens_layout_2[0]);
      int layout_2_height = std::stoi(tokens_layout_2[1]);

      if(layout_1_width < layout_2_width) {
        firstIsLesser = true;
      }
      else if(layout_1_width == layout_2_width) {
        if(layout_1_height < layout_2_height) {
          firstIsLesser = true;
        }
        else {
          firstIsLesser = false;
        }
      }
      else {
        firstIsLesser = false;
      }
    }
  }
  catch (std::invalid_argument const &e) {
    std::cout << "[compareLayouts] Bad input: std::invalid_argument thrown" << std::endl;
    firstIsLesser = true;
  }
    catch (std::out_of_range const &e) {
    std::cout << "[compareLayouts] Integer overflow: std::out_of_range thrown" << std::endl;
    firstIsLesser = true;
  }

  return firstIsLesser;
}


QLDeviceManager::QLDeviceManager(QObject *parent)
    : QObject(parent) {}


QLDeviceManager::~QLDeviceManager() {
  // std::cout << "~QLDeviceManager()" << std::endl;
  if(device_manager_widget != nullptr) {
    delete device_manager_widget;
  }
}


QWidget* QLDeviceManager::createDeviceSelectionWidget(bool newProjectMode) {

  // whenever a new GUI Widget is created, we mark the mode of operation of this widget:
  this->newProjectMode = newProjectMode;

  // std::cout << "createDeviceSelectionWidget()++, newProjectMode: " << newProjectMode << std::endl;

  if(device_manager_widget != nullptr) {
    device_manager_widget->deleteLater();
  }

  // cleanup GUI related data as well:
  QLDeviceTarget empty_device_target;
  this->device_target_selected = empty_device_target;
  this->families.clear();
  this->family.clear();
  this->foundrynodes.clear();
  this->foundrynode.clear();
  this->foundry.clear();
  this->node.clear();
  this->devicenames.clear();
  this->devicename.clear();
  this->voltage_thresholds.clear();
  this->voltage_threshold.clear();
  this->p_v_t_corners.clear();
  this->p_v_t_corner.clear();
  this->layouts.clear();
  this->layout.clear();

  device_manager_widget = new QWidget();

  QWidget* dlg = device_manager_widget;

  dlg->setWindowTitle("Device Selection");

  QVBoxLayout* dlg_toplevellayout = new QVBoxLayout();
  dlg->setLayout(dlg_toplevellayout);

  QVBoxLayout* dlg_titleDetailLayout = new QVBoxLayout();
  QLabel* dlg_titleLabel = new QLabel("Device Selection");
  QFont dlg_titleLabelFont;
  dlg_titleLabelFont.setWeight(QFont::Bold);
  dlg_titleLabelFont.setStyleHint(QFont::SansSerif);
  dlg_titleLabelFont.setPointSize(12);
  dlg_titleLabel->setFont(dlg_titleLabelFont);
  dlg_titleLabel->setWordWrap(true);

  QLabel* dlg_detailLabel = new QLabel("Select the target device for this project");
  dlg_detailLabel->setWordWrap(true);

  dlg_titleDetailLayout->addWidget(dlg_titleLabel);
  dlg_titleDetailLayout->addSpacing(15);
  dlg_titleDetailLayout->addWidget(dlg_detailLabel);


  QFrame* dlg_selectionFrame = new QFrame();
  dlg_selectionFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
  QVBoxLayout* dlg_selectionFrameLayout = new QVBoxLayout();
  dlg_selectionFrame->setLayout(dlg_selectionFrameLayout);
  QGroupBox *devicetypeGroupBox = new QGroupBox(tr("Device Type"));
  QVBoxLayout* devicetypeGroupBoxLayout =  new QVBoxLayout();
  devicetypeGroupBox->setLayout(devicetypeGroupBoxLayout);
  QHBoxLayout* dlg_familylayout = new QHBoxLayout();
  QHBoxLayout* dlg_foundrynodelayout = new QHBoxLayout();
  QHBoxLayout* dlg_devicenamelayout = new QHBoxLayout();
  devicetypeGroupBoxLayout->addLayout(dlg_familylayout);
  devicetypeGroupBoxLayout->addLayout(dlg_foundrynodelayout);
  devicetypeGroupBoxLayout->addLayout(dlg_devicenamelayout);
  QGroupBox *devicevariantGroupBox = new QGroupBox(tr("Device Variant"));
  QVBoxLayout* devicevariantGroupBoxLayout =  new QVBoxLayout();
  devicevariantGroupBox->setLayout(devicevariantGroupBoxLayout);
  QHBoxLayout* dlg_voltage_thresholdlayout = new QHBoxLayout();
  QHBoxLayout* dlg_p_v_t_cornerlayout = new QHBoxLayout();
  devicevariantGroupBoxLayout->addLayout(dlg_voltage_thresholdlayout);
  devicevariantGroupBoxLayout->addLayout(dlg_p_v_t_cornerlayout);
  QGroupBox *devicesizeGroupBox = new QGroupBox(tr("Device Size"));
  QVBoxLayout* devicesizeGroupBoxLayout =  new QVBoxLayout();
  devicesizeGroupBox->setLayout(devicesizeGroupBoxLayout);
  QHBoxLayout* dlg_layoutlayout = new QHBoxLayout();
  QHBoxLayout* dlg_resourceslayout = new QHBoxLayout();
  devicesizeGroupBoxLayout->addLayout(dlg_layoutlayout);
  devicesizeGroupBoxLayout->addLayout(dlg_resourceslayout);


  // dlg_selectionFrameLayout->addLayout(dlg_familylayout);
  // dlg_selectionFrameLayout->addLayout(dlg_foundrynodelayout);
  // dlg_selectionFrameLayout->addLayout(dlg_voltage_thresholdlayout);
  // dlg_selectionFrameLayout->addLayout(dlg_p_v_t_cornerlayout);
  // dlg_selectionFrameLayout->addLayout(dlg_layoutlayout);
  dlg_selectionFrameLayout->addWidget(devicetypeGroupBox);
  dlg_selectionFrameLayout->addWidget(devicevariantGroupBox);
  dlg_selectionFrameLayout->addWidget(devicesizeGroupBox);

  QLabel* m_combobox_family_label = new QLabel("Family");
  QLabel* m_combobox_foundry_node_label = new QLabel("Foundry-Node");
  QLabel* m_combobox_devicename_label = new QLabel("Device");
  QLabel* m_combobox_voltage_threshold_label = new QLabel("Voltage Threshold");
  QLabel* m_combobox_p_v_t_corner_label = new QLabel("Corner");
  QLabel* m_combobox_layout_label = new QLabel("Layout");
  m_combobox_family = new QComboBox();
  m_combobox_foundry_node = new QComboBox();
  m_combobox_devicename = new QComboBox();
  m_combobox_voltage_threshold = new QComboBox();
  m_combobox_p_v_t_corner = new QComboBox();
  m_combobox_layout = new QComboBox();
  m_device_resources_label = new QLabel();
  m_combobox_family->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_foundry_node->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_devicename->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_voltage_threshold->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_p_v_t_corner->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_layout->setSizeAdjustPolicy(QComboBox::AdjustToContents);

  families.clear();
  singularity.clear();
  m_combobox_family->clear();
  for (QLDeviceType device: this->device_list) {
    // ensure that the item being added has not been added before using std::set
    // for performance reasons, keeping a vector as final container for future need of sorting.
    if(singularity.insert(device.family).second == true) {
      families.push_back(device.family);
    }
  }

  for (std::string family: families) {
    m_combobox_family->addItem(QString::fromStdString(family));
  }

  // connect( m_combobox_family, SIGNAL(currentTextChanged(const QString&)), this, SLOT(familyChanged(const QString&)) );
  // QObject::connect( m_combobox_family, &QComboBox::currentTextChanged,
  //                   this, &QLDeviceManager::familyChanged );
  // unfortunately, the QLDeviceManager object seems to live on a thread, where there is no EventQueue!
  // hence the slot never gets called (unless we can derive it from QWidget, in which case it will be on the GUI thread)
  // hence, we resort to using a lambda (instead of say, customizing QWidget for the GUI part itself) as below:
  
  QObject::connect( m_combobox_family, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_family: " << currentText.toStdString() << std::endl;
                        this->familyChanged(currentText);
                        } );

  QObject::connect( m_combobox_foundry_node, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_foundry_node: " << currentText.toStdString() << std::endl;
                        this->foundrynodeChanged(currentText);
                        } );

  QObject::connect( m_combobox_devicename, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_devicename: " << currentText.toStdString() << std::endl;
                        this->devicenameChanged(currentText);
                        } );

  QObject::connect( m_combobox_voltage_threshold, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_voltage_threshold: " << currentText.toStdString() << std::endl;
                        this->voltage_thresholdChanged(currentText);
                        } );

  QObject::connect( m_combobox_p_v_t_corner, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_p_v_t_corner: " << currentText.toStdString() << std::endl;
                        this->p_v_t_cornerChanged(currentText);
                        } );

  QObject::connect( m_combobox_layout, &QComboBox::currentTextChanged, 
                    [this](const QString& currentText){
                        // std::cout << "lambda-oncurrentTextChanged-m_combobox_layout: " << currentText.toStdString() << std::endl;
                        this->layoutChanged(currentText);
                        } );

  dlg_familylayout->addWidget(m_combobox_family_label);
  dlg_familylayout->addWidget(m_combobox_family);

  dlg_foundrynodelayout->addWidget(m_combobox_foundry_node_label);
  dlg_foundrynodelayout->addWidget(m_combobox_foundry_node);

  dlg_devicenamelayout->addWidget(m_combobox_devicename_label);
  dlg_devicenamelayout->addWidget(m_combobox_devicename);

  dlg_voltage_thresholdlayout->addWidget(m_combobox_voltage_threshold_label);
  dlg_voltage_thresholdlayout->addWidget(m_combobox_voltage_threshold);

  dlg_p_v_t_cornerlayout->addWidget(m_combobox_p_v_t_corner_label);
  dlg_p_v_t_cornerlayout->addWidget(m_combobox_p_v_t_corner);

  dlg_layoutlayout->addWidget(m_combobox_layout_label);
  dlg_layoutlayout->addWidget(m_combobox_layout);
  dlg_resourceslayout->addStretch();
  dlg_resourceslayout->addWidget(m_device_resources_label);

  QHBoxLayout* dlg_buttonslayout = nullptr;
  if(!newProjectMode) {
    m_message_label = new QLabel();
    m_message_label->setWordWrap(true);
    // background/font color
    m_message_label->setAutoFillBackground(true);
    //m_message_label->setStyleSheet("QLabel { background-color : red; color : blue; }");
    QPalette m_message_label_palette;
    m_message_label_palette.setColor(QPalette::Window, Qt::white);
    m_message_label_palette.setColor(QPalette::WindowText, Qt::red);
    m_message_label->setPalette(m_message_label_palette);
    // frame
    m_message_label->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    m_message_label->hide();


    dlg_buttonslayout = new QHBoxLayout();
    m_button_reset = new QPushButton("Reset");
    m_button_reset->setToolTip("Reset Device Selection as in the Settings JSON");
    m_button_reset->setDisabled(true);
    QObject::connect( m_button_reset, &QPushButton::clicked, 
                        [this](){
                            // std::cout << "lambda-clicked-m_button_reset" << std::endl;
                            this->resetButtonClicked();
                            } );
    m_button_apply = new QPushButton("Apply");
    m_button_apply->setToolTip("Apply the new Device Selection to the Settings JSON");
    m_button_apply->setDisabled(true);
    QObject::connect( m_button_apply, &QPushButton::clicked, 
                        [this](){
                            // std::cout << "lambda-clicked-m_button_apply" << std::endl;
                            this->applyButtonClicked();
                            } );
    dlg_buttonslayout->addStretch();
    dlg_buttonslayout->addWidget(m_button_reset);
    dlg_buttonslayout->addWidget(m_button_apply);
  }

  dlg_toplevellayout->addLayout(dlg_titleDetailLayout);
  dlg_toplevellayout->addSpacing(20);
  dlg_toplevellayout->addWidget(dlg_selectionFrame);
  if(!newProjectMode) {
    dlg_toplevellayout->addWidget(m_message_label);
    dlg_toplevellayout->addLayout(dlg_buttonslayout);
  }
  dlg_toplevellayout->addStretch();

  // trigger a self UI update according to the currently selected device:
  triggerUIUpdate();

  // std::cout << "createDeviceSelectionWidget()--, newProjectMode: " << newProjectMode << std::endl;

  return dlg;
}

// QLDeviceVariantLayout* QLDeviceManager::findDeviceLayoutVariantPtr(const std::string& family, 
//                                                                    const std::string& foundry,
//                                                                    const std::string& node,
//                                                                    const std::string& devicename,
//                                                                    const std::string& voltage_threshold,
//                                                                    const std::string& p_v_t_corner,
//                                                                    const std::string& layoutName)
// {
//   for (QLDeviceType& device: this->device_list) {
//     if ((device.family == family) && (device.foundry == foundry) && (device.node == node) && (device.devicename == devicename)) {
//       for (QLDeviceVariant& device_variant: device.device_variants) {
//         if ((device_variant.voltage_threshold == voltage_threshold) && (device_variant.p_v_t_corner == p_v_t_corner)) {
//           for (QLDeviceVariantLayout& layout: device_variant.device_variant_layouts) {
//             if (layout.name == layoutName) {
//               return &layout;
//             }
//           }
//         }
//       }
//     }
//   }
//   std::cout << "cannot find layout " << layoutName << " for family " << family << std::endl;
//   return nullptr;
// };


void QLDeviceManager::giveupDeviceSelectionWidget() {

  if(device_manager_widget != nullptr) {

    // remove it from any associated parent widget
    device_manager_widget->setParent(0);
  }

}


void QLDeviceManager::familyChanged(const QString& family_qstring)
{

  // when 'family' changes, repopulate all the 'foundry - node' entries accordingly

  // std::cout << "familychanged: " << family_qstring.toStdString() << std::endl;
  foundrynodes.clear();
  singularity.clear();
  m_combobox_foundry_node->blockSignals(true);
  m_combobox_foundry_node->clear();

  family = family_qstring.toStdString();

  for (QLDeviceType device: this->device_list) {
    if(device.family == family) {

      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      // ensure that the item being added has not been added before using std::set
      // for performance reasons, keeping a vector as final container for future need of sorting.
      if(singularity.insert(_foundrynode).second == true) {
        foundrynodes.push_back(_foundrynode);
      }
    }
  }

  for (std::string _foundrynode: foundrynodes) {

    m_combobox_foundry_node->addItem(QString::fromStdString(_foundrynode));
  }

  m_combobox_foundry_node->setCurrentIndex(-1);
  m_combobox_foundry_node->blockSignals(false);

  if(currentDeviceTargetUpdateInProgress) {

      std::string _foundrynode = 
        convertToFoundryNode(device_target.device_variant.foundry, device_target.device_variant.node);

      int index = m_combobox_foundry_node->findText(QString::fromStdString(_foundrynode));
      // std::cout << "m_combobox_foundry_node index" << index << std::endl;
      m_combobox_foundry_node->setCurrentIndex(index);
  }
  else {
    m_combobox_foundry_node->setCurrentIndex(0);
  }
}


void QLDeviceManager::foundrynodeChanged(const QString& foundrynode_qstring)
{

  // when 'foundry - node' changes, repopulate all the 'devicenames' entries accordingly

  // std::cout << "foundrynodechanged: " << foundrynode_qstring.toStdString() << std::endl;

  foundrynode = foundrynode_qstring.toStdString();
  std::vector<std::string> foundrynode_vector = convertFromFoundryNode(foundrynode);
  foundry = foundrynode_vector[0];
  node = foundrynode_vector[1];
  devicenames.clear();
  singularity.clear();
  m_combobox_devicename->blockSignals(true);
  m_combobox_devicename->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        // ensure that the item being added has not been added before using std::set
        // for performance reasons, keeping a vector as final container for future need of sorting.
        if(singularity.insert(device.devicename).second == true) {
          devicenames.push_back(device.devicename);
        }
      }
    }
  }

  for (std::string _devicename: devicenames) {
    m_combobox_devicename->addItem(QString::fromStdString(_devicename));
  }

  m_combobox_devicename->setCurrentIndex(-1);
  m_combobox_devicename->blockSignals(false);

  if(currentDeviceTargetUpdateInProgress) {
    int index = m_combobox_devicename->findText(QString::fromStdString(device_target.device_variant.devicename));
    // std::cout << "m_combobox_devicename index" << index << std::endl;
    m_combobox_devicename->setCurrentIndex(index);
  }
  else {
    m_combobox_devicename->setCurrentIndex(0);
  }
}


void QLDeviceManager::devicenameChanged(const QString& devicename_qstring)
{

  // when 'devicename' changes, repopulate all the 'voltage_threshold' entries accordingly

  // std::cout << "devicenameChanged: " << devicename_qstring.toStdString() << std::endl;

  devicename = devicename_qstring.toStdString();
  voltage_thresholds.clear();
  singularity.clear();
  m_combobox_voltage_threshold->blockSignals(true);
  m_combobox_voltage_threshold->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        if (device.devicename == devicename) {
          for (QLDeviceVariant variant : device.device_variants) {
            // ensure that the item being added has not been added before using std::set
            // for performance reasons, keeping a vector as final container for future need of sorting.
            if(singularity.insert(variant.voltage_threshold).second == true) {
              voltage_thresholds.push_back(variant.voltage_threshold);
            }
          }
        }
      }
    }
  }

  for (std::string _voltage_threshold: voltage_thresholds) {
    m_combobox_voltage_threshold->addItem(QString::fromStdString(_voltage_threshold));
  }

  m_combobox_voltage_threshold->setCurrentIndex(-1);
  m_combobox_voltage_threshold->blockSignals(false);

  if(currentDeviceTargetUpdateInProgress) {
    int index = m_combobox_voltage_threshold->findText(QString::fromStdString(device_target.device_variant.voltage_threshold));
    // std::cout << "m_combobox_voltage_threshold index" << index << std::endl;
    m_combobox_voltage_threshold->setCurrentIndex(index);
  }
  else {
    m_combobox_voltage_threshold->setCurrentIndex(0);
  }
}


void QLDeviceManager::voltage_thresholdChanged(const QString& voltage_threshold_qstring)
{

  // when 'voltage_threshold' changes, repopulate all the 'p_v_t_corner' entries accordingly

  // std::cout << "voltage_thresholdchanged: " << voltage_threshold_qstring.toStdString() << std::endl;

  voltage_threshold = voltage_threshold_qstring.toStdString();
  p_v_t_corners.clear();
  singularity.clear();
  m_combobox_p_v_t_corner->blockSignals(true);
  m_combobox_p_v_t_corner->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        if (device.devicename == devicename) {
          for (QLDeviceVariant variant : device.device_variants) {
            if (variant.voltage_threshold == voltage_threshold) {
              // ensure that the item being added has not been added before using std::set
              // for performance reasons, keeping a vector as final container for future need of sorting.
              if(singularity.insert(variant.p_v_t_corner).second == true) {
                p_v_t_corners.push_back(variant.p_v_t_corner);
              }
            }
          }
        }
      }
    }
  }

  for (std::string _p_v_t_corner: p_v_t_corners) {
    m_combobox_p_v_t_corner->addItem(QString::fromStdString(_p_v_t_corner));
  }

  m_combobox_p_v_t_corner->setCurrentIndex(-1);
  m_combobox_p_v_t_corner->blockSignals(false);

  if(currentDeviceTargetUpdateInProgress) {
    int index = m_combobox_p_v_t_corner->findText(QString::fromStdString(device_target.device_variant.p_v_t_corner));
    // std::cout << "m_combobox_p_v_t_corner index" << index << std::endl;
    m_combobox_p_v_t_corner->setCurrentIndex(index);
  }
  else {
    m_combobox_p_v_t_corner->setCurrentIndex(0);
  }
}


void QLDeviceManager::p_v_t_cornerChanged(const QString& p_v_t_corner_qstring)
{

  // when 'p_v_t_corner' changes, repopulate all the 'layout' entries accordingly

  // std::cout << "p_v_t_cornerchanged: " << p_v_t_corner_qstring.toStdString() << std::endl;

  p_v_t_corner = p_v_t_corner_qstring.toStdString();
  layouts.clear();
  singularity.clear();
  m_combobox_layout->blockSignals(true);
  m_combobox_layout->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        if (device.devicename == devicename) {
          for (QLDeviceVariant variant : device.device_variants) {
            if (variant.voltage_threshold == voltage_threshold) {
              if(variant.p_v_t_corner == p_v_t_corner) {
                for(QLDeviceVariantLayout _layout : variant.device_variant_layouts) {
                  // ensure that the item being added has not been added before using std::set
                  // for performance reasons, keeping a vector as final container for future need of sorting.
                  if(singularity.insert(_layout.name).second == true) {
                    layouts.push_back(_layout.name);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // sort the layouts:
  std::sort(layouts.begin(), layouts.end(), QLDeviceManager::compareLayouts);

  for (std::string _layout: layouts) {
    m_combobox_layout->addItem(QString::fromStdString(_layout));
  }

  m_combobox_layout->setCurrentIndex(-1);
  m_combobox_layout->blockSignals(false);

  if(currentDeviceTargetUpdateInProgress) {
    int index = m_combobox_layout->findText(QString::fromStdString(device_target.device_variant_layout.name));
    // std::cout << "m_combobox_layout index" << index << std::endl;
    m_combobox_layout->setCurrentIndex(index);
  }
  else {
    m_combobox_layout->setCurrentIndex(0);
  }
}

void QLDeviceManager::layoutChanged(const QString& layout_qstring) {

  // when 'layout' changes, store the value

  // std::cout << "layoutChanged: " << layout_qstring.toStdString() << std::endl;
  // std::cout << "newProjectMode: " << newProjectMode << std::endl;

  layout = layout_qstring.toStdString();

  if(currentDeviceTargetUpdateInProgress) {

    currentDeviceTargetUpdateInProgress = false; // done.
  }

  QLDeviceTarget _device_target = convertToDeviceTarget(family,
                                                        foundry,
                                                        node,
                                                        devicename,
                                                        voltage_threshold,
                                                        p_v_t_corner,
                                                        layout);

  if(isDeviceTargetValid(_device_target)) {

    // store the update: in separate variable, until user says confirm
    device_target_selected = _device_target;

    // std::cout << "selected device: " << std::endl;
    // std::cout << " >> [family]              " << device_target.device_variant.family << std::endl;
    // std::cout << " >> [foundry]             " << device_target.device_variant.foundry << std::endl;
    // std::cout << " >> [node]                " << device_target.device_variant.node << std::endl;
    // std::cout << " >> [devicename]          " << device_target.device_variant.devicename << std::endl;
    // std::cout << " >> [voltage_threshold]   " << device_target.device_variant.voltage_threshold << std::endl;
    // std::cout << " >> [p_v_t_corner]        " << device_target.device_variant.p_v_t_corner << std::endl;
    // std::cout << " >> [layout name]         " << device_target.device_variant_layout.name << std::endl;
    // std::cout << " >> [layout width]        " << device_target.device_variant_layout.width << std::endl;
    // std::cout << " >> [layout height]       " << device_target.device_variant_layout.height << std::endl;
  }

  // now check if the selected device_target (via GUI) is different from the currently set device_target,
  // if so, enable the buttons, and show info text to user:
  if(device_manager_widget != nullptr) {
    
    if(newProjectMode) {

        // no apply or reset buttons, update the device_target immediately:
        // device_target should be set to device_target_selected
        setCurrentDeviceTarget(device_target_selected);
        
        // signal to the QLSettingsManager to update the settings JSON
        // to reflect the device_target selection!
        // change this to emit signal later...
        if(settings_manager != nullptr) {
            settings_manager->newProjectMode = newProjectMode;
            settings_manager->updateJSONSettingsForDeviceTarget(device_target_selected);
        }
    }
    else {

        // existing project mode: check if the device_target is changed from currently set device_target
        // and update the UI accordingly.

        std::string device_target_string = convertToDeviceString(device_target);
        std::string device_target_selected_string = convertToDeviceString(device_target_selected);
        // std::cout << "device_target_string: " << device_target_string << std::endl;
        // std::cout << "device_target_selected_string: " << device_target_selected_string << std::endl;
        if(!device_target_string.empty() && !device_target_selected_string.empty()) {
            if(device_target_string != device_target_selected_string) {
                // current device_target of the project differs from that selected in GUI.
                // Allow user to Apply the new target, or reset it to the current device_target.
                m_button_reset->setDisabled(false);
                m_button_apply->setDisabled(false);

                m_message_label->setText("Device Selection has changed!\n\n"
                                        "- Click 'Apply' to set the new Device.\n"
                                        "- Click 'Reset' to reset to the original Device.\n\n"
                                        "Task Settings will not reflect new Device until 'Apply' is executed!");
                m_message_label->show();
                // device_manager_widget->adjustSize();
            }
            else {
                // current device_target of the project is the same as the one selected in the GUI, no changes to apply.
                m_button_reset->setDisabled(true);
                m_button_apply->setDisabled(true);

                m_message_label->setText("");
                m_message_label->hide();
            }
        }
        // else if(!device_target_selected_string.empty()) {
        // // no currently set device target from JSON, probably new project.
        // // then, if the device_target_selected is via GUI, allow user to click apply
        // // so that we can do rest of the settings for selected device target...
        // m_button_apply->setDisabled(false);

        // m_message_label->setText("New Device Selection!\n\n"
        //                         "Click 'Apply' to set the new Device!\n\n"
        //                         "Task Settings will not reflect new Device\n"
        //                         "until 'Apply' is executed!");
        // m_message_label->show();
        // }
    }

    updateDeviceResourcesLabel();
  }

}


// refreshes m_device_resources_label from device_target_selected's current
// QLDeviceLayoutInfo. Recomputed live here rather than trusting the one-time
// whole-catalog scan's snapshot on device_target_selected.device_variant_layout:
// a FIXED/CUSTOM device's real counts are available the moment it is picked,
// and this selection has not gone through setCurrentDeviceTarget() yet (Apply
// has not been clicked), so nothing else would refresh that snapshot for it.
// QLDeviceLayoutInfo is the single gate deciding what is known right now - "-"
// rather than "0" for whatever it has not resolved yet (e.g. an AUTO/RESOURCES
// device before Packing(), or one only being previewed, not yet applied).
//
// Also refreshes device_layout.json on disk for this selection, by explicit
// request: every combo change - not only Apply - keeps that file matching
// whatever is currently picked here, deleting it when the pick is
// unresolved (AUTO/RESOURCES before Packing()).
void QLDeviceManager::updateDeviceResourcesLabel() {

  if(device_manager_widget == nullptr) {
    return;
  }

  QLDeviceLayoutInfo::refresh(device_target_selected);

  const auto resourceText = [](const std::optional<int>& count) {
    return count.has_value() ? QString::number(*count) : QString("-");
  };
  std::optional<int> clb_count, dsp_count, bram_count, io_count;
  for (const auto& [resource_name, resource_count] :
       deviceResourceInformation(device_target_selected)) {
    if (resource_name == "clb") clb_count = resource_count;
    if (resource_name == "dsp") dsp_count = resource_count;
    if (resource_name == "bram") bram_count = resource_count;
    if (resource_name == "io") io_count = resource_count;
  }
  QString archInfo;
  archInfo += "width: <b>" + QString::number(device_target_selected.device_variant_layout.width) + " </b>| ";
  archInfo += "height: <b>" + QString::number(device_target_selected.device_variant_layout.height) + " </b>| ";
  archInfo += "\n";
  archInfo += "clb: <b>" + resourceText(clb_count) + " </b>| ";
  archInfo += "dsp: <b>" + resourceText(dsp_count) + " </b>| ";
  archInfo += "bram: <b>" + resourceText(bram_count) + " </b>| ";
  archInfo += "io: <b>" + resourceText(io_count) + " </b>";
  m_device_resources_label->setText(archInfo);
}


// std::vector<std::shared_ptr<LayoutInfoHelper>>
// QLDeviceManager::ExtractDeviceAvailableResourcesFromVprLogContent(const std::string& content) const
// {
//   static QRegularExpression layoutPattern(R"(^Resource usage for device layout (\w+)...$)");
//   static QRegularExpression clbLogPattern(R"(^(\d+)\s+blocks of type: clb$)");
//   static QRegularExpression dspLogPattern(R"(^(\d+)\s+blocks of type: dsp$)");
//   static QRegularExpression bramLogPattern(R"(^(\d+)\s+blocks of type: bram$)");
//   static QRegularExpression ioLogPattern(R"(^(\d+)\s+blocks of type: io.+$)");

//   auto tryExtractSubInt = [](const QRegularExpression& pattern, const std::string& content) -> int {
//     int result = 0;
//     auto match = pattern.match(content.c_str());
//     if (match.hasMatch() && (match.lastCapturedIndex() == 1)) {
//       bool ok;
//       int candidate = match.captured(1).toInt(&ok);
//       if (ok) {
//         result = candidate;
//       }
//     }
//     return result;
//   };
//   auto tryExtractSubStr = [](const QRegularExpression& pattern, const std::string& content) -> std::string {
//     std::string result;
//     auto match = pattern.match(content.c_str());
//     if (match.hasMatch() && (match.lastCapturedIndex() == 1)) {
//       result = match.captured(1).toStdString();
//     }
//     return result;
//   };

//   QString buff(content.c_str());
//   QList<QString> lines = buff.split("\n");

//   std::vector<std::shared_ptr<LayoutInfoHelper>> result;
//   std::shared_ptr<LayoutInfoHelper> layoutInfo;
//   for (const QString& line: lines) {
//     std::string trimmedLine = line.trimmed().toStdString();
//     std::string layoutName = tryExtractSubStr(layoutPattern, trimmedLine);
//     if (!layoutName.empty() && (layoutName != "auto")) {
//       if (layoutInfo) {
//         result.push_back(layoutInfo);
//       }
//       layoutInfo = std::make_shared<LayoutInfoHelper>(layoutName);
//       layoutInfo->clb = 0;
//       layoutInfo->dsp = 0;
//       layoutInfo->bram = 0;
//       layoutInfo->io = 0;
//       continue;
//     }
//     int clb = tryExtractSubInt(clbLogPattern, trimmedLine);
//     if (layoutInfo && clb) {
//       layoutInfo->clb += clb;
//       continue;
//     }
//     int dsp = tryExtractSubInt(dspLogPattern, trimmedLine);
//     if (layoutInfo && dsp) {
//       layoutInfo->dsp += dsp;
//       continue;
//     }
//     int bram = tryExtractSubInt(bramLogPattern, trimmedLine);
//     if (layoutInfo && bram) {
//       layoutInfo->bram += bram;
//       continue;
//     }
//     int io = tryExtractSubInt(ioLogPattern, trimmedLine);
//     if (layoutInfo && io) {
//       layoutInfo->io += io;
//       continue;
//     }
//   }

//   if (layoutInfo) { // add last item
//     result.push_back(layoutInfo);
//   }

//   return result;
// }


// void QLDeviceManager::collectDeviceVariantAvailableResources(const QLDeviceVariant& device_variant) {
//   CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());
//   if (!compiler) {
//       return;
//   }

//   // create command to ask vpr to spit out resource information for the variant:
//   auto [architectureFile, isTemporary] = GetArchitectureFileForDeviceVariant(device_variant);
//   if (architectureFile.empty()) {
//     return;
//   }

//   // use a 'placeholder' blif as vpr requires a blif to run, though we don't use it in 'resource_usage' mode
//   std::filesystem::path blif_filepath = std::filesystem::canonical(GlobalSession->Context()->DataPath() /
//                                                                    std::filesystem::path("..") /
//                                                                    std::filesystem::path("scripts") / 
//                                                                    "and2.blif");

//   std::string vpr_command =
//       ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->m_vprExecutablePath.string() + std::string(" ") +
//       architectureFile.string() + std::string(" ") +
//       blif_filepath.string() + std::string(" ") +
//       std::string("--show_resource_usage on");

//   // execute vpr command
//   QProcess* process = compiler->ExecuteCommand(vpr_command);

//   // non-blocking: once the command executes, use the result and update the device_data structure to store the layout details:
//   QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), [this, process, device_variant, architectureFile, isTemporary](int exitCode) {
//     std::vector<std::shared_ptr<LayoutInfoHelper>> layoutsInfo =
//         ExtractDeviceAvailableResourcesFromVprLogContent(process->readAllStandardOutput().toStdString());
//     process->deleteLater();
//     if (isTemporary && std::filesystem::exists(architectureFile)) {
//       std::filesystem::remove(architectureFile);
//     }

//     if (exitCode == 0) {
//       for (const std::shared_ptr<LayoutInfoHelper>& layoutInfo: layoutsInfo) {
//         QLDeviceVariantLayout* device_layout = findDeviceLayoutVariantPtr(device_variant.family, device_variant.foundry, device_variant.node, device_variant.devicename, device_variant.voltage_threshold, device_variant.p_v_t_corner, layoutInfo->name);
//         if (device_layout) {
//           device_layout->bram = layoutInfo->bram;
//           device_layout->dsp = layoutInfo->dsp;
//           device_layout->clb = layoutInfo->clb;
//           device_layout->io = layoutInfo->io;
//         }
//       }
//       // update GUI with the acquired resource information.
//       triggerUIUpdate();
//     } else {
//       std::cout << "Cannot fetch layout available resources. Process finished with err code " << exitCode << std::endl;
//     }
//   });

// }


void QLDeviceManager::resetButtonClicked() {

  //device_target_selected = device_target;
  
  triggerUIUpdate();
}


void QLDeviceManager::applyButtonClicked() {

  // device_target should be set to device_target_selected
  setCurrentDeviceTarget(device_target_selected);

  triggerUIUpdate();
  
  // signal to the QLSettingsManager to update the settings JSON
  // to reflect the device_target selection!
  // change this to emit signal later...
  if(settings_manager != nullptr) {
    settings_manager->newProjectMode = newProjectMode;
    settings_manager->updateJSONSettingsForDeviceTarget(device_target);
  }
}


void QLDeviceManager::parseDeviceData() {

  std::string family;
  std::string foundry;
  std::string node;
  std::string devicename;

  std::error_code ec;

  // get all the device_data root dir paths to search, in precedence order
  std::vector<std::filesystem::path> root_device_data_dir_path_list = deviceDataRootDirPathList();

  // clear the list before parsing
  device_list.clear();

  // the dedupe in reportDeviceDataError() covers one walk: a re-parse after a
  // device was added or its config.json fixed has to be able to report again.
  reported_device_data_error_set.clear();

  // devices already discovered, keyed by device type string, so that the same device
  // appearing in two roots is reported rather than silently duplicated (REQ-006).
  std::set<std::string> discovered_device_type_set;

  // each dir in the device_data is a family
  //    for each family, check for foundry dirs
  //      for each foundry, check for node
  //        for each node, check for devicename
  //          for each family-foundry-node-devicename dir, check the device_variants

  // walk every root. roots earlier in the list win: the installation is first, so a device
  // shipped with the installation can never be shadowed by an externally installed one.
  for (const std::filesystem::path& root_device_data_dir_path : root_device_data_dir_path_list) {

  // look at the directories inside the device_data_dir_path for 'family' entries
  // NOTE: the error_code overload is required here - a registered root that has been
  // deleted or made unreadable must warn and be skipped, never abort startup (REQ-007).
  std::filesystem::directory_iterator root_dir_iterator =
                    std::filesystem::directory_iterator(root_device_data_dir_path, ec);
  if(ec) {
    std::cout << "WARNING: skipping unreadable device data root: "
              << root_device_data_dir_path.string() << " (" << ec.message() << ")" << std::endl;
    ec.clear();
    continue;
  }

  for (const std::filesystem::directory_entry& dir_entry_family : root_dir_iterator) {

    if(dir_entry_family.is_directory()) {

      // we would see family at this level
      family = dir_entry_family.path().filename().string();

      // look at the directories inside the 'family' dir for 'foundry' entries
      for (const std::filesystem::directory_entry& dir_entry_foundry :
                    std::filesystem::directory_iterator(dir_entry_family.path(), ec)) {

        if(dir_entry_foundry.is_directory()) {

          // we would see foundry at this level
          foundry = dir_entry_foundry.path().filename().string();

          // look at the directories inside the 'foundry' dir for 'node' entries
          for (const std::filesystem::directory_entry& dir_entry_node :
                          std::filesystem::directory_iterator(dir_entry_foundry.path(), ec)) {

            if(dir_entry_node.is_directory()) {

              // we would see devicenames at this level
              node = dir_entry_node.path().filename().string();

              // look at the directories inside the 'node' dir for 'devicename' entries
              for (const std::filesystem::directory_entry& dir_entry_devicename :
                              std::filesystem::directory_iterator(dir_entry_node.path(), ec)) {

                if(dir_entry_devicename.is_directory()) {

                  // we would see devices at this level
                  devicename = dir_entry_devicename.path().filename().string();

                  // if this device type was already found in an earlier (higher precedence)
                  // root, report the shadowing and keep the first one. resolving this
                  // silently is what makes "why is my flow using the wrong device data?"
                  // impossible to debug.
                  std::string device_type_string = DeviceTypeString(family, foundry, node, devicename);
                  if(discovered_device_type_set.count(device_type_string) > 0) {
                    std::cout << "WARNING: ignoring shadowed device: " << device_type_string << std::endl;
                    std::cout << "         already found in a higher precedence device data root;" << std::endl;
                    std::cout << "         ignored copy: " << dir_entry_devicename.path().string() << std::endl;
                    continue;
                  }

                  // a device is defined by its config.json. anything else four levels deep is
                  // simply not a device - a device-data checkout carries other things at that
                  // depth (QL-Benchmarks/testcases/<kind>/<design>, .git internals), and
                  // reporting those as malformed devices buries real errors in noise.
                  std::error_code config_ec;
                  if(!std::filesystem::exists(dir_entry_devicename.path() / "config.json", config_ec)) {
                    continue;
                  }

                  // get all the device_variants for this device, resolved against the
                  // device dir we are actually standing in - NOT re-derived from a global
                  // root, which would read another root's files for a shadowed device.
                  std::vector<QLDeviceVariant> device_variants = listDeviceVariantsInDeviceDirectory(family,
                                                                                    foundry,
                                                                                    node,
                                                                                    devicename,
                                                                                    dir_entry_devicename.path());

                  if(device_variants.empty()) {
                    // it HAS a config.json, so it means to be a device: this one is genuinely
                    // malformed and worth reporting. continue with the other devices.
                    std::cout << "error in parsing variants for device: " + family + "_" + foundry + "_" + node + "_" + devicename + "\n" << std::endl;
                  }
                  else {

                    QLDeviceType device;
                    device.family = family;
                    device.foundry = foundry;
                    device.node = node;
                    device.devicename = devicename;
                    device.device_variants = device_variants;
                    // remember which root this device came from, so that every path and key
                    // lookup for it resolves against its own root.
                    device.device_root_path = root_device_data_dir_path;

                    device_list.push_back(device);
                    discovered_device_type_set.insert(device_type_string);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  } // for each device_data root


  // parse resources information for each device variant in the device_list:
  for (QLDeviceType &device: device_list) {

    for (QLDeviceVariant &device_variant: device.device_variants) {

      for (QLDeviceVariantLayout &device_variant_layout: device_variant.device_variant_layouts) {

        QLDeviceTarget _device_target = convertToDeviceTarget(device.family,
                                                              device.foundry,
                                                              device.node,
                                                              device.devicename,
                                                              device_variant.voltage_threshold,
                                                              device_variant.p_v_t_corner,
                                                              device_variant_layout.name);

        std::vector<std::tuple<std::string, std::optional<int>>> resources_vector =
            deviceResourceInformation(_device_target);

        for (const auto& resource_tuple : resources_vector) {

          std::string resourcename = std::get<0>(resource_tuple);
          std::optional<int> resourcecount = std::get<1>(resource_tuple);

          // this part is a carry-over, we should make the layout resource structure
          // in c++ code generic, so we don't need to have these ifs ? TODO future
          if(resourcename == "clb") {
            device_variant_layout.clb = resourcecount;
          }
          if(resourcename == "io") {
            device_variant_layout.io = resourcecount;
          }
          if(resourcename == "dsp") {
            device_variant_layout.dsp = resourcecount;
          }
          if(resourcename == "bram") {
            device_variant_layout.bram = resourcecount;
          }
        }
      }
      // collectDeviceVariantAvailableResources(device_variant);
    }
  }


  // DEBUG
  // std::cout << "============ DEBUG++ ============" << std::endl;
  // for (QLDeviceType device: device_list) {
  //     std::cout << "Device: " + device.family + " " + device.foundry + " " + device.node + " " + device.devicename << std::endl;
  //     for (QLDeviceVariant variant: device.device_variants) {
  //       std::cout << "  Variant: " +  variant.voltage_threshold + " " + variant.p_v_t_corner << std::endl;
  //       for (QLDeviceVariantLayout layout: variant.device_variant_layouts) {
  //         std::cout <<  "    layout_name:" + layout.name + "\n" +
  //                       "              w:" + std::to_string(layout.width) + "\n" +
  //                       "              h:" + std::to_string(layout.height) + "\n" +
  //                       "            clb:" + std::to_string(layout.clb) + "\n" +
  //                       "             io:" + std::to_string(layout.io) + "\n" +
  //                       "           bram:" + std::to_string(layout.bram) + "\n" +
  //                       "            dsp:" + std::to_string(layout.dsp)
  //                       << std::endl;
  //       }
  //     }
  //     std::cout << "\n" << std::endl;
  // }
  // std::cout << "============ DEBUG-- ============" << std::endl;
  //DEBUG
}


std::vector<QLDeviceVariant> QLDeviceManager::listDeviceVariantsInDeviceDirectory(
    std::string family,
    std::string foundry,
    std::string node,
    std::string devicename,
    std::filesystem::path device_data_dir_path) {

  // prep an empty list of device variants for the current 'device'
  std::vector<QLDeviceVariant> device_variants;

  // std::string device_string = DeviceString(family,
  //                                          foundry,
  //                                          node,
  //                                          devicename,
  //                                          "",
  //                                          "",
  //                                          "");
  // std::cout << "parsing variants for: " + device_string << std::endl;


  // [1] check for valid path
  // convert to canonical path, which will also check that the path exists.
  std::error_code ec;
  std::filesystem::path device_data_dir_path_c = 
          std::filesystem::canonical(device_data_dir_path, ec);
  if(ec) {
    // error
    std::cout << "Please check if the path specified exists!" << std::endl;
    std::cout << "path: " + device_data_dir_path.string() << std::endl;
    return device_variants;
  }


  // [2] check dir structure of the device_data_dir_path
  // [2][a] atleast one set of vpr.xml and openfpga.xml files should exist.
  // [2][b] all xmls sets should be in directory structure as below:
  //          - device_data_dir_path/<ANY_DIR_NAME_VT>/<ANY_DIR_NAME_PVT_CORNER> (device_variants)
  //        <ANY_DIR_NAME_VT>(s) represent the Cell Threshold Voltage(s)
  //        <ANY_DIR_NAME_PVT_CORNER>(s) represent the PVT Corner(s) 
  // [2][c] check that we have all the (other)required XML files for the device
  
  // [2][a] search for all vpr.xml/openfpga.xml files, and check the dir paths:
  std::vector<std::filesystem::path> vpr_xml_files;
  std::vector<std::filesystem::path> openfpga_xml_files;
  for (const std::filesystem::directory_entry& dir_entry :
      std::filesystem::recursive_directory_iterator(device_data_dir_path_c,
                                                    std::filesystem::directory_options::skip_permission_denied,
                                                    ec)) {
    if(ec) {
      std::cout << std::string("failed listing contents of ") +
                              device_data_dir_path_c.string() << std::endl;
      return device_variants;
    }

    if(dir_entry.is_regular_file(ec)) {

      // this will match both .xml and .xml.en(encrypted) files
      std::string vpr_xml_pattern = "vpr\\.xml.*";
      std::string openfpga_xml_pattern = "openfpga\\.xml.*";
      
      if (std::regex_match(dir_entry.path().filename().string(),
                            std::regex(vpr_xml_pattern,
                              std::regex::icase))) {
        vpr_xml_files.push_back(dir_entry.path().string());
      }
      if (std::regex_match(dir_entry.path().filename().string(),
                            std::regex(openfpga_xml_pattern,
                              std::regex::icase))) {
        openfpga_xml_files.push_back(dir_entry.path().string());
      }
    }

    if(ec) {
      std::cout << std::string("error while checking: ") +  dir_entry.path().string() << std::endl;
      return device_variants;
    }
  }

  // sort the entries for easier processing
  std::sort(vpr_xml_files.begin(),vpr_xml_files.end());
  std::sort(openfpga_xml_files.begin(),openfpga_xml_files.end());

  // check that we have atleast one set.
  if(vpr_xml_files.size() == 0) {
    std::cout << "No VPR XML files were found in the source device data dir !" << std::endl;
    return device_variants;
  }
  if(openfpga_xml_files.size() == 0) {
    std::cout << "No OPENFPGA XML files were found in the source device data dir !" << std::endl;
    return device_variants;
  }

  // check that we have the same number of entries for both vpr.xml and openfpga.xml
  // as they should be travelling in pairs.
  if(vpr_xml_files.size() != openfpga_xml_files.size()) {
    std::cout << "Mismatched number of VPR XML(s) w.r.t OPENFPGA XML(s) !" << std::endl;
    return device_variants;
  }

  // [2][b] gather all the 'parent' dirs of the XMLs, and check that they are in the expected hierarchy
  std::vector<std::filesystem::path> vpr_xml_file_parent_dirs;
  std::vector<std::filesystem::path> openfpga_xml_file_parent_dirs;
  for(std::filesystem::path xmlpath : vpr_xml_files) {
    vpr_xml_file_parent_dirs.push_back(xmlpath.parent_path());
  }
  for(std::filesystem::path xmlpath : openfpga_xml_files) {
    openfpga_xml_file_parent_dirs.push_back(xmlpath.parent_path());
  }

  // sort the entries for easier processing
  std::sort(vpr_xml_file_parent_dirs.begin(),vpr_xml_file_parent_dirs.end());
  std::sort(openfpga_xml_file_parent_dirs.begin(),openfpga_xml_file_parent_dirs.end());

  // check that we have the same set of dir paths for both XMLs, as they travel in pairs.
  // redundant?
  if(vpr_xml_file_parent_dirs != openfpga_xml_file_parent_dirs) {
    std::cout << "Mismatched number of VPR XML(s) w.r.t OPENFPGA XML(s) !" << std::endl;
    return device_variants;
  }
  // now we can take any one of the file_dirs vector for further steps as they are the same.

  // debug prints
  // std::cout << "vpr xmls" << std::endl;
  // for(auto path : vpr_xml_files) std::cout << path << std::endl;
  // std::cout << std::endl;
  // std::cout << "openfpga xmls" << std::endl;
  // for(auto path : openfpga_xml_files) std::cout << path << std::endl;
  // std::cout << std::endl;
  // std::cout << "vpr xml dirs" << std::endl;
  // for(auto path : vpr_xml_file_parent_dirs) std::cout << path << std::endl;
  // std::cout << std::endl;
  // std::cout << "openfpga xml dirs" << std::endl;
  // for(auto path : openfpga_xml_file_parent_dirs) std::cout << path << std::endl;
  // std::cout << std::endl;

  // now that the dir paths for both xml(s) are identical vectors, take one of them.
  // each dir *should be* of the structure:
  // - source_device_data_dir_path/<voltage_threshold>/<p_v_t_corner> (for variants)
  //          <voltage_threshold> can be any name, usually something like LVT, RVT, ULVT ...
  //          <p_v_t_corner> can be any name, usually something like TYPICAL, BEST, WORST ...
  // from this vector, we can deduce all of the possible device variants, and check correctness of hierarchy
  for (std::filesystem::path dirpath: vpr_xml_file_parent_dirs) {

    // canonicalize to remove any trailing slashes and normalize path to full path
    std::filesystem::path dirpath_c = std::filesystem::canonical(dirpath, ec);
    if(ec) {
      // filesystem error
      return device_variants;
    }
    
    // get the dir-name component of the path, this is the p_v_t_corner
    std::string p_v_t_corner = dirpath_c.filename().string();
    
    // get the dir-name component of the parent of this path, this is the voltage_threshold
    std::string voltage_threshold = dirpath_c.parent_path().filename().string();
    
    // additionally check that p_v_t_corner dir is 2 levels down from the source_device_data_dir_path
    if(!std::filesystem::equivalent(dirpath_c.parent_path().parent_path(), device_data_dir_path_c)) {
      std::cout << dirpath_c.parent_path() << std::endl;
      std::cout << device_data_dir_path_c << std::endl;
      std::cout << "p_v_t_corner dirs with XMLs are not 2 levels down from the source_device_data_dir_path!!!" << std::endl;
      return device_variants;
    }

    // create the variant
    QLDeviceVariant device_variant;
    device_variant.family = family;
    device_variant.foundry = foundry;
    device_variant.node = node;
    device_variant.devicename = devicename;
    device_variant.voltage_threshold = voltage_threshold;
    device_variant.p_v_t_corner = p_v_t_corner;

    // list and store all the layouts available in this device_variant:
    // pass the device dir we were given: this device may live in an external root, and its
    // encrypted vpr.xml can only be decrypted with the _Supp.db sitting beside it.
    device_variant.device_variant_layouts = listDeviceVariantLayouts(family,
                                                                     foundry,
                                                                     node,
                                                                     devicename,
                                                                     voltage_threshold,
                                                                     p_v_t_corner,
                                                                     device_data_dir_path_c);

    // add the variant to the list
    device_variants.push_back(device_variant);
  }

  return device_variants;
}


std::vector<QLDeviceVariant> QLDeviceManager::listDeviceVariants(
    std::string family,
    std::string foundry,
    std::string node,
    std::string devicename) {

  // get to the device_data dir path of the root that owns THIS device (falls back to the
  // first root when the device is not in device_list yet)
  std::filesystem::path root_device_data_dir_path =
     deviceTypeRootDirPath(family, foundry, node, devicename);

  // calculate the device_data dir path for specified device
  std::filesystem::path device_data_dir_path = root_device_data_dir_path / family / foundry / node / devicename;
  // std::cout << "device_data dir: " + device_data_dir_path.string() << std::endl;

  // query the variants from this path:
  return listDeviceVariantsInDeviceDirectory(family, foundry, node, devicename, device_data_dir_path);

}


std::vector<QLDeviceVariantLayout> QLDeviceManager::listDeviceVariantLayouts(std::string family,
                                                                             std::string foundry,
                                                                             std::string node,
                                                                             std::string devicename,
                                                                             std::string voltage_threshold,
                                                                             std::string p_v_t_corner,
                                                                             std::filesystem::path device_data_dir_path) {
  std::vector<QLDeviceVariantLayout> device_variant_layouts = {};

  // the caller normally supplies the device dir (it is mandatory while parsing device data,
  // because the _Supp.db used to decrypt vpr.xml lives in that dir). when it is not given,
  // resolve it against the root that owns this device.
  if(device_data_dir_path.empty()) {

    std::filesystem::path root_device_data_dir_path =
        deviceTypeRootDirPath(family, foundry, node, devicename);

    device_data_dir_path = root_device_data_dir_path / family / foundry / node / devicename;
  }

  std::filesystem::path device_variant_dir = device_data_dir_path / voltage_threshold / p_v_t_corner;

  
  std::filesystem::path source_vpr_xml_filepath;
  std::filesystem::path vpr_xml_filepath;

  // check for unencrypted vpr xml file first:
  source_vpr_xml_filepath = device_variant_dir / "vpr.xml";
  
  if (FileUtils::FileExists(source_vpr_xml_filepath)) {
    // use this file as is
    vpr_xml_filepath = source_vpr_xml_filepath;
  }
  else {
    // we should have an encrypted vpr xml file:
    source_vpr_xml_filepath = device_variant_dir / "vpr.xml.en";

    if (!FileUtils::FileExists(source_vpr_xml_filepath)) {
      // this means we don't have a vpr xml file, which is an error!
      std::cout << "vpr xml: " + source_vpr_xml_filepath.string() << std::endl;
      std::cout << "vpr xml not found!" << std::endl;
      ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles();
      return device_variant_layouts;
    }

    // decrypt the encrypted vpr xml file. and then use that:
    CompilerOpenFPGA_ql* compiler =
        (CompilerOpenFPGA_ql*)GlobalSession->GetCompiler();
    vpr_xml_filepath = compiler->GenerateTempFilePath();

    if (!compiler->decryptDeviceFile(
            source_vpr_xml_filepath, vpr_xml_filepath, device_data_dir_path,
            DeviceTypeString(family, foundry, node, devicename))) {
      compiler->CleanTempFiles();
      return device_variant_layouts;
    }
  }


  // open file with Qt
  // qDebug() << "vpr xml" << QString::fromStdString(vpr_xml_filepath.string());
  QFile file(vpr_xml_filepath.string().c_str());
  if (!file.open(QFile::ReadOnly)) {
    std::cout << "Cannot open file: " + vpr_xml_filepath.string() << std::endl;
    ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles();
    return device_variant_layouts;
  }

  // parse as XML with Qt
  QDomDocument doc;
  if (!doc.setContent(&file)) {
    file.close();
    std::cout << "Incorrect file: " + vpr_xml_filepath.string() << std::endl;
    ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles();
    return device_variant_layouts;
  }
  file.close();
  ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles(); // the decrypted file is not needed anymore.


  QDomNodeList nodes = doc.elementsByTagName("fixed_layout");
  for(int i = 0; i < nodes.count(); i++) {
      QDomNode node = nodes.at(i);
      if(node.isElement()) {

          // we have a 'layout element'
          QLDeviceVariantLayout layout;

          // get the "name" attribute for the "fixed_layout" tag element
          std::string fixed_layout_name_str = node.toElement().attribute("name", "notfound").toStdString();
          layout.name = fixed_layout_name_str;

          // get the "width" attribute for the "fixed_layout" tag element
          std::string fixed_layout_width_str = node.toElement().attribute("width", "0").toStdString();
          try {
            layout.width = std::stoi(fixed_layout_width_str);
          }
          catch (std::invalid_argument const &e) {
            std::cout << "Bad input: std::invalid_argument thrown" << std::endl;
            layout.width = 0;
          }
          catch (std::out_of_range const &e) {
            std::cout << "Integer overflow: std::out_of_range thrown" << std::endl;
            layout.width = 0;
          }

          // get the "height" attribute for the "fixed_layout" tag element
          std::string fixed_layout_height_str = node.toElement().attribute("height", "0").toStdString();
          try {
            layout.height = std::stoi(fixed_layout_height_str);
          }
          catch (std::invalid_argument const &e) {
            std::cout << "Bad input: std::invalid_argument thrown" << std::endl;
            layout.height = 0;
          }
          catch (std::out_of_range const &e) {
            std::cout << "Integer overflow: std::out_of_range thrown" << std::endl;
            layout.height = 0;
          }

          // obtain resource information for each layout using vpr is done asynchronously.

          device_variant_layouts.push_back(layout);
      }
  }

  return device_variant_layouts;
}

std::string QLDeviceManager::DeviceString(std::string family,
                                          std::string foundry,
                                          std::string node,
                                          std::string devicename,
                                          std::string voltage_threshold,
                                          std::string p_v_t_corner,
                                          std::string layout_name) {

  // form the string representation of the device
  std::string device_string = family + "_" + foundry + "_" + node + "_" + devicename;

  if(!voltage_threshold.empty() && !p_v_t_corner.empty()) {
    device_string += "_" + voltage_threshold + "_" + p_v_t_corner;
  }

  if(!layout_name.empty()) {
    device_string += "_" + layout_name;
  }

  return device_string;
}


std::string QLDeviceManager::DeviceTypeString(std::string family,
                                              std::string foundry,
                                              std::string node,
                                              std::string devicename) {

  // form the string representation of the devicetype
  std::string device_type_string = family + "_" + foundry + "_" + node + "_" + devicename;

  return device_type_string;
}


bool QLDeviceManager::DeviceExists(std::string family,
                                   std::string foundry,
                                   std::string node,
                                   std::string devicename,
                                   std::string voltage_threshold,
                                   std::string p_v_t_corner,
                                   std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,devicename,voltage_threshold,p_v_t_corner,layout_name);

  return DeviceExists(device_string);
}


bool QLDeviceManager::DeviceExists(std::string device_string) {

  QLDeviceTarget device_target = convertToDeviceTarget(device_string);
  if(isDeviceTargetValid(device_target)) {
       return true;
  }

  return false;
}


bool QLDeviceManager::DeviceExists(QLDeviceTarget device_target) {

  // loop through the device_list and check if a matching device exists.

  for (QLDeviceType device: device_list) {
    for (QLDeviceVariant device_variant: device.device_variants) {
      for (QLDeviceVariantLayout device_variant_layout: device_variant.device_variant_layouts) {
        if(device_target.device_variant.family            == device_variant.family &&
           device_target.device_variant.foundry           == device_variant.foundry &&
           device_target.device_variant.node              == device_variant.node &&
           device_target.device_variant.devicename        == device_variant.devicename &&
           device_target.device_variant.voltage_threshold == device_variant.voltage_threshold &&
           device_target.device_variant.p_v_t_corner      == device_variant.p_v_t_corner &&
           device_target.device_variant_layout.name       == device_variant_layout.name) {

          return true;
        }
      }
    }
  }

  return false;
}


QLDeviceTarget QLDeviceManager::convertToDeviceTarget(std::string family,
                                                std::string foundry,
                                                std::string node,
                                                std::string devicename,
                                                std::string voltage_threshold,
                                                std::string p_v_t_corner,
                                                std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,devicename,voltage_threshold,p_v_t_corner,layout_name);

  return convertToDeviceTarget(device_string);
}


QLDeviceTarget QLDeviceManager::convertToDeviceTarget(std::string device_string) {

  QLDeviceTarget device_target;

  // loop through the device_list and check if a matching device exists.

  for (QLDeviceType device: device_list) {
    for (QLDeviceVariant device_variant: device.device_variants) {
      for (QLDeviceVariantLayout device_variant_layout: device_variant.device_variant_layouts) {
        std::string current_device_string = DeviceString(device_variant.family,
                                                         device_variant.foundry,
                                                         device_variant.node,
                                                         device_variant.devicename,
                                                         device_variant.voltage_threshold,
                                                         device_variant.p_v_t_corner,
                                                         device_variant_layout.name);
        if(current_device_string == device_string) {
          device_target.device_variant_layout = device_variant_layout;
          device_target.device_variant = device_variant;
          return device_target;
        }
      }
    }
  }

  return device_target;
}

bool QLDeviceManager::isDeviceTargetValid(QLDeviceTarget device_target) {

  if(!device_target.device_variant.family.empty() && 
     !device_target.device_variant.foundry.empty() && 
     !device_target.device_variant.node.empty() && 
     !device_target.device_variant.devicename.empty() && 
     !device_target.device_variant.voltage_threshold.empty() && 
     !device_target.device_variant.p_v_t_corner.empty() && 
     !device_target.device_variant_layout.name.empty()) {
       return true;
  }

  return false;
}

void QLDeviceManager::setCurrentDeviceTarget(std::string family,
                                             std::string foundry,
                                             std::string node,
                                             std::string devicename,
                                             std::string voltage_threshold,
                                             std::string p_v_t_corner,
                                             std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,devicename,voltage_threshold,p_v_t_corner,layout_name);
  
  setCurrentDeviceTarget(device_string);
}

void QLDeviceManager::setCurrentDeviceTarget(std::string device_string) {

  QLDeviceTarget device_target = convertToDeviceTarget(device_string);

  setCurrentDeviceTarget(device_target);

}


void QLDeviceManager::setCurrentDeviceTarget(QLDeviceTarget device_target) {

  // std::cout << "setCurrentDeviceTarget" << std::endl;
  // std::cout << "newProjectMode: " << newProjectMode << std::endl;
  // std::cout << "device_target: " << convertToDeviceString(device_target) << std::endl;

  if(isDeviceTargetValid(device_target)) {

    this->device_target = device_target;
  }
  else {

    // invalid target device in "device_target" - JSON is incorrect, or missing?
    QLDeviceTarget empty_device_target;
    this->device_target = empty_device_target;
  }

  // The device determines its config.json, and for a FIXED or LAYOUT_MODE: CUSTOM
  // package that file already states the fabric geometry - so this is the earliest
  // point device_layout.json can be written. AUTO and RESOURCES have no answer yet
  // and are picked up again at the end of packing.
  QLDeviceLayoutInfo::refresh(this->device_target);
}


// std::pair<std::filesystem::path, bool> QLDeviceManager::GetArchitectureFileForDeviceVariant(const QLDeviceVariant& device_variant)
// {
//   bool is_temporary = false;
//   std::filesystem::path architectureFile;
//   std::filesystem::path device_type_dir_path =
//       std::filesystem::path(deviceDataRootDirPath() /
//                             device_variant.family /
//                             device_variant.foundry /
//                             device_variant.node /
//                             device_variant.devicename);

//   std::filesystem::path device_variant_dir_path =
//       std::filesystem::path(deviceDataRootDirPath() /
//                             device_variant.family /
//                             device_variant.foundry /
//                             device_variant.node /
//                             device_variant.devicename /
//                             device_variant.voltage_threshold /
//                             device_variant.p_v_t_corner);

//   // prefer to use the unencrypted file, if available.
//   architectureFile =
//       std::filesystem::path(device_variant_dir_path / std::string("vpr.xml"));

//   // if not, use the encrypted file after decryption.
//   std::error_code ec;
//   if (!std::filesystem::exists(architectureFile, ec)) {

//     std::filesystem::path vpr_xml_en_path =
//           std::filesystem::path(device_variant_dir_path / std::string("vpr.xml.en"));
//     architectureFile = ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->GenerateTempFilePath(true);
//     is_temporary = true;

//     std::filesystem::path cryptdbPath =
//         CRFileCryptProc::getInstance()->getCryptDBFileName(device_type_dir_path.string(),
//                                                            device_variant.family +
//                                                            "_" +
//                                                            device_variant.foundry +
//                                                            "_" +
//                                                            device_variant.node +
//                                                            "_" +
//                                                            device_variant.devicename);

//     if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(cryptdbPath.string())) {
//       std::cout << "load cryptdb failed!" << std::endl;
//       return std::make_pair(std::filesystem::path(""), is_temporary);
//     }

//     if (!CRFileCryptProc::getInstance()->decryptFile(vpr_xml_en_path, architectureFile)) {
//       std::cout << "decryption failed!" << std::endl;
//       return std::make_pair(std::filesystem::path(""), is_temporary);
//     }
//   }

//   //Message( std::string("Using vpr.xml for: ") + convertToDeviceString(device_variant) );
//   return std::make_pair(architectureFile, is_temporary);
// }


std::string QLDeviceManager::getCurrentDeviceTargetString() {

  return convertToDeviceString(getCurrentDeviceTarget());

}


QLDeviceTarget QLDeviceManager::getCurrentDeviceTarget() {

  return this->device_target;
}


void QLDeviceManager::triggerUIUpdate() {

  // set the selected device in the GUI:
  // newProjectMode : set the device selected in device_target_newproject
  // existingProjectMode : set the device selected in device_target_selected

  // to trigger the GUI update, we update the 'family' combobox, which cascades and
  // terminates at layoutChanged()

  if(device_manager_widget != nullptr) {
    if(newProjectMode) {
      // trigger GUI to default selection (index0 of all fields...)
      m_combobox_family->blockSignals(true);
      m_combobox_family->setCurrentIndex(-1);
      m_combobox_family->blockSignals(false);
      m_combobox_family->setCurrentIndex(0);
    }
    else {
      // existingProjectMode

      // check if the device_target contains a valid target, if so, set the ball rolling:
      if( DeviceExists(this->device_target) ) {

        int index;
        index = m_combobox_family->findText(QString::fromStdString(this->device_target.device_variant.family));
        // std::cout << "m_combobox_family index" << index << std::endl;
        m_combobox_family->blockSignals(true);
        m_combobox_family->setCurrentIndex(-1);
        m_combobox_family->blockSignals(false);
        currentDeviceTargetUpdateInProgress = true;
        m_combobox_family->setCurrentIndex(index);
      }
      else {
        // invalid device, ignore for now.
        std::cout << "existing-project, but device_target is invalid, json parse problem??" << std::endl;
      }
    }
  }
}


std::string QLDeviceManager::convertToFoundryNode(std::string foundry, std::string node) {

    std::string foundrynode = foundry + " - " + node;
    return foundrynode;
}


std::vector<std::string> QLDeviceManager::convertFromFoundryNode(std::string foundrynode) {

    std::vector<std::string> tokens;
    StringUtils::tokenize(foundrynode, " - ", tokens);
    return tokens;

    // Qt:
    // std::vector<std::string> tokens;
    // QString foundrynode_qstring = QString::fromStdString(foundrynode);
    // QRegExp delimiterExp(" - ");
    // QStringList foundrynodelist = foundrynode_qstring.split(delimiterExp);
    // for(QString token: foundrynodelist) {
    //   tokens.push_back(token.toStdString());
    // }
}

std::string QLDeviceManager::convertToDeviceString(QLDeviceTarget device_target) {

  // form the string representation of the device
  std::string device_string;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }


  device_string = DeviceString(device_target.device_variant.family,
                                device_target.device_variant.foundry,
                                device_target.device_variant.node,
                                device_target.device_variant.devicename,
                                device_target.device_variant.voltage_threshold,
                                device_target.device_variant.p_v_t_corner,
                                device_target.device_variant_layout.name);

  return device_string;

}


std::string QLDeviceManager::convertToDeviceTypeString(QLDeviceTarget device_target) {

  // form the string representation of the devicetype
  std::string device_type_string;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }


  device_type_string = DeviceTypeString(device_target.device_variant.family,
                                device_target.device_variant.foundry,
                                device_target.device_variant.node,
                                device_target.device_variant.devicename);

  return device_type_string;
}


// encryptDevice-> take input device data -> produce encrypted version
// addDevice -> call encryptDevice -> take encrypted version -> copy into aurora installation

int QLDeviceManager::encryptDevice(std::string family, std::string foundry, std::string node, std::string devicename,
                                     std::string device_data_source, std::string device_data_target,
                                     std::string customer_id) {
    CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

    // encrypt_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> <target_device_data_dir_path>
    // 1. ensure that the structure in the <source_device_data_dir_path> reflects 
    //      required structure, as specified in the document: <TODO>
    //    basically, all the required files should exist, in the right hierarchy,
    //      and missing optional files would output a warning.
    // 2. encrypt all the files in the <source_device_data_dir_path> in place
    // 3. copy over all the encrypted files & cryption db
    //      from: <source_device_data_dir_path>
    //      to: <INSTALLATION> / device_data / <family> / <foundry> / <node> / <devicename>
    //      and clean up all the encrypted files & cryption db from the <source_device_data_dir_path>


    std::filesystem::path source_device_data_dir_path = device_data_source;

    std::string device = DeviceString(family,
                                      foundry,
                                      node,
                                      devicename,
                                      "",
                                      "",
                                      "");

    // convert to canonical path, which will also check that the path exists.
    std::error_code ec;
    std::filesystem::path source_device_data_dir_path_c = 
            std::filesystem::canonical(source_device_data_dir_path, ec);
    if(ec) {
      // error
      compiler->ErrorMessage("QLDeviceManager::encryptDevice() failed!\n");
      compiler->ErrorMessage("Please check if the path specified exists!\n");
      compiler->ErrorMessage("path: " + source_device_data_dir_path.string());
      return -1;
    }

    std::filesystem::path target_device_data_dir_path;
    // if unspecified, default destination for the encrypted files will be in 
    // a dir parallel to the source dir, with "_en" added to the name of the source dir.
    if (device_data_target.empty()) {
      std::string target_device_data_dir_name = source_device_data_dir_path_c.filename().string() + "_en";
      target_device_data_dir_path = source_device_data_dir_path_c / ".." / target_device_data_dir_name;
    }
    else {
      target_device_data_dir_path = device_data_target;
    }

    // debug prints
    // std::cout << std::endl;
    // std::cout << "family: " << family << std::endl;
    // std::cout << "foundry: " << foundry << std::endl;
    // std::cout << "node: " << node << std::endl;
    // std::cout << "devicename: " << devicename << std::endl;
    // std::cout << "source_device_data_dir_path: " << source_device_data_dir_path_c << std::endl;
    // std::cout << "force: " << std::string(force?"true":"false") << std::endl;
    // std::cout << std::endl;


    if (std::filesystem::exists(target_device_data_dir_path, ec)) {
        compiler->Message("\nWARNING: The target device data dir already exists.");
        compiler->Message("device:      " + device);
        compiler->Message("target path: " + target_device_data_dir_path.string());
        compiler->Message("this will overwrite the target device dir with new files.");
        compiler->Message("\n");
    }
    else {
        compiler->Message("\nNew Device files will be created.");
        compiler->Message("device:      " + device);
        compiler->Message("target path: " + target_device_data_dir_path.string());
        compiler->Message("\n");
    }


    // [2] check dir structure of the source_device_data_dir_path of the device to be added
    // and return the list of device_variants if everything is ok.
    std::vector<QLDeviceVariant> device_variants;
    device_variants = listDeviceVariantsInDeviceDirectory(family,
                                                          foundry,
                                                          node,
                                                          devicename,
                                                          source_device_data_dir_path_c);

    if(device_variants.empty()) {
      compiler->ErrorMessage(std::string("error parsing device_data in: ") +
                               source_device_data_dir_path_c.string());
        return -1;
    }
    else {
      for (QLDeviceVariant variant: device_variants) {
        std::cout << "  variant: " +  variant.family + " " + variant.foundry + " " + variant.node + " " + variant.devicename + " " + variant.voltage_threshold + " " + variant.p_v_t_corner << std::endl;
        for (QLDeviceVariantLayout layout: variant.device_variant_layouts) {
          std::cout <<  "    layout_name:" + layout.name + "\n" +
                        "              w:" + std::to_string(layout.width) + "\n" +
                        "              h:" + std::to_string(layout.height) //+ "\n" +
                        // "            clb:" + std::to_string(layout.clb) + "\n" +
                        // "             io:" + std::to_string(layout.io) + "\n" +
                        // "           bram:" + std::to_string(layout.bram) + "\n" +
                        // "            dsp:" + std::to_string(layout.dsp)
                        << std::endl;
        }
      }
    }

    // collect the list of every filepath in the source_device_data_dir that we want to encrypt.
    std::vector<std::filesystem::path> source_device_data_file_list_to_encrypt;
    std::vector<std::filesystem::path> source_device_data_file_list_to_copy;

    // files that matched no classification rule at all. reported individually as they are
    // found, and summarised at the end so the count is not lost in the packaging output.
    int unclassified_file_count = 0;

    // include pin_table csv files for copy
    std::filesystem::path device_target_config_json_filepath = source_device_data_dir_path / std::string("config.json");
    std::ifstream device_target_config_json_ifstream(device_target_config_json_filepath.string());
    json device_target_config_json = json::parse(device_target_config_json_ifstream);
    // get json value
    std::string pin_table_value;
    std::filesystem::path pin_table_path;
    if( device_target_config_json.contains("PIN_TABLE")  ) {
      pin_table_value = device_target_config_json["PIN_TABLE"].get<std::string>();
    }
    // CRR devices ship SB_MAPS + switchbox-template CSVs instead of a pre-built
    // rr_graph.  Including rr_graph.bin / router_lookahead.bin in the encrypted
    // package would cause VPR to use the pre-built graph and completely bypass the
    // encrypted --sb_maps path, defeating in-memory CRR decryption.
    const bool is_crr_device = device_target_config_json.contains("SB_MAPS");
    if (!pin_table_value.empty()) {
      if (std::filesystem::is_regular_file(source_device_data_dir_path / pin_table_value)) {
        pin_table_path = source_device_data_dir_path / pin_table_value;
      }
    }
    for (const std::filesystem::directory_entry& dir_entry :
        std::filesystem::recursive_directory_iterator(source_device_data_dir_path_c,
                                                      std::filesystem::directory_options::skip_permission_denied,
                                                      ec))
    {
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed listing contents of ") +  source_device_data_dir_path.string());
        return -1;
      }

      if(dir_entry.is_regular_file(ec)) {

          // every regular file must end up in exactly one of three buckets: encrypted, copied,
          // or DELIBERATELY EXCLUDED. anything matching no rule is reported at the end of this
          // block instead of being silently dropped - silent drops are how a device could be
          // packaged without its routing graph and nobody notice (aurora2#2189).
          // deliberate exclusions 'continue' out of the block and are never reported.
          //
          // classification is detected by watching the two lists rather than by a flag each
          // rule has to remember to set: several rules deliberately fall through to later ones,
          // and a flag would silently rot the first time a rule is added without it.
          const size_t encrypt_list_size_before = source_device_data_file_list_to_encrypt.size();
          const size_t copy_list_size_before = source_device_data_file_list_to_copy.size();

          // skip entries which are in specific directories in device data:
          // skip '/extra/' or '\extra\'
          if (std::regex_match(dir_entry.path().string(),
                                std::regex(R"(.+[\/\\]extra[\/\\].*)",
                                std::regex::icase))) {
            continue;
          }
          // skip any 'design_variables.yml'
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*design_variables\\.yml",
                                  std::regex::icase))) {
            continue;
          }
          // skip any 'vpr-template.xml'
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*vpr-template\\.xml",
                                  std::regex::icase))) {
            continue;
          }
          // skip any 'openfpga-template.xml'
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*openfpga-template\\.xml",
                                  std::regex::icase))) {
            continue;
          }
          // skip 'tileable_rrgraph/' and any spreadsheet in the device tree.
          // SECURITY: the switchbox_*.xlsx under tileable_rrgraph are the PLAINTEXT source
          // that the encrypted CSV/** files are generated from. shipping them would hand over
          // exactly the proprietary routing data the CSV encryption exists to protect.
          if (std::regex_match(dir_entry.path().string(),
                                std::regex(R"(.+[\/\\]tileable_rrgraph[\/\\].*)",
                                std::regex::icase))) {
            continue;
          }
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*\\.xlsx", std::regex::icase))) {
            continue;
          }
          // skip provenance and readme files. these are upstream bookkeeping, and no device
          // shipped in the installation carries them - the kit manifest records provenance.
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*\\.sha", std::regex::icase)) ||
              std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*\\.md", std::regex::icase))) {
            continue;
          }

          // include all files in 'CSV/' for encryption, required for RRG generation
          if (std::regex_match(dir_entry.path().string(),
                                std::regex(R"(.+[\/\\]CSV[\/\\].*)",
                                std::regex::icase))) {
            source_device_data_file_list_to_encrypt.push_back(dir_entry.path().string());
            continue;  // prevent matching further copy rules (e.g. pin_table.csv)
          }
          // include SB_MAPS.yml file for encryption, required for RRG generation
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*SB_MAPS\\.yml",
                                  std::regex::icase))) {
              source_device_data_file_list_to_encrypt.push_back(dir_entry.path().string());
              continue;  // prevent matching further copy rules
          }

          // include all files in 'examples/' for copy.
          //
          // NOTE: do NOT exclude these here. encryptDevice() transforms a device tree in place
          // for whoever calls it, and aurora2's DEVICE_INSTALL rule calls it for every CRR
          // device when ENABLE_ENCRYPTION=on. Dropping examples/ here would silently remove
          // from the INSTALLATION files that exist in the source. A device kit must not ship
          // examples, but that is the kit generator's job, not this function's.
          //
          // Per-device examples/ is being retired (the release ships one device-agnostic
          // examples/ tree instead). Nothing here requires it to exist: when the directory is
          // absent no path matches this rule and the loop simply moves on.
          if (std::regex_match(dir_entry.path().string(),
                                std::regex(R"(.+[\/\\]examples[\/\\].*)",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // routing graph / router lookahead handling.
          //
          // for a CRR device these MUST NOT be packaged, in ANY form: the device regenerates
          // its routing graph at runtime from the encrypted SB_MAPS + switchbox templates, and
          // shipping a pre-built graph would make VPR load that instead and bypass the
          // encrypted path entirely - silently defeating the encryption the package exists to
          // provide. this is a security property, not tidiness.
          //
          // note the source tree of some CRR devices (IDAHO-2025Q4-*) really does contain
          // these files, compressed and/or split, and a previous build may also have left a
          // decompressed one behind. so match the artifact by name in any form rather than
          // enumerating extensions - and mark it EXCLUDED so it does not get reported as
          // unclassified on every package of those devices.
          // match the graph BINARY and its compressed/split forms only - the pattern must
          // contain "bin". matching any extension would also swallow router_lookahead.capnp
          // and silently disable the .capnp ENCRYPTION rule below.
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*(rr_graph|router_lookahead)\\..*bin.*",
                                  std::regex::icase))) {

            if (is_crr_device) {
              // deliberately excluded
              continue;
            }

            // non-CRR devices depend on a pre-built graph, so copy the assembled binaries.
            // (non-CRR devices are out of scope for the device kit today; this keeps
            // add_device working for them.)
            if (std::regex_match(dir_entry.path().filename().string(),
                                    std::regex(".*rr_graph\\.bin",
                                    std::regex::icase)) ||
                std::regex_match(dir_entry.path().filename().string(),
                                    std::regex(".*router_lookahead\\.bin",
                                    std::regex::icase))) {
                source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
            }
            else {
              // a compressed/split graph archive for a non-CRR device: not usable as-is.
              // excluded deliberately rather than dropped silently.
              continue;
            }
          }

          // include any python scripts to copy
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".+\\.py",
                                  std::regex::icase))) {
              source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include any already encrypted files to copy
          if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".+\\.en",
                                  std::regex::icase))) {
              source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // we want xml files for encryption
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.xml",
                                std::regex::icase))) {
            // exclude fpga_io_map xml files from encryption
            // include them for copy
            if (std::regex_match(dir_entry.path().filename().string(),
                                  std::regex(".*fpga_io_map\\.xml",
                                  std::regex::icase))) {
              source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
              continue;
            }
            source_device_data_file_list_to_encrypt.push_back(dir_entry.path().string());
          }

          // we want capnp files for encryption (router_lookahead.capnp)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.capnp",
                                std::regex::icase))) {
            source_device_data_file_list_to_encrypt.push_back(dir_entry.path().string());
          }

          if (!pin_table_path.empty() && std::filesystem::equivalent(dir_entry.path(), pin_table_path)) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include template json files for copy
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+_template\\.json",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include yosys template script for copy (aurora_template_script.ys)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex("aurora_template_script\\.ys",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include synplify template script for copy (aurora_template_script.prj)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex("aurora_template_script\\.prj",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include openfpga template script for copy (aurora_template_script.openfpga)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex("aurora_template_script\\.openfpga",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include verilog files for copy (cells_sim.v etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.v",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include system verilog files for copy (cells_sim.sv etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.sv",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include txt files for copy (brams.txt etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.txt",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include json file for copy (config.json etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.json",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include md5 file for copy (SOFTWARE_RELEASE.md5 etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.md5",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // include au file for copy (.bitstream_enable.au etc.)
          if (std::regex_match(dir_entry.path().filename().string(),
                                std::regex(".+\\.au",
                                std::regex::icase))) {
            source_device_data_file_list_to_copy.push_back(dir_entry.path().string());
          }

          // nothing claimed this file. it is NOT silently dropped - report it, so a new kind
          // of device data file cannot go missing from a package unnoticed. deliberate
          // exclusions never reach here; they 'continue' above.
          if(source_device_data_file_list_to_encrypt.size() == encrypt_list_size_before &&
             source_device_data_file_list_to_copy.size() == copy_list_size_before) {

            std::error_code relative_ec;
            std::filesystem::path unclassified_relative_path =
                std::filesystem::relative(dir_entry.path(), source_device_data_dir_path_c, relative_ec);

            compiler->Message(std::string("WARNING: file matches no encrypt/copy/exclude rule, "
                                          "it will NOT be in the device package: ") +
                              (relative_ec ? dir_entry.path().string()
                                           : unclassified_relative_path.string()));
            unclassified_file_count++;
          }
      }

      if(ec) {
        compiler->ErrorMessage(std::string("error while checking: ") +  dir_entry.path().string());
        return -1;
      }
    }

    // debug prints
    // std::sort(source_device_data_file_list_to_encrypt.begin(),source_device_data_file_list_to_encrypt.end());
    // std::cout << "source_device_data_file_list_to_encrypt" << std::endl;
    // for(auto path : source_device_data_file_list_to_encrypt) std::cout << path << std::endl;
    // std::cout << std::endl;

    // Generate a fresh key database and encrypt each file in the list.
    // Use the caller-supplied customer id (from "--customer-id") when provided;
    // otherwise fall back to the device type string as the stored metadata.
    const std::string key_customer_id =
        customer_id.empty() ? DeviceTypeString(family, foundry, node, devicename)
                            : customer_id;
    qlcrypt::KeyDB keys;
    if (auto s = qlcrypt::KeyDB::create(keys, key_customer_id);
        !qlcrypt::ok(s)) {
      compiler->ErrorMessage(std::string("keygen failed: ") + std::string(qlcrypt::toString(s)));
      return -1;
    }

    // encrypt the list of files (in place: produces <src>.en; original kept)
    {
      qlcrypt::FileCrypt fc(keys);
      for (const auto& src : source_device_data_file_list_to_encrypt) {
        const std::string outPath = src.string() + ".en";
        if (auto s = fc.encryptFile(src.string(), outPath); !qlcrypt::ok(s)) {
          compiler->ErrorMessage(std::string("encrypt failed: ") + src.string() +
                                 " -> " + std::string(qlcrypt::toString(s)));
          return -1;
        }
      }
      compiler->Message("files encrypted ok.");
    }

    // save cryptdb as <dir>/<family>_Supp.db
    const std::filesystem::path cryptdb_path =
        source_device_data_dir_path_c /
        (DeviceTypeString(family, foundry, node, devicename) + "_Supp.db");
    if (auto s = keys.save(cryptdb_path.string()); !qlcrypt::ok(s)) {
      compiler->ErrorMessage(std::string("cryptdb save failed: ") + std::string(qlcrypt::toString(s)));
      return -1;
    }
    compiler->Message("cryptdb saved ok.");
    const std::string cryptdb_path_str = cryptdb_path.string();

    // [4] copy all encrypted files and cryptdb into the installation target_device_data_dir_path
    //     also, cleanup these files in the source_device_data_dir_path

    // delete the target_device_data_dir_path directory in the installation
    //   so that, we don't have a mix of old remnants and new files.
    std::filesystem::remove_all(target_device_data_dir_path,
                                ec);
    if(ec) {
      // error
      compiler->ErrorMessage(std::string("failed to delete target dir: ") + target_device_data_dir_path.string());
      return -1;
    }

    // create the target_device_data_dir_path directory in the installation
    std::filesystem::create_directories(target_device_data_dir_path,
                                        ec);
    if(ec) {
      // error
      compiler->ErrorMessage(std::string("failed to create target dir: ") + target_device_data_dir_path.string());
      return -1;
    }

    // pass through the list of files we prepared earlier for encryption and process each one
    for(std::filesystem::path source_file_path : source_device_data_file_list_to_encrypt) {

      // corresponding encrypted file path
      std::filesystem::path source_en_file_path = 
          std::filesystem::path(source_file_path.string() + ".en");

      // get the encrypted file path, relative to the source_device_data_dir_path
      std::filesystem::path relative_en_file_path = 
          std::filesystem::relative(source_en_file_path,
                                    source_device_data_dir_path_c,
                                    ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to create relative path: ") + source_en_file_path.string());
        return -1;
      }

      // add the relative encrypted file path to the target_device_data_dir_path
      std::filesystem::path target_en_file_path = 
          target_device_data_dir_path / relative_en_file_path;

      // ensure that the target encrypted file's parent dir is created if not existing:
      std::filesystem::create_directories(target_en_file_path.parent_path(),
                                          ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to create directory: ") + target_en_file_path.parent_path().string());
        return -1;
      }

      // copy the source encrypted file to the target encrypted file path:
      // MinGW g++ bug? overwrite_existing, still throws error if it exists? hence the check below.
      if(FileUtils::FileExists(target_en_file_path)) {
        std::filesystem::remove(target_en_file_path);
      }
      std::cout << "copying:" << relative_en_file_path << std::endl;
      FileUtils::overwriteFile(source_en_file_path,
                               target_en_file_path,
                               ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to copy: ") + source_en_file_path.string());
        return -1;
      }

      // delete the source encrypted file, as it not needed anymore.
      std::filesystem::remove(source_en_file_path,
                              ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to delete: ") + source_en_file_path.string());
        return -1;
      }
    }

    // now copy the cryptdb file into the installation and delete from the source
    std::filesystem::path source_cryptdb_path = cryptdb_path_str;

    // get the cryptdb file path, relative to the source_device_data_dir_path
    std::filesystem::path relative_cryptdb_path =
        std::filesystem::relative(source_cryptdb_path,
                                  source_device_data_dir_path_c,
                                  ec);
    if(ec) {
      // error
      compiler->ErrorMessage(std::string("failed to create relative path: ") + source_cryptdb_path.string());
        return -1;
    }

    // add the relative encrypted file path to the target_device_data_dir_path
    std::filesystem::path target_cryptdb_path =
        target_device_data_dir_path / relative_cryptdb_path;

    // copy the source cryptdb file to the target cryptdb file path:
    // MinGW g++ bug? overwrite_existing, still throws error if it exists? hence the check below.
    if(FileUtils::FileExists(target_cryptdb_path)) {
      std::filesystem::remove(target_cryptdb_path);
    }
    std::cout << "copying:" << relative_cryptdb_path << std::endl;
    FileUtils::overwriteFile(source_cryptdb_path,
                             target_cryptdb_path,
                             ec);
    if(ec) {
      // error
      compiler->ErrorMessage(std::string("failed to copy: ") + source_cryptdb_path.string());
      return -1;
    }

    // delete the source encrypted file, as it not needed anymore.
    std::filesystem::remove(source_cryptdb_path,
                            ec);
    if(ec) {
      // error
      compiler->ErrorMessage(std::string("failed to delete: ") + source_cryptdb_path.string());
      return -1;
    }

    // pass through the list of files we prepared earlier for copying without encryption and process each one
    for(std::filesystem::path source_file_path : source_device_data_file_list_to_copy) {

      // get the file path, relative to the source_device_data_dir_path
      std::filesystem::path relative_file_path = 
          std::filesystem::relative(source_file_path,
                                    source_device_data_dir_path_c,
                                    ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to create relative path: ") + source_file_path.string());
        return -1;
      }

      // add the relative file path to the target_device_data_dir_path
      std::filesystem::path target_file_path = 
          target_device_data_dir_path / relative_file_path;

      // ensure that the target file's parent dir is created if not existing:
      std::filesystem::create_directories(target_file_path.parent_path(),
                                          ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to create directory: ") + target_file_path.parent_path().string());
        return -1;
      }

      // copy the source file to the target file path:
      // MinGW g++ bug? overwrite_existing, still throws error if it exists? hence the check below.
      if(FileUtils::FileExists(target_file_path)) {
        std::filesystem::remove(target_file_path);
      }
      std::cout << "copying:" << relative_file_path << std::endl;
      FileUtils::overwriteFile(source_file_path,
                               target_file_path,
                               ec);
      if(ec) {
        // error
        compiler->ErrorMessage(std::string("failed to copy: ") + source_file_path.string());
        return -1;
      }
    }

    // Post-encryption audit: for CRR devices, the target must not contain any
    // plaintext copies of IP-sensitive files (SB_MAPS.yml, CSV/*.csv).
    // This catches classification bugs where a file ends up on both the encrypt
    // list (producing .en) and the copy list (producing a plaintext).
    {
      std::vector<std::string> residual_plaintext;
      std::error_code walk_ec;
      auto walk_it = std::filesystem::recursive_directory_iterator(
          target_device_data_dir_path,
          std::filesystem::directory_options::skip_permission_denied, walk_ec);
      if (walk_ec) {
        // Fail closed: if we cannot even start walking the target tree we
        // cannot prove the absence of plaintext, so treat it as a leak.
        compiler->ErrorMessage(
            std::string("POST-ENCRYPTION AUDIT FAILED: cannot scan target tree: ") +
            walk_ec.message());
        return -1;
      }
      for (const std::filesystem::directory_entry& dir_entry : walk_it) {
        // Reset per-iteration so a stale error from a previous entry does not
        // short-circuit (and silently skip) the rest of the tree.
        std::error_code entry_ec;
        if (!dir_entry.is_regular_file(entry_ec) || entry_ec) continue;
        const std::string fname = dir_entry.path().filename().string();
        const std::string fullpath = dir_entry.path().string();
        const bool is_sb_maps =
            std::regex_match(fname, std::regex(".*SB_MAPS\\.yml",
                                               std::regex::icase));
        const bool is_csv_in_csv_dir = std::regex_match(
            fullpath,
            std::regex(R"(.+[\/\\]CSV[\/\\].*\.csv)", std::regex::icase));
        if (is_sb_maps || is_csv_in_csv_dir) {
          residual_plaintext.push_back(fullpath);
        }
      }
      if (!residual_plaintext.empty()) {
        std::string msg =
            "\nPOST-ENCRYPTION AUDIT FAILED: residual plaintext files in target:";
        for (const auto& p : residual_plaintext) msg += "\n  " + p;
        compiler->ErrorMessage(msg);
        return -1;
      }
    }

    // surface the unclassified count alongside the result: individual warnings scroll past in
    // a long packaging run, and "0 unclassified" is the line that says the package is complete.
    if(unclassified_file_count > 0) {
      compiler->Message("\nWARNING: " + std::to_string(unclassified_file_count) +
                        " file(s) matched no encrypt/copy/exclude rule and are NOT in the device package.");
      compiler->Message("Add a rule for them in QLDeviceManager::encryptDevice() if they are device data.");
    }

    compiler->Message("\ndevice encrypted ok: " + device);
    compiler->Message("\ntarget path: " + target_device_data_dir_path.string());

    return 0;
  }


int QLDeviceManager::addDevice(std::string family, std::string foundry, std::string node, std::string devicename,
                                 std::string device_data_source, bool force) {

    // add_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> [force]
    // this will perform the steps:
    // 1. check if the 'device' already exists in the installation
    //      check if the '<INSTALLATION> / device_data / <family> / <foundry> / <node> / <devicename>' dir path
    //        already exists in installation
    //      if it already exists, we will display an error, and stop.
    //      if 'force' has been specified, we will push out a warning, but proceed further.
    // 2. call encryptDevice() to ensure the source structure is ok, and to encrypt and copy the device to the 
    //    target device data installation dir

    CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

    std::filesystem::path source_device_data_dir_path = device_data_source;

    std::string device = QLDeviceManager::getInstance()->DeviceString(family,
                                                                      foundry,
                                                                      node,
                                                                      devicename,
                                                                      "",
                                                                      "",
                                                                      "");

    // convert to canonical path, which will also check that the path exists.
    std::error_code ec;
    std::filesystem::path source_device_data_dir_path_c = 
            std::filesystem::canonical(source_device_data_dir_path, ec);
    if(ec) {
      // error
      compiler->ErrorMessage("QLDeviceManager::addDevice() failed!\n");
      compiler->ErrorMessage("Please check if the path specified exists!\n");
      compiler->ErrorMessage("path: " + source_device_data_dir_path.string());
      return -1;
    }

    // debug prints
    // std::cout << std::endl;
    // std::cout << "family: " << family << std::endl;
    // std::cout << "foundry: " << foundry << std::endl;
    // std::cout << "node: " << node << std::endl;
    // std::cout << "devicename: " << devicename << std::endl;
    // std::cout << "source_device_data_dir_path: " << source_device_data_dir_path_c << std::endl;
    // std::cout << "force: " << std::string(force?"true":"false") << std::endl;
    // std::cout << std::endl;

    // [1] check if installation already has the device added and inform the user accordingly.
    //     (device data dir for this device already exists)
    std::filesystem::path target_device_data_dir_path = 
        std::filesystem::path(deviceDataRootDirPath() /
                              family /
                              foundry /
                              node /
                              devicename);

    if (std::filesystem::exists(target_device_data_dir_path, ec)) {
      if(force) {
        compiler->Message("\nWARNING: The device you are trying to add already exists in the installation.");
        compiler->Message("device:      " + device);
        compiler->Message("target path: " + target_device_data_dir_path.string());
        compiler->Message("'force' has been specified, this will overwrite the target device dir with new files.");
        compiler->Message("\n");
      }
      else {
        compiler->Message("\n");
        compiler->ErrorMessage("The device you are trying to add already exists in the installation.");
        compiler->Message("device:      " + device);
        compiler->Message("target path: " + target_device_data_dir_path.string());
        compiler->Message("Please specify 'force' to overwrite the target device dir with new files.");
        compiler->Message("Please enter command in the format:\n"
                          "    add_device <family> <foundry> <node> <devicename> <source_device_data_dir_path> [force]");
        compiler->Message("\n");
        return -1;
      }
    }
    else {
        //Message("\nNew Device files will be added to the installation.");
        //Message("device:      " + device);
        //Message("target path: " + target_device_data_dir_path.string());
        //Message("\n");
    }

    // the device may already exist in a LOWER precedence root (e.g. one registered by
    // install_device). adding it here is allowed - the installation is authoritative - but
    // the user needs to know the other copy is about to become invisible, otherwise a stale
    // externally installed device silently stops being the one the flow uses.
    for (const QLDeviceType& existing_device: device_list) {

      if(existing_device.family == family &&
         existing_device.foundry == foundry &&
         existing_device.node == node &&
         existing_device.devicename == devicename &&
         !existing_device.device_root_path.empty() &&
         existing_device.device_root_path != deviceDataRootDirPath()) {

        compiler->Message("\nNOTE: this device also exists in another device data root, which will now be shadowed.");
        compiler->Message("device:        " + device);
        compiler->Message("shadowed root: " + existing_device.device_root_path.string());
        compiler->Message("\n");
        break;
      }
    }

    int status = encryptDevice(family, foundry, node, devicename,
                               device_data_source, target_device_data_dir_path.string());

    if(status == 0) {
      compiler->Message("\ndevice added ok: " + device);
      return 0;
    }

    compiler->ErrorMessage("\nadd device failed: " + device);
    return status;

  }


// ---------------------------------------------------------------------------------------
// device kit installation
// ---------------------------------------------------------------------------------------

namespace {

// SHA-256, implemented here rather than pulled in as a dependency: qlcrypt uses it internally
// but does not expose a hashing API, and shelling out to sha256sum is not portable to the
// Windows/MSYS2 builds. this verifies an integrity checksum, not a secret.
struct Sha256 {

  uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                       0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  uint64_t bit_count = 0;
  uint8_t  buffer[64] = {0};
  size_t   buffer_used = 0;

  static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

  void transform(const uint8_t* block) {

    static const uint32_t k[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
             (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + S1 + ch + k[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;
      h = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
  }

  void update(const uint8_t* data, size_t length) {
    bit_count += uint64_t(length) * 8;
    while(length > 0) {
      size_t take = std::min(size_t(64) - buffer_used, length);
      std::memcpy(buffer + buffer_used, data, take);
      buffer_used += take;
      data += take;
      length -= take;
      if(buffer_used == 64) {
        transform(buffer);
        buffer_used = 0;
      }
    }
  }

  std::string final() {
    uint64_t total_bits = bit_count;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0x00;
    while(buffer_used != 56) {
      update(&zero, 1);
    }
    uint8_t length_bytes[8];
    for (int i = 0; i < 8; ++i) {
      length_bytes[7 - i] = uint8_t((total_bits >> (i * 8)) & 0xff);
    }
    std::memcpy(buffer + buffer_used, length_bytes, 8);
    transform(buffer);

    std::ostringstream digest;
    digest << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
      digest << std::setw(8) << state[i];
    }
    return digest.str();
  }
};

// hex sha-256 of a file, or an empty string when it cannot be read.
std::string sha256OfFile(const std::filesystem::path& file_path) {

  std::ifstream file_ifstream(file_path.string(), std::ios::binary);
  if(!file_ifstream.is_open()) {
    return std::string();
  }

  Sha256 sha256;
  std::vector<char> chunk(64 * 1024);
  while(file_ifstream.good()) {
    file_ifstream.read(chunk.data(), std::streamsize(chunk.size()));
    std::streamsize got = file_ifstream.gcount();
    if(got > 0) {
      sha256.update(reinterpret_cast<const uint8_t*>(chunk.data()), size_t(got));
    }
  }
  return sha256.final();
}

// split an Aurora version like "2026.3-alpha1" into numeric components and a pre-release tag.
void parseAuroraVersion(const std::string& version_string,
                        std::vector<long>& out_numbers,
                        std::string& out_prerelease) {

  out_numbers.clear();
  out_prerelease.clear();

  std::string numeric_part = version_string;
  std::string::size_type dash = version_string.find('-');
  if(dash != std::string::npos) {
    numeric_part = version_string.substr(0, dash);
    out_prerelease = version_string.substr(dash + 1);
  }

  std::string::size_type begin = 0;
  while(begin <= numeric_part.size()) {
    std::string::size_type end = numeric_part.find('.', begin);
    if(end == std::string::npos) {
      end = numeric_part.size();
    }
    std::string component = numeric_part.substr(begin, end - begin);
    if(!component.empty()) {
      // take the first run of digits in the component. Aurora patch releases are tagged
      // "2026.2.p1", so a component can carry a non-digit prefix; parsing it as 0 would make
      // 2026.2.p1 compare EQUAL to 2026.2 and equal to every other patch of it.
      std::string::size_type digits_begin = component.find_first_of("0123456789");
      if(digits_begin == std::string::npos) {
        out_numbers.push_back(0);
      }
      else {
        std::string::size_type digits_end = component.find_first_not_of("0123456789", digits_begin);
        std::string digits = component.substr(digits_begin,
            (digits_end == std::string::npos) ? std::string::npos : digits_end - digits_begin);
        try {
          out_numbers.push_back(std::stol(digits));
        }
        catch (const std::exception&) {
          out_numbers.push_back(0);
        }
      }
    }
    begin = end + 1;
  }
}

// <0 / 0 / >0, like strcmp.
//
// NOTE the pre-release rule: "2026.2-alpha" sorts BEFORE "2026.2". That is not cosmetic - the
// 2026.2-alpha release still carried the OLD crypto stack, so it cannot read a kit produced
// today and MUST fail the minimum-version check. Stripping the suffix would wrongly accept it.
int compareAuroraVersions(const std::string& lhs, const std::string& rhs) {

  std::vector<long> lhs_numbers, rhs_numbers;
  std::string lhs_prerelease, rhs_prerelease;
  parseAuroraVersion(lhs, lhs_numbers, lhs_prerelease);
  parseAuroraVersion(rhs, rhs_numbers, rhs_prerelease);

  for (size_t i = 0; i < std::max(lhs_numbers.size(), rhs_numbers.size()); ++i) {
    long lhs_number = (i < lhs_numbers.size()) ? lhs_numbers[i] : 0;
    long rhs_number = (i < rhs_numbers.size()) ? rhs_numbers[i] : 0;
    if(lhs_number != rhs_number) {
      return (lhs_number < rhs_number) ? -1 : 1;
    }
  }

  if(lhs_prerelease.empty() != rhs_prerelease.empty()) {
    // the one WITH a pre-release tag is older
    return lhs_prerelease.empty() ? 1 : -1;
  }
  if(lhs_prerelease != rhs_prerelease) {
    return (lhs_prerelease < rhs_prerelease) ? -1 : 1;
  }
  return 0;
}

// Expand a leading '~' or '~/' to the user's home directory.
//
// A Tcl command receives its arguments already word-split, so the shell never expands a tilde
// inside `aurora --cmd "install_device kit.tar.gz -target ~/devices"` -- bash does not expand
// '~' inside double quotes unless it starts the word. Without this, that documented form would
// silently create a directory literally named '~' under the current directory. Only a LEADING
// tilde is expanded; '~user' is not supported and is left alone.
std::filesystem::path expandLeadingTilde(const std::filesystem::path& input_path) {

  const std::string input_string = input_path.string();
  if(input_string.empty() || input_string[0] != '~') {
    return input_path;
  }
  // '~user/...' -- not ours to resolve; leave it untouched rather than guess.
  if(input_string.size() > 1 && input_string[1] != '/' && input_string[1] != '\\') {
    return input_path;
  }

  const char* home_dir = std::getenv("HOME");
#ifdef _WIN32
  if(home_dir == nullptr) { home_dir = std::getenv("USERPROFILE"); }
#endif
  if(home_dir == nullptr || *home_dir == '\0') {
    return input_path;                       // no home to expand to: leave it as the user typed it
  }

  return std::filesystem::path(home_dir) / input_string.substr(input_string.size() > 1 ? 2 : 1);
}

// true when 'candidate' is inside 'ancestor' (or is it), after resolving symlinks and '..'.
bool pathIsInside(const std::filesystem::path& candidate, const std::filesystem::path& ancestor) {

  std::error_code ec;

  // resolve as much of the candidate as exists: the target dir may not have been created yet.
  std::filesystem::path candidate_existing = candidate;
  while(!candidate_existing.empty() && !std::filesystem::exists(candidate_existing, ec)) {
    std::filesystem::path parent = candidate_existing.parent_path();
    if(parent == candidate_existing) {
      break;
    }
    candidate_existing = parent;
  }

  std::filesystem::path candidate_c = std::filesystem::weakly_canonical(candidate_existing, ec);
  if(ec) { candidate_c = candidate_existing; ec.clear(); }
  std::filesystem::path ancestor_c = std::filesystem::weakly_canonical(ancestor, ec);
  if(ec) { ancestor_c = ancestor; ec.clear(); }

  auto candidate_it = candidate_c.begin();
  auto ancestor_it = ancestor_c.begin();
  for(; ancestor_it != ancestor_c.end(); ++ancestor_it, ++candidate_it) {
    if(candidate_it == candidate_c.end() || *candidate_it != *ancestor_it) {
      return false;
    }
  }
  return true;
}

// run a command and return its stdout.
//
// FileUtils::ExecuteSystemCommand writes a "Command: ..." preamble line into the output
// stream before the process output (FileUtils.cpp:418), which corrupts anything parsed from
// it. strip that here, in one place, rather than at each call site.
bool runCommandCaptureOutput(const std::string& command,
                             const std::vector<std::string>& args,
                             std::string& out_text) {

  // capture stderr SEPARATELY. with a null error stream the child's stderr is folded into
  // stdout, and tar routinely writes warnings there ("Removing leading '../' from member
  // names") - which would then be parsed as archive content and, in the traversal check,
  // rejected as an unsafe entry even for a benign kit.
  std::ostringstream command_output;
  std::ostringstream command_error;
  int status = FileUtils::ExecuteSystemCommand(command, args, &command_output, -1,
                                               /*workingDir*/ std::string(), &command_error).code;

  out_text = command_output.str();

  const std::string preamble = "Command: ";
  if(out_text.compare(0, preamble.size(), preamble) == 0) {
    std::string::size_type first_newline = out_text.find('\n');
    out_text = (first_newline == std::string::npos) ? std::string()
                                                    : out_text.substr(first_newline + 1);
  }

  return (status == 0);
}

} // anonymous namespace


int QLDeviceManager::installDevice(std::string kit_archive_path,
                                   std::string target_dir_path,
                                   bool force) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::error_code ec;

  // ORDER MATTERS THROUGHOUT: every check below runs BEFORE anything is written to the
  // target, so a bad kit never leaves a half-installed device behind.

  // ---- [1] the kit itself -------------------------------------------------------------
  std::filesystem::path kit_path(kit_archive_path);
  if(!std::filesystem::is_regular_file(kit_path, ec)) {
    compiler->ErrorMessage("device kit not found: " + kit_path.string());
    return -1;
  }
  std::filesystem::path kit_path_c = std::filesystem::canonical(kit_path, ec);
  if(ec) { kit_path_c = kit_path; ec.clear(); }

  // ---- [2] the target must be OUTSIDE the Aurora installation ---------------------------
  // enforced on the input rather than trusting the user to pick well: the whole point of the
  // feature is that installing a device never modifies the Aurora installation.
  std::filesystem::path target_path = expandLeadingTilde(std::filesystem::path(target_dir_path));
  if(target_path.empty()) {
    compiler->ErrorMessage("please specify a target directory to install the device into.");
    return -1;
  }

  std::filesystem::path installation_root_dir_path = GlobalSession->Context()->DataPath();
  std::filesystem::path installation_dir_path = installation_root_dir_path.parent_path();

  if(pathIsInside(target_path, installation_dir_path)) {
    compiler->ErrorMessage("refusing to install into the Aurora installation.");
    compiler->Message("target:       " + target_path.string());
    compiler->Message("installation: " + installation_dir_path.string());
    compiler->Message("Please choose a directory outside the Aurora installation - installing a");
    compiler->Message("device must never modify the installation itself.");
    return -1;
  }

  // ---- [3] integrity: verify the .sha256 sidecar ----------------------------------------
  std::filesystem::path kit_checksum_path = kit_path_c.string() + ".sha256";
  if(!std::filesystem::is_regular_file(kit_checksum_path, ec)) {
    compiler->ErrorMessage("checksum file not found: " + kit_checksum_path.string());
    compiler->Message("A device kit must be accompanied by its .sha256 file.");
    return -1;
  }

  std::string expected_sha256;
  {
    std::ifstream checksum_ifstream(kit_checksum_path.string());
    checksum_ifstream >> expected_sha256;
  }
  if(expected_sha256.empty()) {
    compiler->ErrorMessage("could not read checksum from: " + kit_checksum_path.string());
    return -1;
  }

  compiler->Message("verifying kit checksum...");
  std::string actual_sha256 = sha256OfFile(kit_path_c);
  if(actual_sha256.empty()) {
    compiler->ErrorMessage("could not read the device kit: " + kit_path_c.string());
    return -1;
  }
  if(actual_sha256 != expected_sha256) {
    compiler->ErrorMessage("device kit checksum MISMATCH - the kit is corrupt or was modified.");
    compiler->Message("expected: " + expected_sha256);
    compiler->Message("actual:   " + actual_sha256);
    return -1;
  }

  // ---- [4] read the manifest WITHOUT extracting -----------------------------------------
  std::string manifest_text;
  std::vector<std::string> manifest_args = {"-xzOf", kit_path_c.string(), "manifest.json"};
  bool manifest_ok = runCommandCaptureOutput("tar", manifest_args, manifest_text);
  if(!manifest_ok || manifest_text.empty()) {
    compiler->ErrorMessage("device kit has no readable manifest.json: " + kit_path_c.string());
    return -1;
  }

  json manifest_json;
  try {
    manifest_json = json::parse(manifest_text);
  }
  catch (const json::exception& e) {
    compiler->ErrorMessage(std::string("device kit manifest.json is malformed: ") + e.what());
    return -1;
  }

  if(!manifest_json.contains("device") ||
     !manifest_json["device"].contains("family") ||
     !manifest_json["device"].contains("foundry") ||
     !manifest_json["device"].contains("node") ||
     !manifest_json["device"].contains("devicename")) {
    compiler->ErrorMessage("device kit manifest.json does not describe a device.");
    return -1;
  }

  std::string family = manifest_json["device"]["family"].get<std::string>();
  std::string foundry = manifest_json["device"]["foundry"].get<std::string>();
  std::string node = manifest_json["device"]["node"].get<std::string>();
  std::string devicename = manifest_json["device"]["devicename"].get<std::string>();
  std::string device_type_string = DeviceTypeString(family, foundry, node, devicename);

  std::string customer_id =
      manifest_json.contains("customer_id") ? manifest_json["customer_id"].get<std::string>() : "";

  compiler->Message("device:      " + device_type_string);
  if(!customer_id.empty()) {
    compiler->Message("customer:    " + customer_id);
  }

  // ---- [5] version gate -----------------------------------------------------------------
  // refuse up front rather than letting the flow fail cryptically later. a kit built today
  // cannot be read by an Aurora older than the crypto stack it was produced with.
  std::string min_aurora_version =
      manifest_json.contains("min_aurora_version")
          ? manifest_json["min_aurora_version"].get<std::string>()
          : std::string();

  if(!min_aurora_version.empty()) {

    std::string running_aurora_version(foedag_version_number);

    if(compareAuroraVersions(running_aurora_version, min_aurora_version) < 0) {

      // A pre-release OF the required version (2026.2-rc1 vs a 2026.2 minimum) sorts older by
      // semver and is refused here, which is deliberate: 2026.2-alpha shipped the LEGACY crypto
      // stack and genuinely cannot read this kit. But the two cases are indistinguishable from
      // the version alone, so say which one this is instead of a flat "too old" - an internal
      // user on an rc build needs to know 'force' is the right answer, not an upgrade.
      std::vector<long> running_numbers, minimum_numbers;
      std::string running_prerelease, minimum_prerelease;
      parseAuroraVersion(running_aurora_version, running_numbers, running_prerelease);
      parseAuroraVersion(min_aurora_version, minimum_numbers, minimum_prerelease);
      const bool running_is_prerelease_of_minimum =
          !running_prerelease.empty() && minimum_prerelease.empty() &&
          running_numbers == minimum_numbers;

      if(!force) {
        if(running_is_prerelease_of_minimum) {
          compiler->ErrorMessage("this Aurora is a pre-release of the version this kit needs.");
          compiler->Message("this Aurora:        " + running_aurora_version);
          compiler->Message("kit needs at least: " + min_aurora_version);
          compiler->Message("Pre-releases are treated as older than the release they precede,");
          compiler->Message("because early ones (2026.2-alpha) carry the legacy crypto stack.");
          compiler->Message("If this build already has the current stack, re-run with 'force'.");
        }
        else {
          compiler->ErrorMessage("this Aurora is too old to use this device kit.");
          compiler->Message("this Aurora:        " + running_aurora_version);
          compiler->Message("kit needs at least: " + min_aurora_version);
          compiler->Message("Please upgrade Aurora, or re-run with 'force' to install anyway");
          compiler->Message("(the device is unlikely to work).");
        }
        return -1;
      }

      compiler->Message("\nWARNING: this Aurora (" + running_aurora_version + ") is older than the kit's");
      compiler->Message("minimum (" + min_aurora_version + "). Installing anyway because 'force' was given;");
      compiler->Message("the device is unlikely to work.");
    }
  }

  // ---- [6] reject a kit that would write outside the target ------------------------------
  // list the archive first: an absolute path or a '..' component would let a crafted kit
  // escape the target directory. the checksum only proves the kit is intact, not benign.
  std::string listing_text;
  std::vector<std::string> listing_args = {"-tzf", kit_path_c.string()};
  if(!runCommandCaptureOutput("tar", listing_args, listing_text)) {
    compiler->ErrorMessage("could not list the device kit contents: " + kit_path_c.string());
    return -1;
  }

  // the kit must actually CONTAIN the device its manifest describes. checking the listing
  // here, rather than the extracted tree afterwards, is what makes the refusal cost nothing:
  // a kit whose manifest disagrees with its payload used to be rejected only after ~68MB had
  // already been written into the user's directory, contrary to "verify before extract".
  const std::string required_prefix =
      family + "/" + foundry + "/" + node + "/" + devicename + "/";
  bool listing_holds_the_device = false;

  {
    std::istringstream listing(listing_text);
    std::string entry;
    while(std::getline(listing, entry)) {
      if(entry.empty()) {
        continue;
      }
      if(entry.front() == '/' || entry.find("..") != std::string::npos ||
         (entry.size() > 1 && entry[1] == ':')) {
        compiler->ErrorMessage("device kit contains an unsafe path, refusing to extract:");
        compiler->Message("entry: " + entry);
        return -1;
      }
      // tar may or may not prefix entries with './'
      std::string normalised_entry = entry;
      if(normalised_entry.rfind("./", 0) == 0) {
        normalised_entry = normalised_entry.substr(2);
      }
      if(normalised_entry.rfind(required_prefix, 0) == 0) {
        listing_holds_the_device = true;
      }
    }
  }

  if(!listing_holds_the_device) {
    compiler->ErrorMessage("the device kit did not contain the device it describes: " +
                           device_type_string);
    compiler->Message("expected to find: " + required_prefix);
    compiler->Message("Nothing was written to the target.");
    return -1;
  }

  // ---- [7] already installed? -----------------------------------------------------------
  std::filesystem::path installed_device_dir_path =
      target_path / family / foundry / node / devicename;

  // NOTE this runs AFTER the archive has been validated: with 'force' it deletes the
  // device that is already installed, and deleting it before knowing the replacement is
  // usable would leave the customer with nothing.
  if(std::filesystem::exists(installed_device_dir_path, ec)) {
    if(!force) {
      // idempotent: installing an already-installed device is a NO-OP that SUCCEEDS, so
      // re-running an install script does not fail (REQ-030). use 'force' to replace it.
      compiler->Message("device is already installed, nothing to do: " + device_type_string);
      compiler->Message("path:   " + installed_device_dir_path.string());
      compiler->Message("Specify 'force' to replace it with this kit.");
      return 0;
    }
    compiler->Message("WARNING: replacing the device already installed at " +
                      installed_device_dir_path.string());
    std::filesystem::remove_all(installed_device_dir_path, ec);
    if(ec) {
      compiler->ErrorMessage("could not replace the installed device: " + ec.message());
      return -1;
    }
    ec.clear();
  }

  // ---- [8] extract. NO re-encryption: the kit is already encrypted ----------------------
  std::filesystem::create_directories(target_path, ec);
  if(ec) {
    compiler->ErrorMessage("could not create the target directory: " + target_path.string() +
                           " (" + ec.message() + ")");
    return -1;
  }

  compiler->Message("installing...");
  std::string extract_text;
  std::vector<std::string> extract_args = {"-xzf", kit_path_c.string(), "-C", target_path.string()};
  if(!runCommandCaptureOutput("tar", extract_args, extract_text)) {
    compiler->ErrorMessage("could not extract the device kit into: " + target_path.string());
    compiler->Message(extract_text);
    return -1;
  }

  if(!std::filesystem::exists(installed_device_dir_path, ec)) {
    compiler->ErrorMessage("the device kit did not contain the device it describes: " +
                           device_type_string);
    return -1;
  }

  // the manifest and the customer README are metadata about the delivery, not device data -
  // keep them beside the device tree rather than leaving them at the root of the user's chosen
  // directory. The target is a directory they picked and may keep their own files in; dropping
  // generic names like README.txt there litters it, and a second kit would overwrite the first
  // one's copy.
  const std::pair<const char*, const char*> delivery_files[] = {
      {"manifest.json", "kit_manifest.json"},
      {"README.txt", "kit_README.txt"},
  };
  for (const auto& [extracted_name, installed_name] : delivery_files) {
    std::filesystem::path extracted_path = target_path / extracted_name;
    if(std::filesystem::exists(extracted_path, ec)) {
      std::filesystem::rename(extracted_path, installed_device_dir_path / installed_name, ec);
      ec.clear();
    }
    ec.clear();
  }

  // ---- [9] remember the root so the device is visible in later sessions ------------------
  if(!registerDeviceRoot(target_path)) {
    compiler->Message("\nWARNING: the device was installed but could not be remembered.");
    compiler->Message("Set AURORA2_DEVICE_DATA_PATH=" + target_path.string() + " to use it.");
  }

  // make the newly installed device visible to this session too.
  parseDeviceData();

  compiler->Message("\ndevice installed ok: " + device_type_string);
  compiler->Message("path: " + installed_device_dir_path.string());

  return 0;
}


int QLDeviceManager::uninstallDevice(std::string family, std::string foundry, std::string node,
                                     std::string devicename, bool dry_run, bool force) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::error_code ec;
  std::string device_type_string = DeviceTypeString(family, foundry, node, devicename);

  // make sure devices have been discovered. this can be called on a fresh interpreter where
  // nothing has populated device_list yet, and an empty list would look exactly like
  // "the device is not installed".
  if(device_list.empty()) {
    parseDeviceData();
  }

  // resolve the copy to remove against the device-data roots DIRECTLY, not through device_list.
  //
  // device_list carries only the winning copy of a shadowed device. If the customer installs a
  // kit for a device that also ships built-in, the installation wins, the installed copy is
  // absent from device_list, and resolving through it would land on the installation and refuse
  // - leaving the user's own copy both invisible to list_devices and impossible to remove.
  // Walking the roots finds the copy that is actually theirs.
  std::filesystem::path installation_root_dir_path = GlobalSession->Context()->DataPath();
  std::filesystem::path installation_root_dir_path_c =
      std::filesystem::canonical(installation_root_dir_path, ec);
  if(ec) { installation_root_dir_path_c = installation_root_dir_path; ec.clear(); }

  auto root_is_the_installation = [&](const std::filesystem::path& root_dir_path) {
    return root_dir_path == installation_root_dir_path_c ||
           pathIsInside(root_dir_path, installation_root_dir_path_c);
  };

  std::vector<std::filesystem::path> external_root_dir_path_list;
  bool installation_holds_device = false;

  for (const std::filesystem::path& root_dir_path : deviceDataRootDirPathList()) {
    const std::filesystem::path candidate_dir_path =
        root_dir_path / family / foundry / node / devicename;
    if(!std::filesystem::exists(candidate_dir_path / "config.json", ec)) { ec.clear(); continue; }
    ec.clear();
    if(root_is_the_installation(root_dir_path)) {
      installation_holds_device = true;
    }
    else {
      external_root_dir_path_list.push_back(root_dir_path);
    }
  }

  if(external_root_dir_path_list.empty()) {
    // never delete from the Aurora installation: built-in devices are part of the install and
    // are not ours to remove.
    if(installation_holds_device) {
      compiler->ErrorMessage("refusing to uninstall a device that is part of the Aurora installation.");
      compiler->Message("device: " + device_type_string);
      compiler->Message("root:   " + installation_root_dir_path_c.string());
      compiler->Message("Only devices installed with 'install_device' can be uninstalled.");
    }
    else {
      compiler->ErrorMessage("device is not installed: " + device_type_string);
      compiler->Message("Use 'list_devices' to see what is installed.");
    }
    return -1;
  }

  // highest-precedence installed copy first. any further copies are reported after removal so
  // the user knows another remains rather than believing the device is gone.
  std::filesystem::path device_root_dir_path = external_root_dir_path_list.front();

  std::filesystem::path device_dir_path =
      device_root_dir_path / family / foundry / node / devicename;

  if(!std::filesystem::exists(device_dir_path, ec)) {
    compiler->ErrorMessage("device directory not found: " + device_dir_path.string());
    return -1;
  }
  ec.clear();

  // say exactly what will be removed, always - this deletes from the user's own directory.
  compiler->Message("device:       " + device_type_string);
  compiler->Message("will remove:  " + device_dir_path.string());

  if(external_root_dir_path_list.size() > 1 || installation_holds_device) {
    // the device will still resolve afterwards, from another copy. say so plainly rather than
    // letting "removed" imply the device is gone.
    compiler->Message("note: another copy of this device remains and will still be used:");
    for(std::size_t i = 1; i < external_root_dir_path_list.size(); ++i) {
      compiler->Message("      installed: " + external_root_dir_path_list[i].string());
    }
    if(installation_holds_device) {
      compiler->Message("      built-in:  " + installation_root_dir_path_c.string());
    }
  }

  if(dry_run) {
    compiler->Message("\n(dry run - nothing was removed)");
    return 0;
  }

  if(!force) {
    // there is no interactive prompt in batch mode, so require the caller to be explicit
    // rather than deleting on the strength of the command alone.
    compiler->ErrorMessage("refusing to delete without confirmation.");
    compiler->Message("Re-run with 'force' to remove it, or 'dry-run' to see what would go.");
    return -1;
  }

  std::uintmax_t removed_count = std::filesystem::remove_all(device_dir_path, ec);
  if(ec) {
    compiler->ErrorMessage("could not remove the device: " + ec.message());
    return -1;
  }
  compiler->Message("removed " + std::to_string(removed_count) + " file(s)/dir(s).");

  // prune now-empty parents (node, foundry, family) but ONLY within this root, and only while
  // they are empty: the user's directory may hold their own files and other devices.
  std::filesystem::path parent_dir_path = device_dir_path.parent_path();
  while(parent_dir_path != device_root_dir_path &&
        pathIsInside(parent_dir_path, device_root_dir_path)) {

    if(!std::filesystem::is_empty(parent_dir_path, ec) || ec) {
      ec.clear();
      break;
    }
    std::filesystem::remove(parent_dir_path, ec);
    if(ec) { ec.clear(); break; }
    parent_dir_path = parent_dir_path.parent_path();
  }

  // if this root holds no further devices, stop searching it.
  //
  // ask the FILESYSTEM, not device_list: a device in this root that is shadowed by one in a
  // higher precedence root is deliberately absent from device_list, so trusting the list here
  // would deregister a root that still holds devices - and they would become invisible if the
  // shadowing device were ever removed.
  bool root_has_devices = false;
  for (const std::filesystem::directory_entry& remaining_entry :
           std::filesystem::recursive_directory_iterator(device_root_dir_path,
               std::filesystem::directory_options::skip_permission_denied, ec)) {
    if(remaining_entry.is_regular_file(ec) &&
       remaining_entry.path().filename() == "config.json") {
      root_has_devices = true;
      break;
    }
  }
  ec.clear();

  if(!root_has_devices) {
    if(unregisterDeviceRoot(device_root_dir_path)) {
      compiler->Message("no devices remain in " + device_root_dir_path.string() +
                        ", stopped searching it.");
    }
  }

  // refresh so the removed device disappears from this session too.
  parseDeviceData();

  compiler->Message("\ndevice uninstalled ok: " + device_type_string);
  return 0;
}


// ---------------------------------------------------------------------------------------
// external device data root registry
// ---------------------------------------------------------------------------------------

namespace {

// the registry is a small shared file that two Aurora processes can reach at the same time
// (e.g. two install_device runs). a naive read-modify-write loses entries or, worse, leaves a
// half-written file behind. so: take an advisory lock, write a temp file, rename it over.

const int DEVICE_ROOT_REGISTRY_VERSION = 1;
const int REGISTRY_LOCK_TIMEOUT_SECONDS = 5;
// a lock older than this is assumed to be from a process that died holding it.
const int REGISTRY_LOCK_STALE_SECONDS = 60;

// create <registry>.lock exclusively, retrying until the timeout. returns false when the lock
// could not be taken, so the caller reports failure rather than racing.
bool acquireRegistryLock(const std::filesystem::path& lock_file_path) {

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(REGISTRY_LOCK_TIMEOUT_SECONDS);

  while(true) {

    // O_EXCL creation is the lock: it succeeds for exactly one process.
    int lock_fd = ::open(lock_file_path.string().c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
    if(lock_fd >= 0) {
      ::close(lock_fd);
      return true;
    }

    // only EEXIST means somebody else holds the lock. anything else (a missing parent
    // directory, no permission) will never resolve by waiting, so fail immediately instead of
    // spinning until the timeout and reporting misleading "lock contention".
    if(errno != EEXIST) {
      std::cout << "ERROR: cannot create device root registry lock: "
                << lock_file_path.string() << " (" << std::strerror(errno) << ")" << std::endl;
      return false;
    }

    // someone holds it. if the lock is old enough that its owner has almost certainly died,
    // break it rather than making the user wait forever on a crashed process.
    std::error_code ec;
    auto lock_write_time = std::filesystem::last_write_time(lock_file_path, ec);
    if(!ec) {
      auto lock_age = std::filesystem::file_time_type::clock::now() - lock_write_time;
      if(lock_age > std::chrono::seconds(REGISTRY_LOCK_STALE_SECONDS)) {
        std::cout << "WARNING: breaking stale device root registry lock: "
                  << lock_file_path.string() << std::endl;
        std::filesystem::remove(lock_file_path, ec);
        continue;
      }
    }

    if(std::chrono::steady_clock::now() >= deadline) {
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void releaseRegistryLock(const std::filesystem::path& lock_file_path) {
  std::error_code ec;
  std::filesystem::remove(lock_file_path, ec);
}

} // anonymous namespace


std::filesystem::path QLDeviceManager::deviceRootRegistryFilePath() {

  // per-user, never machine-global: one user installing a device must not change what another
  // user sees.
#ifdef _WIN32
  const char* const home_dir_env_str = std::getenv("USERPROFILE");
#else
  const char* const home_dir_env_str = std::getenv("HOME");
#endif

  if(home_dir_env_str == nullptr || std::string(home_dir_env_str).empty()) {
    // no usable home: the registry is simply unavailable. callers degrade to env-var-only
    // operation rather than failing (NFR-005).
    return std::filesystem::path();
  }

  return std::filesystem::path(home_dir_env_str) / ".aurora" / "device_roots.json";
}


std::vector<std::filesystem::path> QLDeviceManager::readDeviceRootRegistry() {

  std::vector<std::filesystem::path> registered_root_dir_path_list;

  std::filesystem::path registry_file_path = deviceRootRegistryFilePath();

  if(registry_file_path.empty() || !FileUtils::FileExists(registry_file_path)) {
    // no home dir, or nothing registered yet - both are normal.
    return registered_root_dir_path_list;
  }

  try {

    std::ifstream registry_ifstream(registry_file_path.string());
    json registry_json = json::parse(registry_ifstream);

    if(registry_json.contains("roots") && registry_json["roots"].is_array()) {

      for (const auto& root_entry: registry_json["roots"]) {
        if(root_entry.is_string()) {
          std::string root_dir_path_str = root_entry.get<std::string>();
          if(!root_dir_path_str.empty()) {
            registered_root_dir_path_list.push_back(std::filesystem::path(root_dir_path_str));
          }
        }
      }
    }
  }
  catch (const json::exception& e) {

    // a corrupt registry must not make Aurora unusable - report it and carry on with the
    // built-in devices.
    std::cout << "WARNING: could not read device root registry, ignoring it: "
              << registry_file_path.string() << " (" << e.what() << ")" << std::endl;
    registered_root_dir_path_list.clear();
  }

  return registered_root_dir_path_list;
}


// write the registry atomically: temp file -> rename. the rename is what guarantees a reader
// never sees a partially written registry.
static bool writeDeviceRootRegistryFile(const std::filesystem::path& registry_file_path,
                                        const std::vector<std::filesystem::path>& root_dir_path_list) {

  std::error_code ec;
  std::filesystem::create_directories(registry_file_path.parent_path(), ec);
  if(ec) {
    std::cout << "ERROR: could not create device root registry directory: "
              << registry_file_path.parent_path().string() << " (" << ec.message() << ")" << std::endl;
    return false;
  }

  json registry_json;
  registry_json["version"] = DEVICE_ROOT_REGISTRY_VERSION;
  registry_json["roots"] = json::array();
  for (const std::filesystem::path& root_dir_path: root_dir_path_list) {
    registry_json["roots"].push_back(root_dir_path.string());
  }

  std::filesystem::path registry_temp_file_path =
      registry_file_path.string() + ".tmp." + std::to_string(
#ifdef _WIN32
          ::_getpid()
#else
          ::getpid()
#endif
      );

  {
    std::ofstream registry_ofstream(registry_temp_file_path.string(), std::ios::trunc);
    if(!registry_ofstream.is_open()) {
      std::cout << "ERROR: could not write device root registry: "
                << registry_temp_file_path.string() << std::endl;
      return false;
    }
    registry_ofstream << registry_json.dump(2) << std::endl;
    registry_ofstream.flush();
    if(!registry_ofstream.good()) {
      std::cout << "ERROR: failed writing device root registry: "
                << registry_temp_file_path.string() << std::endl;
      registry_ofstream.close();
      std::filesystem::remove(registry_temp_file_path, ec);
      return false;
    }
  }

  // replace the live registry in one step.
  std::filesystem::rename(registry_temp_file_path, registry_file_path, ec);
  if(ec) {
    std::cout << "ERROR: could not replace device root registry: "
              << registry_file_path.string() << " (" << ec.message() << ")" << std::endl;
    std::filesystem::remove(registry_temp_file_path, ec);
    return false;
  }

  return true;
}


bool QLDeviceManager::registerDeviceRoot(const std::filesystem::path& root_dir_path) {

  std::filesystem::path registry_file_path = deviceRootRegistryFilePath();
  if(registry_file_path.empty()) {
    std::cout << "WARNING: no usable home directory, cannot remember this device data root."
              << std::endl;
    std::cout << "         set AURORA2_DEVICE_DATA_PATH=" << root_dir_path.string()
              << " to use it." << std::endl;
    return false;
  }

  // the lock lives beside the registry, so its directory must exist before we can take it.
  // on a machine that has never installed a device there is no ~/.aurora yet.
  std::error_code registry_dir_ec;
  std::filesystem::create_directories(registry_file_path.parent_path(), registry_dir_ec);
  if(registry_dir_ec) {
    std::cout << "WARNING: could not create " << registry_file_path.parent_path().string()
              << " (" << registry_dir_ec.message() << ")" << std::endl;
    return false;
  }

  std::filesystem::path registry_lock_file_path = registry_file_path.string() + ".lock";
  if(!acquireRegistryLock(registry_lock_file_path)) {
    std::cout << "ERROR: could not take the device root registry lock: "
              << registry_lock_file_path.string() << std::endl;
    return false;
  }

  // canonicalize so that the same directory reached by different paths registers once.
  std::error_code ec;
  std::filesystem::path root_dir_path_c = std::filesystem::canonical(root_dir_path, ec);
  if(ec) {
    root_dir_path_c = root_dir_path;
  }

  std::vector<std::filesystem::path> registered_root_dir_path_list = readDeviceRootRegistry();

  for (const std::filesystem::path& registered_root_dir_path: registered_root_dir_path_list) {
    if(registered_root_dir_path == root_dir_path_c) {
      // already registered: nothing to do, and this is not an error (install is idempotent).
      releaseRegistryLock(registry_lock_file_path);
      return true;
    }
  }

  registered_root_dir_path_list.push_back(root_dir_path_c);

  bool status = writeDeviceRootRegistryFile(registry_file_path, registered_root_dir_path_list);

  releaseRegistryLock(registry_lock_file_path);
  return status;
}


bool QLDeviceManager::unregisterDeviceRoot(const std::filesystem::path& root_dir_path) {

  std::filesystem::path registry_file_path = deviceRootRegistryFilePath();
  if(registry_file_path.empty()) {
    return false;
  }

  std::error_code registry_dir_ec;
  std::filesystem::create_directories(registry_file_path.parent_path(), registry_dir_ec);
  if(registry_dir_ec) {
    return false;
  }

  std::filesystem::path registry_lock_file_path = registry_file_path.string() + ".lock";
  if(!acquireRegistryLock(registry_lock_file_path)) {
    std::cout << "ERROR: could not take the device root registry lock: "
              << registry_lock_file_path.string() << std::endl;
    return false;
  }

  std::error_code ec;
  std::filesystem::path root_dir_path_c = std::filesystem::canonical(root_dir_path, ec);
  if(ec) {
    root_dir_path_c = root_dir_path;
  }

  std::vector<std::filesystem::path> registered_root_dir_path_list = readDeviceRootRegistry();
  std::vector<std::filesystem::path> remaining_root_dir_path_list;

  for (const std::filesystem::path& registered_root_dir_path: registered_root_dir_path_list) {
    // compare against both the canonical and the raw path: a root that has already been
    // deleted cannot be canonicalized, and must still be removable from the registry.
    if(registered_root_dir_path != root_dir_path_c &&
       registered_root_dir_path != root_dir_path) {
      remaining_root_dir_path_list.push_back(registered_root_dir_path);
    }
  }

  bool status = writeDeviceRootRegistryFile(registry_file_path, remaining_root_dir_path_list);

  releaseRegistryLock(registry_lock_file_path);
  return status;
}


std::vector<std::filesystem::path> QLDeviceManager::deviceDataRootDirPathList() {

  std::vector<std::filesystem::path> root_device_data_dir_path_list;

  std::error_code root_ec;

  // the installation's own device_data dir - needed up front to recognise the setup.sh default.
  std::filesystem::path installation_root_dir_path = GlobalSession->Context()->DataPath();
  std::filesystem::path installation_root_dir_path_c =
      std::filesystem::canonical(installation_root_dir_path, root_ec);
  if(root_ec) {
    installation_root_dir_path_c = installation_root_dir_path;
    root_ec.clear();
  }

  // [1] $AURORA2_DEVICE_DATA_DIR is an EXCLUSIVE override - but ONLY when it points somewhere
  //     other than the installation's own device_data.
  //
  //     scripts/setup.sh sets this variable whenever the user has not, defaulting it to
  //     "${AURORA2_ROOT}/device_data/", so it is set in every normal session. Treating that
  //     default as an override would make the exclusive branch permanently active and leave
  //     multi-root discovery unreachable - registered roots and AURORA2_DEVICE_DATA_PATH would
  //     silently never be consulted.
  //
  //     when a user or CI job points it at a DIFFERENT tree (the device-data validation gate
  //     and the on-demand benchmark both do exactly this) it stays fully exclusive, replacing
  //     the device set as before.
  const char* const path_device_data_env_str = std::getenv("AURORA2_DEVICE_DATA_DIR");  // this is from setup.sh

  if (path_device_data_env_str != nullptr && std::string(path_device_data_env_str).size() > 0) {

    std::filesystem::path env_root_device_data_dir_path = std::string(path_device_data_env_str);

    if(FileUtils::FileExists(env_root_device_data_dir_path)) {

      // canonicalize before comparing: setup.sh supplies a trailing slash, and the path may
      // also differ by symlinks or '..' while naming the same directory.
      std::filesystem::path env_root_device_data_dir_path_c =
          std::filesystem::canonical(env_root_device_data_dir_path, root_ec);
      if(root_ec) {
        env_root_device_data_dir_path_c = env_root_device_data_dir_path;
        root_ec.clear();
      }

      if(env_root_device_data_dir_path_c != installation_root_dir_path_c) {

        // a genuine override: return immediately, ignoring every other source of roots.
        root_device_data_dir_path_list.push_back(env_root_device_data_dir_path_c);
        return root_device_data_dir_path_list;
      }

      // else: this is just the setup.sh default naming the installation. fall through and
      // build the additive list, with the installation first as usual.
    }
    else {

      // an env var pointing at a path that does not exist used to be silently ignored.
      // say so instead - a mistyped AURORA2_DEVICE_DATA_DIR otherwise looks like
      // "my devices vanished" with no explanation.
      std::cout << "WARNING: AURORA2_DEVICE_DATA_DIR does not exist, ignoring it: "
                << env_root_device_data_dir_path.string() << std::endl;
    }
  }

  // collected roots are canonicalized and de-duplicated: the same directory reached twice
  // (e.g. the installation also listed in AURORA2_DEVICE_DATA_PATH) would otherwise be walked
  // twice and make every one of its devices look "shadowed".
  std::set<std::string> seen_root_dir_path_set;

  auto append_root_dir_path = [&](const std::filesystem::path& root_dir_path,
                                  const std::string& source_description,
                                  bool warn_when_missing) {

    if(root_dir_path.empty()) {
      return;
    }

    if(!FileUtils::FileExists(root_dir_path)) {
      if(warn_when_missing) {
        std::cout << "WARNING: skipping device data root that no longer exists ("
                  << source_description << "): " << root_dir_path.string() << std::endl;
      }
      return;
    }

    std::error_code ec;
    std::filesystem::path root_dir_path_c = std::filesystem::canonical(root_dir_path, ec);
    if(ec) {
      root_dir_path_c = root_dir_path;
    }

    if(seen_root_dir_path_set.insert(root_dir_path_c.string()).second) {
      root_device_data_dir_path_list.push_back(root_dir_path_c);
    }
  };

  // [2] the installation's device_data dir always comes first among the additive roots:
  //     built-in devices are authoritative and can never be shadowed by an external root.
  append_root_dir_path(installation_root_dir_path, "installation", false);

  // [3] roots registered by install_device. a registered root that has been deleted or moved
  //     warns and is skipped - a stale entry must never break startup (REQ-007).
  for (const std::filesystem::path& registered_root_dir_path: readDeviceRootRegistry()) {
    append_root_dir_path(registered_root_dir_path, "registered by install_device", true);
  }

  // [4] $AURORA2_DEVICE_DATA_PATH - additive, ':'-separated on every platform (MSYS2 builds
  //     use POSIX-style paths, consistent with the rest of the toolchain).
  const char* const path_device_data_search_env_str = std::getenv("AURORA2_DEVICE_DATA_PATH");

  if(path_device_data_search_env_str != nullptr) {

    std::string path_device_data_search_str(path_device_data_search_env_str);
    std::string::size_type entry_begin = 0;

    while(entry_begin <= path_device_data_search_str.size()) {

      std::string::size_type entry_end = path_device_data_search_str.find(':', entry_begin);
      if(entry_end == std::string::npos) {
        entry_end = path_device_data_search_str.size();
      }

      std::string entry = path_device_data_search_str.substr(entry_begin, entry_end - entry_begin);
      if(!entry.empty()) {
        append_root_dir_path(std::filesystem::path(entry), "AURORA2_DEVICE_DATA_PATH", true);
      }

      entry_begin = entry_end + 1;
    }
  }

  return root_device_data_dir_path_list;
}


 std::filesystem::path QLDeviceManager::deviceDataRootDirPath() {

  // the first root is the one that owns newly added devices, and the fallback for any
  // device whose own root is not known.
  std::vector<std::filesystem::path> root_device_data_dir_path_list = deviceDataRootDirPathList();

  if(root_device_data_dir_path_list.empty()) {

    // deviceDataRootDirPathList() always yields at least the installation root, so this is
    // unreachable in practice - but returning a default-constructed path is safer than
    // dereferencing an empty container.
    return std::filesystem::path();
  }

  return root_device_data_dir_path_list.front();
}


std::filesystem::path QLDeviceManager::deviceTypeRootDirPath(const std::string& family,
                                                             const std::string& foundry,
                                                             const std::string& node,
                                                             const std::string& devicename) {

  // a device's root is a property of the device TYPE, so match on the 4 coordinates only.
  // note this deliberately does not go through deviceTypeTreeElement(), which additionally
  // requires a matching variant + layout and would fail for a device_target that carries no
  // layout - and would be a triple-nested scan on a path that is called very often.
  for (const QLDeviceType& device: device_list) {

    if(device.family == family &&
       device.foundry == foundry &&
       device.node == node &&
       device.devicename == devicename) {

      if(!device.device_root_path.empty()) {
        return device.device_root_path;
      }
      break;
    }
  }

  // not in device_list yet (parseDeviceData() still running), or discovered before roots
  // were recorded: fall back to the first root.
  return deviceDataRootDirPath();
}


bool QLDeviceManager::deviceFileIsEncrypted(std::filesystem::path filepath) {

  // Extension-based shortcut: files ending in .en are always encrypted.
  if(filepath.extension() == ".en") {
    return true;
  }

  // Content-based detection: some files (e.g. sb_maps_generated.yml produced
  // by the AUTO/CUSTOM layout generator) carry the qlcrypt 'QLEN' magic but
  // use a regular extension. Peek the first 4 bytes for the magic header so
  // we still take the encrypted (--crypt_key_db) path for them.
  if(!FileUtils::FileExists(filepath)) {
    return false;
  }
  std::ifstream ifs(filepath, std::ios::binary);
  if(!ifs) {
    return false;
  }
  char buf[4] = {0};
  ifs.read(buf, 4);
  if(ifs.gcount() != 4) {
    return false;
  }
  return buf[0] == 'Q' && buf[1] == 'L' && buf[2] == 'E' && buf[3] == 'N';
}


// v2.10+: read in and keep the config JSON populated, and other API use the JSON.
std::filesystem::path QLDeviceManager::deviceConfigJSONPath(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path device_config_json_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  device_config_json_path = deviceTypeDirPath(device_target) / std::string("config.json");

  if(FileUtils::FileExists(device_config_json_path)) {

    // check the json file for the versions: 
    // CONFIG_FILE_VERSION >= v2.9.0 (reflects min compat with aurora version)
    // DEVICE_DATA_VERSION >= v2.0.0 (reflects structure change)
    // if they don't match expectations, we declare compatible config.json is not found
    // TODO.
  }
  else {

    // file does not exist!
    device_config_json_path.clear();
  }

  return device_config_json_path;
}

std::string QLDeviceManager::deviceDSPVersion(QLDeviceTarget device_target) {

  // Default DSP version when the device does not specify one (DSPV1.0).
  // The returned form is "<major>_<minor>" (e.g. "1_0", "2_0", "1_1"), which
  // maps directly onto the dsp_generator_v<major>_<minor> catalog directories.
  static const std::string DEFAULT_MAJOR_VERSION = "1";
  static const std::string DEFAULT_MINOR_VERSION = "0";
  std::string major = DEFAULT_MAJOR_VERSION;
  std::string minor = DEFAULT_MINOR_VERSION;

  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {

    // The device-data source repo renamed "DSP_TYPE" to "DSP_VERSION" (2026-08-20) and
    // packages pick it up as they re-sync, so both spellings are in the fleet at once.
    // Read either, newer name first. Without this a package on the new spelling matches
    // neither branch and silently reports DSPV1, which is what the IP catalog then
    // filters on (see IPGenerator).
    for( const char* key : {"DSP_VERSION", "DSP_TYPE"} ) {

      if( !device_target_config_json.contains(key) ) {
        continue;
      }

      const auto& dsp_version = device_target_config_json[key];
      if( !dsp_version.is_string() ) {
        continue;
      }

      // Skip any leading non-digits ("DSPV", "v") and capture the version:
      // group 1 = major, group 2 = optional minor (after a '.' or '_').
      // e.g. "DSPV2" -> 2 / (default), "DSPV1.1" -> 1 / 1, "v4.0" -> 4 / 0.
      static const std::regex dsp_re(R"(^\D*(\d+)(?:[._](\d+))?)");
      const std::string value = dsp_version.get<std::string>();
      std::smatch m;
      if( std::regex_search(value, m, dsp_re) ) {
        major = m[1].str();
        minor = m[2].matched ? m[2].str() : DEFAULT_MINOR_VERSION;
        break;
      }
      // A value that does not parse falls through to the other spelling rather than
      // being taken as authoritative. If neither parses the DSPV1 default stands:
      // this function returns a string and has no error channel.
    }
  }

  return major + "_" + minor;
}

std::string QLDeviceManager::normalizeVersionString(const std::string& value) {

  static const std::string DEFAULT_MINOR_VERSION = "0";

  // leading non-digits ("v", "DSPV") skipped; group 1 = major, group 2 = optional
  // minor after a '.' or '_'. "v2.4" -> 2/4, "v2" -> 2/default, "2_4" -> 2/4.
  static const std::regex version_re(R"(^\D*(\d+)(?:[._](\d+))?)");

  std::smatch m;
  if( std::regex_search(value, m, version_re) ) {
    return m[1].str() + "." +
           (m[2].matched ? m[2].str() : DEFAULT_MINOR_VERSION);
  }

  return std::string();
}

bool QLDeviceManager::parseVersionString(const std::string& version, int& major, int& minor) {

  // Strict: this consumes normalizeVersionString() output, so anything else is a
  // caller error, not something to salvage.
  static const std::regex parts_re(R"(^(\d+)\.(\d+)$)");

  std::smatch m;
  if( !std::regex_match(version, m, parts_re) ) {
    return false;
  }

  // Digits only, so the sole failure left is overflow. Parsed into locals first so
  // a throw on the minor cannot leave the caller with a half-written major.
  int parsed_major = 0;
  int parsed_minor = 0;
  try {
    parsed_major = std::stoi(m[1].str());
    parsed_minor = std::stoi(m[2].str());
  }
  catch(const std::exception&) {
    return false;
  }

  major = parsed_major;
  minor = parsed_minor;

  return true;
}

std::string QLDeviceManager::deviceCRRVersion(QLDeviceTarget device_target) {

  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {

    if( device_target_config_json.contains("CRR_VERSION") ) {
      const auto& crr_version = device_target_config_json["CRR_VERSION"];
      if( crr_version.is_string() ) {
        return normalizeVersionString(crr_version.get<std::string>());
      }
    }
  }

  return std::string();
}

// Spelling cleanup shared by both layout-setting normalisers: trim, drop an
// optional "FPGA_" prefix and upper-case. Only the SPELLING is shared here;
// the alias maps are not - see below.
static std::string canonicalizeLayoutToken(const std::string& value) {

  std::string token = value;

  const std::size_t first = token.find_first_not_of(" \t\r\n");
  const std::size_t last = token.find_last_not_of(" \t\r\n");
  if(first == std::string::npos) {
    return std::string();
  }
  token = token.substr(first, last - first + 1);

  // ascii upper-case: these are config identifiers, not user text, so stay
  // independent of the process locale.
  for(char& c : token) {
    if(c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }

  if(token.rfind("FPGA_", 0) == 0) {
    token = token.substr(5);
  }

  return token;
}

// Normalise "DEVICE_TYPE". Only CUSTOM and FIXED are legal.
//
// This deliberately does NOT apply normalizeLayoutMode()'s alias map, and the
// two must never be merged into one helper: that map contains FIXED -> CUSTOM,
// so reusing it here would turn a device that must never be re-shaped into a
// re-shapable one and silently delete the FIXED guard. That is exactly the bug
// add_layout.py has upstream - it normalises DEVICE_TYPE through the
// LAYOUT_MODE map, so a FIXED package with no DEVICE_TYPE_SETTINGS collapses to
// CUSTOM, finds no settings, falls back to LAYOUT_MODE=AUTO and auto-sizes hard
// silicon without a word. Aurora is the only guard against that.
// Returns false when the value is not a recognised device type.
static bool normalizeDeviceType(const std::string& value, std::string& out_device_type) {

  const std::string token = canonicalizeLayoutToken(value);

  if(token == "CUSTOM" || token == "FIXED") {
    out_device_type = token;
    return true;
  }

  return false;
}

// Normalise "DEVICE_TYPE_SETTINGS.LAYOUT_MODE" to AUTO / CUSTOM / RESOURCES.
//
// The two legacy spellings older packages carry are aliased: FIXED meant "an
// explicitly specified array", which is CUSTOM, and SCALED meant "scale to a
// resource budget", which is RESOURCES. This alias map is valid for LAYOUT_MODE
// ONLY - see normalizeDeviceType().
// Returns false when the value is not a recognised layout mode.
static bool normalizeLayoutMode(const std::string& value, std::string& out_layout_mode) {

  std::string token = canonicalizeLayoutToken(value);

  if(token == "FIXED") {
    token = "CUSTOM";
  }
  else if(token == "SCALED") {
    token = "RESOURCES";
  }

  if(token == "AUTO" || token == "CUSTOM" || token == "RESOURCES") {
    out_layout_mode = token;
    return true;
  }

  return false;
}


// The config.json keys this file reads. Spelled once here so a rename in the
// device data contract is a single edit, and a typo is a compile error rather
// than a key that silently never matches.
constexpr const char* CONFIG_KEY_DEVICE_TYPE = "DEVICE_TYPE";
constexpr const char* CONFIG_KEY_DEVICE_TYPE_SETTINGS = "DEVICE_TYPE_SETTINGS";
constexpr const char* CONFIG_KEY_LAYOUT_MODE = "LAYOUT_MODE";


QLDeviceLayoutSettings QLDeviceManager::deviceLayoutSettings(QLDeviceTarget device_target) {

  QLDeviceLayoutSettings layout_settings;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // reported in the caller's error messages even when the file is missing.
  layout_settings.config_json_path = deviceTypeDirPath(device_target) / std::string("config.json");

  json device_target_config_json;
  std::string config_parse_error;
  if(!loadDeviceConfigJSON(device_target, device_target_config_json, &config_parse_error)) {
    if(!config_parse_error.empty()) {
      // the file is there but unreadable. this must NOT degrade to "absent":
      // absent means "pre-contract package, take the layout-name path", and a
      // corrupt config on a FIXED device would take that path straight past the
      // gate that is the only thing protecting hard silicon.
      layout_settings.config_parse_failed = true;
      layout_settings.config_parse_error = config_parse_error;
      return layout_settings;
    }
    // no config.json: a package predating the layout-mode contract. the caller
    // falls back to the layout-name path so an already released kit keeps
    // working without a re-sync.
    return layout_settings;
  }
  if(!device_target_config_json.is_object()) {
    // valid JSON that is not a mapping is a corrupt device config, not an absent
    // one - contains() would just answer false for every key and we would fall
    // through as if the file were not there.
    layout_settings.config_parse_failed = true;
    layout_settings.config_parse_error = "top-level JSON is not an object";
    return layout_settings;
  }
  layout_settings.config_found = true;

  if( device_target_config_json.contains(CONFIG_KEY_DEVICE_TYPE) ) {
    const auto& device_type = device_target_config_json[CONFIG_KEY_DEVICE_TYPE];
    if( device_type.is_string() &&
        normalizeDeviceType(device_type.get<std::string>(), layout_settings.device_type) ) {
      layout_settings.device_type_present = true;
    }
    else {
      layout_settings.invalid = true;
      layout_settings.invalid_key = CONFIG_KEY_DEVICE_TYPE;
      layout_settings.invalid_value = device_type.is_string() ? device_type.get<std::string>()
                                                             : device_type.dump();
      return layout_settings;
    }
  }

  if( device_target_config_json.contains(CONFIG_KEY_DEVICE_TYPE_SETTINGS) ) {
    const auto& device_type_settings = device_target_config_json[CONFIG_KEY_DEVICE_TYPE_SETTINGS];
    if( !device_type_settings.is_object() ) {
      layout_settings.invalid = true;
      layout_settings.invalid_key = CONFIG_KEY_DEVICE_TYPE_SETTINGS;
      layout_settings.invalid_value = device_type_settings.dump();
      return layout_settings;
    }
    layout_settings.device_type_settings_present = true;

    if( device_type_settings.contains(CONFIG_KEY_LAYOUT_MODE) ) {
      const auto& layout_mode = device_type_settings[CONFIG_KEY_LAYOUT_MODE];
      if( layout_mode.is_string() &&
          normalizeLayoutMode(layout_mode.get<std::string>(), layout_settings.layout_mode) ) {
        layout_settings.layout_mode_present = true;
      }
      else {
        layout_settings.invalid = true;
        layout_settings.invalid_key =
            std::string(CONFIG_KEY_DEVICE_TYPE_SETTINGS) + "." + CONFIG_KEY_LAYOUT_MODE;
        layout_settings.invalid_value = layout_mode.is_string() ? layout_mode.get<std::string>()
                                                               : layout_mode.dump();
        return layout_settings;
      }
    }
  }

  return layout_settings;
}

// Load the device's `config.json` into the supplied json object.
// Returns true on success, false if the file does not exist or parsing fails.
// config.json is always plaintext device data; it is never encrypted.
bool QLDeviceManager::loadDeviceConfigJSON(QLDeviceTarget device_target, json& out_config_json,
                                           std::string* out_parse_error) {

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  std::filesystem::path device_type_dir = deviceTypeDirPath(device_target);
  std::filesystem::path config_json_path = device_type_dir / std::string("config.json");

  if(!FileUtils::FileExists(config_json_path)) {
    return false;
  }

  try {
    std::ifstream ifs(config_json_path.string());
    out_config_json = json::parse(ifs);
    return true;
  }
  catch(const std::exception& e) {
    // report the parse failure to the caller when it asked, so it can tell a
    // corrupt config.json apart from an absent one. std::cout alone never
    // reaches the compiler's error stream.
    if(out_parse_error != nullptr) {
      *out_parse_error = e.what();
    }
    std::cout << "loadDeviceConfigJSON: failed to parse "
              << config_json_path.string() << ": " << e.what() << std::endl;
    return false;
  }
}


// Trailing junk is rejected: a half-parsed geometry would silently mis-size a device.
bool QLDeviceManager::parseWholeNumber(const std::string& text, int& out_value) {

  try {
    size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    while( (consumed < text.size()) && std::isspace(static_cast<unsigned char>(text[consumed])) ) {
      consumed++;
    }
    if(consumed != text.size()) {
      return false;
    }
    // stoi accepts a sign, but nothing config.json spells this way has a
    // meaningful negative value - a geometry least of all.
    if(value < 0) {
      return false;
    }
    out_value = value;
    return true;
  }
  catch(const std::exception&) {
    return false;
  }
}


bool QLDeviceManager::parseDeviceGeometry(const std::string& text, int& out_x, int& out_y) {

  const size_t separator = text.find_first_of("xX");
  if(separator == std::string::npos) {
    return false;
  }

  return parseWholeNumber(text.substr(0, separator), out_x) &&
         parseWholeNumber(text.substr(separator + 1), out_y);
}


// Derive the layout's resource counts from its resolved QLDeviceLayoutInfo.
//
// This is the only source of resource counts. They were previously read from a
// resources.json generated by running vpr over the architecture, which a package
// cut outside the Aurora tree never had; the counts are a pure function of the
// resolved layout (issue #2257):
//
//   bram = <bram columns> * floor(arrayY / <bram tile height>)
//   dsp  = <dsp columns>  * floor(arrayY / <dsp tile height>)
//   clb  = (arrayX - <bram columns> - <dsp columns>) * arrayY
//   io   = 2 * (arrayX + arrayY) * IO_CAPACITY
//
// An AUTO/RESOURCES device that has not been through Packing() yet - or one
// only being previewed, not yet the project's actual device - is expected to
// be unresolved; deriveDeviceResourceInformation() checks layout_info.resolved()
// itself to tell that apart from a genuine config.json problem.
std::vector<std::tuple<std::string, std::optional<int>>> QLDeviceManager::deriveResourceCounts(
    const QLDeviceLayoutInfo& layout_info, std::string* out_error) {

  std::vector<std::tuple<std::string, std::optional<int>>> resources_vector;

  const auto fail = [&](const std::string& reason) {
    if(out_error) {
      *out_error = reason;
    }
    return resources_vector;
  };

  if( !layout_info.resolved() ) {
    return fail("layout is not resolved");
  }

  const QLDeviceLayout& layout = layout_info.layout();

  int bram_tile_width = 0;
  int bram_tile_height = 0;
  int dsp_tile_width = 0;
  int dsp_tile_height = 0;
  if( !parseDeviceGeometry(layout.bramSize, bram_tile_width, bram_tile_height) || (bram_tile_height <= 0) ) {
    return fail("\"BRAM_SIZE\": \"" + layout.bramSize + "\" is not a positive <x>x<y> geometry");
  }
  if( !parseDeviceGeometry(layout.dspSize, dsp_tile_width, dsp_tile_height) || (dsp_tile_height <= 0) ) {
    return fail("\"DSP_SIZE\": \"" + layout.dspSize + "\" is not a positive <x>x<y> geometry");
  }

  // The clb count subtracts one array column per entry of bramCols/dspCols, so a
  // block wider than a single tile would leave clb too high and the columns-fit
  // check too lax. Every QLF_K6N10 package is 1xN today; decline rather than
  // report a count computed on an assumption the package contradicts, and whoever
  // ships the first wide block gets this message instead of a wrong number.
  const std::pair<const char*, int> block_widths[] = {
      {"BRAM_SIZE", bram_tile_width},
      {"DSP_SIZE",  dsp_tile_width},
  };
  for(const auto& [key, width] : block_widths) {
    if(width != 1) {
      return fail(std::string("\"") + key + "\" is " + std::to_string(width) +
                  " tiles wide, and the clb count assumes one tile per block column");
    }
  }
  if( !layout.ioCapacity.has_value() ) {
    return fail("config.json has no usable \"IO_CAPACITY\" key");
  }

  const int array_x = layout.arrayX;
  const int array_y = layout.arrayY;
  const int bram_columns = static_cast<int>(layout.bramCols.size());
  const int dsp_columns = static_cast<int>(layout.dspCols.size());
  if( (bram_columns + dsp_columns) >= array_x ) {
    // the column lists do not fit the array: report nothing rather than a
    // negative clb count.
    return fail("\"BRAM_COLS\" (" + std::to_string(bram_columns) + ") + \"DSP_COLS\" (" +
                std::to_string(dsp_columns) + ") do not fit an array " +
                std::to_string(array_x) + " columns wide");
  }

  resources_vector.push_back(std::make_tuple(std::string("clb"),
      std::optional<int>((array_x - bram_columns - dsp_columns) * array_y)));
  resources_vector.push_back(std::make_tuple(std::string("bram"),
      std::optional<int>(bram_columns * (array_y / bram_tile_height))));
  resources_vector.push_back(std::make_tuple(std::string("dsp"),
      std::optional<int>(dsp_columns * (array_y / dsp_tile_height))));
  resources_vector.push_back(std::make_tuple(std::string("io"),
      std::optional<int>(2 * (array_x + array_y) * (*layout.ioCapacity))));

  return resources_vector;
}


std::vector<std::tuple<std::string, std::optional<int>>> QLDeviceManager::deriveDeviceResourceInformation(
    QLDeviceTarget device_target, std::string* out_error) {

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // QLDeviceLayoutInfo is the single gate deciding whether a layout resolves
  // at all (AUTO/RESOURCES before Packing(), or one only being previewed and
  // not yet the project's actual device, comes back unresolved) - nothing
  // here re-derives that decision.
  const QLDeviceLayoutInfo layout_info(device_target);

  std::string reason;
  std::vector<std::tuple<std::string, std::optional<int>>> resources_vector =
      deriveResourceCounts(layout_info, &reason);

  if( resources_vector.empty() && !layout_info.resolved() ) {
    // not yet known, not a failure worth reporting.
    return resources_vector;
  }

  if( resources_vector.empty() && !reason.empty() && out_error ) {
    *out_error = std::string("cannot derive resource counts for ") +
                 convertToDeviceTypeString(device_target) + ": layout \"" +
                 device_target.device_variant_layout.name + "\": " + reason;
  }

  return resources_vector;
}


std::vector<std::tuple<std::string, std::optional<int>>> QLDeviceManager::deviceResourceInformation(
    QLDeviceTarget device_target) {

  // vector of tuples, one per resource type and its count - unset when not yet
  // known (e.g. an AUTO/RESOURCES device before Packing() has run):
  // [<"resourcename1",resourcecount1>,<"resourcename2",resourcecount2>,...]

  std::string derive_error;
  std::vector<std::tuple<std::string, std::optional<int>>> resources_vector =
      deriveDeviceResourceInformation(device_target, &derive_error);

  // the device is about to be shown with no resources at all, so say why rather
  // than leaving it unexplained. Silent when derive_error is empty: that means
  // "not yet known", not a failure.
  if( resources_vector.empty() && !derive_error.empty() ) {
    reportDeviceDataError(derive_error);
  }

  return resources_vector;
}


void QLDeviceManager::reportDeviceDataError(const std::string& message) {

  if( !reported_device_data_error_set.insert(message).second ) {
    return;
  }

  if(!GlobalSession) {
    return;
  }

  // The GUI walks device_data from the MainWindow constructor, before the console
  // the compiler's error stream is pointed at exists, so hold the message until
  // MainWindow has wired it and flushes. A batch run has no console to wait for -
  // stderr is its output, and nothing would ever flush.
  const bool gui = GlobalSession->CmdLine() && GlobalSession->CmdLine()->WithQt();
  if( gui && !device_data_error_console_ready ) {
    deferred_device_data_error_list.push_back(message);
    return;
  }

  CompilerOpenFPGA_ql* compiler = (CompilerOpenFPGA_ql*)GlobalSession->GetCompiler();
  if(compiler) {
    // append=false: no Tcl command is in flight while device_data is walked, and
    // appending would land this in the result of whichever one runs next.
    compiler->ErrorMessage(message, false);
  }
}


void QLDeviceManager::flushDeferredDeviceDataErrors() {

  // Check for somewhere to write BEFORE taking the queue: called with no compiler
  // yet, this has to keep holding the messages and stay un-flushed so that a later
  // call still delivers them, rather than dropping them on the floor.
  CompilerOpenFPGA_ql* compiler =
      GlobalSession ? (CompilerOpenFPGA_ql*)GlobalSession->GetCompiler() : nullptr;
  if(!compiler) {
    return;
  }

  device_data_error_console_ready = true;

  std::vector<std::string> messages;
  messages.swap(deferred_device_data_error_list);

  // reportDeviceDataError()'s dedupe covers one walk, and parseDeviceData() clears
  // it at the start of the next one - so a re-parse before this first runs can
  // queue a message already in the queue. Dedupe again here rather than printing
  // the same line twice.
  std::set<std::string> flushed;
  for(const std::string& message : messages) {
    if( flushed.insert(message).second ) {
      compiler->ErrorMessage(message, false);
    }
  }
}


std::filesystem::path QLDeviceManager::deviceTypeDirPath(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path device_type_dir_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // resolve against the root this device was actually discovered under, so that a device
  // installed into an external directory reads its own files - and, critically, its own
  // _Supp.db key database, which every decrypt path derives from this dir.
  device_type_dir_path =
      std::filesystem::path(deviceTypeRootDirPath(device_target.device_variant.family,
                                                  device_target.device_variant.foundry,
                                                  device_target.device_variant.node,
                                                  device_target.device_variant.devicename) /
                            device_target.device_variant.family /
                            device_target.device_variant.foundry /
                            device_target.device_variant.node /
                            device_target.device_variant.devicename);

  return device_type_dir_path;
}


std::filesystem::path QLDeviceManager::deviceVariantDirPath(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path device_variant_dir_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // same per-device root resolution as deviceTypeDirPath()
  device_variant_dir_path =
      std::filesystem::path(deviceTypeRootDirPath(device_target.device_variant.family,
                                                  device_target.device_variant.foundry,
                                                  device_target.device_variant.node,
                                                  device_target.device_variant.devicename) /
                            device_target.device_variant.family /
                            device_target.device_variant.foundry /
                            device_target.device_variant.node /
                            device_target.device_variant.devicename /
                            device_target.device_variant.voltage_threshold /
                            device_target.device_variant.p_v_t_corner);

  return device_variant_dir_path;
}


std::filesystem::path QLDeviceManager::deviceYosysScriptFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path aurora_template_script_yosys_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific yosys script
  // -- hardcoded path --
  // aurora_template_script_yosys_path = 
  //     std::filesystem::path(deviceTypeDirPath(device_target) / std::string("aurora") / std::string("aurora_template_script.ys"));
  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("AURORA_YOSYS_TEMPLATE_SCRIPT")  ) {
      json_value = device_target_config_json["AURORA_YOSYS_TEMPLATE_SCRIPT"].get<std::string>();
    }
    aurora_template_script_yosys_path = 
        deviceTypeDirPath(device_target) / json_value;
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {

    aurora_template_script_yosys_path = 
        std::filesystem::path(deviceTypeDirPath(device_target) / std::string("aurora_template_script.ys"));
  }

  // std::cout << "[zyxw]" << "using ys template: " << aurora_template_script_yosys_path.string() << std::endl;

  if(!FileUtils::FileExists(aurora_template_script_yosys_path)) {

    compiler->ErrorMessage("Cannot find device Yosys Template Script: " + aurora_template_script_yosys_path.string());
    return empty_path;
  }

  return aurora_template_script_yosys_path;
}

std::filesystem::path QLDeviceManager::deviceSynplifyScriptFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path aurora_template_script_synplify_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific synplify script
  // -- hardcoded path --
  // aurora_template_script_synplify_path = 
  //     std::filesystem::path(deviceTypeDirPath(device_target) / std::string("aurora") / std::string("aurora_template_script.prj"));
  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("AURORA_SYNPLIFY_TEMPLATE_SCRIPT")  ) {
      json_value = device_target_config_json["AURORA_SYNPLIFY_TEMPLATE_SCRIPT"].get<std::string>();
    }
    aurora_template_script_synplify_path = 
        deviceTypeDirPath(device_target) / json_value;
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {

    aurora_template_script_synplify_path = 
        std::filesystem::path(deviceTypeDirPath(device_target) / std::string("aurora_template_script.prj"));
  }

  // std::cout << "[zyxw]" << "using ys template: " << aurora_template_script_synplify_path.string() << std::endl;

  if(!FileUtils::FileExists(aurora_template_script_synplify_path)) {

    compiler->ErrorMessage("Cannot find device Synplify Template Script: " + aurora_template_script_synplify_path.string());
    return empty_path;
  }

  return aurora_template_script_synplify_path;
}


std::filesystem::path QLDeviceManager::deviceSettingsTemplateFile(QLDeviceTarget device_target) {

  std::filesystem::path empty_path;
  return empty_path;

}


std::filesystem::path QLDeviceManager::devicePowerTemplateFile(QLDeviceTarget device_target) {

  std::filesystem::path empty_path;
  return empty_path;

}


std::filesystem::path QLDeviceManager::deviceOpenFPGAScriptFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path aurora_template_script_openfpga_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific openfpga script

  // -- hardcoded path --
  // aurora_template_script_openfpga_path = 
  //     std::filesystem::path(deviceTypeDirPath(device_target) / std::string("aurora") / std::string("aurora_template_script.openfpga"));
  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("AURORA_OPENFPGA_TEMPLATE_SCRIPT")  ) {
      json_value = device_target_config_json["AURORA_OPENFPGA_TEMPLATE_SCRIPT"].get<std::string>();
    }
    aurora_template_script_openfpga_path = 
        deviceTypeDirPath(device_target) / json_value;
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    aurora_template_script_openfpga_path = 
        std::filesystem::path(compiler->GetSession()->Context()->DataPath() /
                              std::string("..") /
                              std::string("scripts") /
                              std::string("aurora_template_script.openfpga"));
  }

  // std::cout << "[zyxw]" << "using openfpga template: " << aurora_template_script_openfpga_path.string() << std::endl;

  if(!FileUtils::FileExists(aurora_template_script_openfpga_path)) {

    compiler->ErrorMessage("Cannot find device OpenFPGA Template Script: " + aurora_template_script_openfpga_path.string());
    return empty_path;
  }

  return aurora_template_script_openfpga_path;
}


std::filesystem::path QLDeviceManager::deviceVPRArchitectureFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path vpr_architecture_file_path;

  // if we are in auto/custom layout generation mode, then return the generated vpr xml filepath here
  // as we will not be using the (auto/cuustom) device's own vpr xml.

  if(compiler->m_autoLayoutGenerationMode ||
     compiler->m_customLayoutGenerationMode){
    if(!FileUtils::FileExists(compiler->m_autoLayoutGeneratedVPRXMLPath)) {

      compiler->ErrorMessage("Cannot find generated vpr architecture file: " + compiler->m_autoLayoutGeneratedVPRXMLPath.string());
      return empty_path;
    }
    return compiler->m_autoLayoutGeneratedVPRXMLPath;
  }

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific vpr architecture file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("CORNER_VPR_ARCH")  ) {

      json_value = device_target_config_json["CORNER_VPR_ARCH"].get<std::string>();
    }
    // check for unencrypted file
    vpr_architecture_file_path = 
        deviceVariantDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(vpr_architecture_file_path)) {

      // check for encrypted file
      vpr_architecture_file_path += ".en";
      if(!FileUtils::FileExists(vpr_architecture_file_path)) {

        compiler->ErrorMessage("Cannot find device vpr architecture file: " + vpr_architecture_file_path.string());
        return empty_path;
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // check for unencrypted file
    vpr_architecture_file_path = 
        std::filesystem::path(deviceVariantDirPath(device_target) / std::string("vpr.xml"));
    if(!FileUtils::FileExists(vpr_architecture_file_path)) {

      // check for encrypted file
      vpr_architecture_file_path += ".en";
      if(!FileUtils::FileExists(vpr_architecture_file_path)) {

        compiler->ErrorMessage("Cannot find device vpr architecture file: " + vpr_architecture_file_path.string());
        return empty_path;
      }
    }
  }

  // std::cout << "[zyxw]" << "using vpr arch file: " << vpr_architecture_file_path.string() << std::endl;

  return vpr_architecture_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGAArchitectureFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path openfpga_architecture_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific openfpga architecture file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("CORNER_OPENFPGA_ARCH")  ) {

      json_value = device_target_config_json["CORNER_OPENFPGA_ARCH"].get<std::string>();
    }
    // check for unencrypted file
    openfpga_architecture_file_path = 
        deviceVariantDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(openfpga_architecture_file_path)) {

      // check for encrypted file
      openfpga_architecture_file_path += ".en";
      if(!FileUtils::FileExists(openfpga_architecture_file_path)) {

        compiler->ErrorMessage("Cannot find device openfpga architecture file: " + openfpga_architecture_file_path.string());
        return empty_path;
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // check for unencrypted file
    openfpga_architecture_file_path = 
        std::filesystem::path(deviceVariantDirPath(device_target) / std::string("openfpga.xml"));
    if(!FileUtils::FileExists(openfpga_architecture_file_path)) {

      // check for encrypted file
      openfpga_architecture_file_path += ".en";
      if(!FileUtils::FileExists(openfpga_architecture_file_path)) {

        compiler->ErrorMessage("Cannot find device openfpga architecture file: " + openfpga_architecture_file_path.string());
        return empty_path;
      }
    }
  }

  // std::cout << "[zyxw]" << "using openfpga arch file: " << openfpga_architecture_file_path.string() << std::endl;

  return openfpga_architecture_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGABitstreamAnnotationFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path bitstream_annotation_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific bitstream annotation file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("BITSTREAM_ANNOTATION")  ) {

      json_value = device_target_config_json["BITSTREAM_ANNOTATION"].get<std::string>();
    }
    // check for unencrypted file
    bitstream_annotation_file_path = 
        deviceTypeDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(bitstream_annotation_file_path)) {

      // check for encrypted file
      bitstream_annotation_file_path += ".en";
      if(!FileUtils::FileExists(bitstream_annotation_file_path)) {

        compiler->ErrorMessage("Cannot find device bitstream annotation file: " + bitstream_annotation_file_path.string());
        return empty_path;
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // check for unencrypted file
    bitstream_annotation_file_path = 
        std::filesystem::path(deviceTypeDirPath(device_target) / std::string("bitstream_annotation.xml"));
    if(!FileUtils::FileExists(bitstream_annotation_file_path)) {

      // check for encrypted file
      bitstream_annotation_file_path += ".en";
      if(!FileUtils::FileExists(bitstream_annotation_file_path)) {

        compiler->ErrorMessage("Cannot find device bitstream annotation file: " + bitstream_annotation_file_path.string());
        return empty_path;
      }
    }
  }

  // std::cout << "[zyxw]" << "using bitstream annotation file: " << bitstream_annotation_file_path.string() << std::endl;

  return bitstream_annotation_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGARepackDesignConstraintFile(QLDeviceTarget device_target,
                                                                                bool device_only,
                                                                                bool report_missing) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path repack_design_constraint_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // 1. check if we have a repack_design_constraint.xml file in the project (generated) directory (unencrypted only)
  // 2. check if we have a repack_design_constraint.xml file in the TCL script directory (unencrypted only)
  // 3. use the device specific repack design contraint file, and note that we may have
  //    unencrypted (first priority) or encrypted file

  std::string repack_design_constraint_file_name = "repack_design_constraint.xml";

  // 1. project path check
  //    Skipped for device_only callers: the generated repack constraints are
  //    written into the project directory, so looking there for a template
  //    would pick up this design's own previous output.
  if(!device_only) {
    std::filesystem::path project_path = std::filesystem::path(compiler->ProjManager()->projectPath());
    repack_design_constraint_file_path = project_path / repack_design_constraint_file_name;
    if(!FileUtils::FileExists(repack_design_constraint_file_path)) {
      repack_design_constraint_file_path.clear();
    }
  }

  // 2. tcl script dir path check
  if(!device_only && repack_design_constraint_file_path.empty()) {
    if(settings_manager != nullptr) {
      std::filesystem::path tcl_script_dir_path = settings_manager->getTCLScriptDirPath();
        if(!tcl_script_dir_path.empty()) {
          repack_design_constraint_file_path = tcl_script_dir_path / repack_design_constraint_file_name;
          if(!FileUtils::FileExists(repack_design_constraint_file_path)) {
            repack_design_constraint_file_path.clear();
          }
        }
    }
  }

  // 3. device data dir path check
  if(repack_design_constraint_file_path.empty()) {
    // use config.json if it exists
    json device_target_config_json;
    if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
      // get json value
      std::string json_value;
      if( device_target_config_json.contains("REPACK_DESIGN_CONSTRAINT")  ) {

        json_value = device_target_config_json["REPACK_DESIGN_CONSTRAINT"].get<std::string>();
      }
      // check for unencrypted file
      repack_design_constraint_file_path = 
          deviceTypeDirPath(device_target) / json_value;
      if(!FileUtils::FileExists(repack_design_constraint_file_path)) {

        // check for encrypted file
        repack_design_constraint_file_path += ".en";
        if(!FileUtils::FileExists(repack_design_constraint_file_path)) {

          if(report_missing) {
            compiler->ErrorMessage("Cannot find device repack design contraint file: " + repack_design_constraint_file_path.string());
          }
          return empty_path;
        }
      }
    }
    // else, we assume that this is a legacy device data directory (< v2.8.0)
    else {
      // check for unencrypted file
      repack_design_constraint_file_path = 
          std::filesystem::path(deviceTypeDirPath(device_target) / repack_design_constraint_file_name);
      if(!FileUtils::FileExists(repack_design_constraint_file_path)) {

        // check for encrypted file
        repack_design_constraint_file_path += ".en";
        if(!FileUtils::FileExists(repack_design_constraint_file_path)) {

          if(report_missing) {
            compiler->ErrorMessage("Cannot find device repack design contraint file: " + repack_design_constraint_file_path.string());
          }
          return empty_path;
        }
      }
    }
  }

  // std::cout << "[zyxw]" << "using repack design contraint file: " << repack_design_constraint_file_path.string() << std::endl;

  return repack_design_constraint_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGAFixedSimFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path fixed_sim_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific fixed sim file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("FIXED_SIM_OPENFPGA")  ) {

      json_value = device_target_config_json["FIXED_SIM_OPENFPGA"].get<std::string>();
    }
    // check for unencrypted file
    fixed_sim_file_path = 
        deviceTypeDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(fixed_sim_file_path)) {

      // check for encrypted file
      fixed_sim_file_path += ".en";
      if(!FileUtils::FileExists(fixed_sim_file_path)) {

        compiler->ErrorMessage("Cannot find device fixed sim file: " + fixed_sim_file_path.string());
        return empty_path;
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // check for unencrypted file
    fixed_sim_file_path = 
        std::filesystem::path(deviceTypeDirPath(device_target) / std::string("fixed_sim_openfpga.xml"));
    if(!FileUtils::FileExists(fixed_sim_file_path)) {

      // check for encrypted file
      fixed_sim_file_path += ".en";
      if(!FileUtils::FileExists(fixed_sim_file_path)) {

        compiler->ErrorMessage("Cannot find device fixed sim file: " + fixed_sim_file_path.string());
        return empty_path;
      }
    }
  }

  // std::cout << "[zyxw]" << "using fixed sim file: " << fixed_sim_file_path.string() << std::endl;

  return fixed_sim_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGAFabricKeyFile(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path fabric_key_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific fabric key file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // this is a bit of a special handling case, because we still want to support
  // multiple layouts in a single device for tsmc16 until we decide to change it
  // to become multiple devices instead.
  // so, we will not rely **only** on the 'config.json', but will actually do:
  // 1. (new structure) if config.json found, find fabric file as : "aurora/layoutname_fabric_key.xml", or +.en
  // 2. (new structure) if layoutname_fabric_key.xml.* is not found, find fabric file using config.json value
  // 3. (legacy)if config.json not found, find fabric file as fabric_key/family_foundry_node_vt_corner_layoutname.xml or +.en


  // check config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("FABRIC_KEY")  ) {

      json_value = device_target_config_json["FABRIC_KEY"].get<std::string>();
    }

    // 1. check for specific layout's fabric key (**not** using the json value): aurora/layoutname_fabric_key.xml
    // check for unencrypted file
    fabric_key_file_path = 
        deviceTypeDirPath(device_target) /
        std::string("aurora") /
        std::string(device_target.device_variant_layout.name + "_fabric_key.xml");

    if(!FileUtils::FileExists(fabric_key_file_path)) {

      // check for encrypted file
      fabric_key_file_path += ".en";
      if(!FileUtils::FileExists(fabric_key_file_path)) {

        // mark the variable empty to indicate it was not found
        fabric_key_file_path.clear();
      }
    }

    // 2. if layoutname_ specific file not found, find the fabric key using the config.json value
    if(fabric_key_file_path.empty()) {

      // check for unencrypted file
      fabric_key_file_path = 
          deviceTypeDirPath(device_target) / json_value;
      if(!FileUtils::FileExists(fabric_key_file_path)) {

        // check for encrypted file
        fabric_key_file_path += ".en";
        if(!FileUtils::FileExists(fabric_key_file_path)) {

          fabric_key_file_path.clear();
        }
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // check for unencrypted file
    fabric_key_file_path = 
        std::filesystem::path(deviceTypeDirPath(device_target) /
                              std::string("fabric_key") /
                              std::string(device_target.device_variant.family + "_" +
                              device_target.device_variant.foundry + "_" +
                              device_target.device_variant.node + "_" +
                              device_target.device_variant.voltage_threshold + "_" +
                              device_target.device_variant.p_v_t_corner + "_" +
                              device_target.device_variant_layout.name + "_" +
                              std::string("fabric_key.xml")));
    if(!FileUtils::FileExists(fabric_key_file_path)) {

      // check for encrypted file
      fabric_key_file_path += ".en";
      if(!FileUtils::FileExists(fabric_key_file_path)) {

        fabric_key_file_path.clear();
      }
    }
  }

  // if(fabric_key_file_path.empty()) {
  //   // std::cout << "[zyxw]" << "no fabric key available, use autogenerated one" << std::endl;
  // }
  // else {
  //   // std::cout << "[zyxw]" << "using fabric key file: " << fabric_key_file_path.string() << std::endl;
  // }

  return fabric_key_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGABitstreamRemappingFile(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path bitstream_remapping_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific bitstream remapping file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // use config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("BITSTREAM_REMAPPING")  ) {

      json_value = device_target_config_json["BITSTREAM_REMAPPING"].get<std::string>();
    }
    // check for unencrypted file
    bitstream_remapping_file_path = 
        deviceTypeDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(bitstream_remapping_file_path)) {

      // check for encrypted file
      bitstream_remapping_file_path += ".en";
      if(!FileUtils::FileExists(bitstream_remapping_file_path)) {

        // compiler->Message("Cannot find device bitstream_remapping file: " + bitstream_remapping_file_path.string());
        return empty_path;
      }
    }
  }

  // std::cout << "[zyxw]" << "using bitstream_remapping file: " << bitstream_remapping_file_path.string() << std::endl;

  return bitstream_remapping_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGAPinTableFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path pin_table_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific pin table file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // this is a bit of a special handling case, because we still want to support
  //  multiple layouts in a single device for tsmc16 until we decide to change it
  //  to become multiple devices instead.
  // additionally, the pin table file can be placed in the 'design directory' which
  //  should override any other file in the device data directory, so we should search there
  //  first (design dir file gets priority)
  // so, we will not rely **only** on the 'config.json', but will actually do:
  // 1. (new structure) if config.json found, find pin table file as : "projectpath/../layoutname_pin_table.csv", or +.en
  //    1a. (new structure, not found in design dir, search device data) -> find pin table file as : "aurora/layoutname_pin_table.csv", or +.en
  // 2. (new structure) if layoutname_pin_table.csv.* is not found, find pin table as "projectpath/../filename-from-json-value", or +.en file 
  //    2a. (new structure, using json, not found in design dir, search device data) -> find pin table using config.json value
  // 3. (legacy) if config.json not found, find pin table file as "projectpath/../family_foundry_node_vt_corner_layoutname_pin_table.csv"
  //    3a. (legacy, not found in design dir, search device data) -> find pin table as : pin_table/family_foundry_node_vt_corner_layoutname_pin_table.csv or +.en


  // check config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("PIN_TABLE")  ) {

      json_value = device_target_config_json["PIN_TABLE"].get<std::string>();
    }

    // 1. check for specific layout's pin table (**not** using the json value): layoutname_pin_table.csv in project_path
    // check for unencrypted file
    pin_table_file_path = 
        std::filesystem::path(compiler->ProjManager()->projectPath()) /
        std::string("..") /
        std::string(device_target.device_variant_layout.name + "_pin_table.csv");

    if(!FileUtils::FileExists(pin_table_file_path)) {

      // check for encrypted file
      pin_table_file_path += ".en";
      if(!FileUtils::FileExists(pin_table_file_path)) {

        // mark the variable empty to indicate it was not found
        pin_table_file_path.clear();
      }
    }
    // 1a. not found in design dir, search in the device data: aurora/layoutname_pin_table.csv
    if(pin_table_file_path.empty()) {
      // check for unencrypted file
      pin_table_file_path = 
          deviceTypeDirPath(device_target) /
          std::string("aurora") /
          std::string(device_target.device_variant_layout.name + "_pin_table.csv");

      if(!FileUtils::FileExists(pin_table_file_path)) {

        // check for encrypted file
        pin_table_file_path += ".en";
        if(!FileUtils::FileExists(pin_table_file_path)) {

          // mark the variable empty to indicate it was not found
          pin_table_file_path.clear();
        }
      }
    }

    // 2. if layoutname_ specific file not found, find the pin table csv using the config.json value (filename only) in design dir
    if(pin_table_file_path.empty()) {

      // check for unencrypted file
      pin_table_file_path = 
          std::filesystem::path(compiler->ProjManager()->projectPath()) /
          std::string("..") /
          std::filesystem::path(json_value).filename();
      if(!FileUtils::FileExists(pin_table_file_path)) {

        // check for encrypted file
        pin_table_file_path += ".en";
        if(!FileUtils::FileExists(pin_table_file_path)) {

          pin_table_file_path.clear();
        }
      }
    }
    // 2a. json value filename not found in design dir, search device data
    if(pin_table_file_path.empty()) {

      // check for unencrypted file
      pin_table_file_path = 
          deviceTypeDirPath(device_target) / json_value;
      if(!FileUtils::FileExists(pin_table_file_path)) {

        // check for encrypted file
        pin_table_file_path += ".en";
        if(!FileUtils::FileExists(pin_table_file_path)) {

          pin_table_file_path.clear();
        }
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // 3. find file in design dir:
    // check for unencrypted file
    pin_table_file_path = 
        std::filesystem::path(std::filesystem::path(compiler->ProjManager()->projectPath()) /
                              std::string("..") /
                              std::string(device_target.device_variant.family + "_" +
                              device_target.device_variant.foundry + "_" +
                              device_target.device_variant.node + "_" +
                              device_target.device_variant.voltage_threshold + "_" +
                              device_target.device_variant.p_v_t_corner + "_" +
                              device_target.device_variant_layout.name + "_" +
                              std::string("pin_table.csv")));
    if(!FileUtils::FileExists(pin_table_file_path)) {

      // check for encrypted file
      pin_table_file_path += ".en";
      if(!FileUtils::FileExists(pin_table_file_path)) {

        pin_table_file_path.clear();
      }
    }
    // 3a. if not found in design dir, search the device data:
    if(pin_table_file_path.empty()) {
      // check for unencrypted file
      pin_table_file_path = 
          std::filesystem::path(deviceTypeDirPath(device_target) /
                                std::string("pin_table") /
                                std::string(device_target.device_variant.family + "_" +
                                device_target.device_variant.foundry + "_" +
                                device_target.device_variant.node + "_" +
                                device_target.device_variant.voltage_threshold + "_" +
                                device_target.device_variant.p_v_t_corner + "_" +
                                device_target.device_variant_layout.name + "_" +
                                std::string("pin_table.csv")));
      if(!FileUtils::FileExists(pin_table_file_path)) {

        // check for encrypted file
        pin_table_file_path += ".en";
        if(!FileUtils::FileExists(pin_table_file_path)) {

          pin_table_file_path.clear();
        }
      }
    }
  }

  if(pin_table_file_path.empty()) {
    compiler->ErrorMessage("Cannot find device pin table file!");
    return empty_path;
  }
  else {
    // std::cout << "[zyxw]" << "using pin table file: " << pin_table_file_path.string() << std::endl;
  }

  return pin_table_file_path;
}


std::filesystem::path QLDeviceManager::deviceOpenFPGAIOMapFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path io_map_file_path;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific io map file, and note that we may have
  // unencrypted (first priority) or encrypted file

  // this is a bit of a special handling case, because we still want to support
  //  multiple layouts in a single device for tsmc16 until we decide to change it
  //  to become multiple devices instead.
  // additionally, the io map file can be placed in the 'design directory' which
  //  should override any other file in the device data directory, so we should search there
  //  first (design dir file gets priority)
  // so, we will not rely **only** on the 'config.json', but will actually do:
  // 1. (new structure) if config.json found, find io map file as : "projectpath/../layoutname_fpga_io_map.xml", or +.en
  //    1a. (new structure, not found in design dir, search device data) -> find io map file as : "aurora/layoutname_fpga_io_map.xml", or +.en
  // 2. (new structure) if layoutname_fpga_io_map.xml.* is not found, find io map as "projectpath/../filename-from-json-value", or +.en file 
  //    2a. (new structure, using json, not found in design dir, search device data) -> find io map using config.json value
  // 3. (legacy) if config.json not found, find io map file as "projectpath/../family_foundry_node_vt_corner_layoutname_fpga_io_map.xml"
  //    3a. (legacy, not found in design dir, search device data) -> find io map as : fpga_io_map/family_foundry_node_vt_corner_layoutname_fpga_io_map.xml or +.en


  // check config.json if it exists
  json device_target_config_json;
  if(loadDeviceConfigJSON(device_target, device_target_config_json)) {
    // get json value
    std::string json_value;
    if( device_target_config_json.contains("FPGA_IO_MAP")  ) {

      json_value = device_target_config_json["FPGA_IO_MAP"].get<std::string>();
    }

    // 1. check for specific layout's io map (**not** using the json value): layoutname_fpga_io_map.xml in project_path
    // check for unencrypted file
    io_map_file_path = 
        std::filesystem::path(compiler->ProjManager()->projectPath()) /
        std::string("..") /
        std::string(device_target.device_variant_layout.name + "_fpga_io_map.xml");

    if(!FileUtils::FileExists(io_map_file_path)) {

      // check for encrypted file
      io_map_file_path += ".en";
      if(!FileUtils::FileExists(io_map_file_path)) {

        // mark the variable empty to indicate it was not found
        io_map_file_path.clear();
      }
    }
    // 1a. not found in design dir, search in the device data: aurora/layoutname_fpga_io_map.xml
    if(io_map_file_path.empty()) {
      // check for unencrypted file
      io_map_file_path = 
          deviceTypeDirPath(device_target) /
          std::string("aurora") /
          std::string(device_target.device_variant_layout.name + "_fpga_io_map.xml");

      if(!FileUtils::FileExists(io_map_file_path)) {

        // check for encrypted file
        io_map_file_path += ".en";
        if(!FileUtils::FileExists(io_map_file_path)) {

          // mark the variable empty to indicate it was not found
          io_map_file_path.clear();
        }
      }
    }

    // 2. if layoutname_ specific file not found, find the io map file using the config.json value (filename only) in design dir
    if(io_map_file_path.empty()) {

      // check for unencrypted file
      io_map_file_path = 
          std::filesystem::path(compiler->ProjManager()->projectPath()) /
          std::string("..") /
          std::filesystem::path(json_value).filename();
      if(!FileUtils::FileExists(io_map_file_path)) {

        // check for encrypted file
        io_map_file_path += ".en";
        if(!FileUtils::FileExists(io_map_file_path)) {

          io_map_file_path.clear();
        }
      }
    }
    // 2a. json value filename not found in design dir, search device data
    if(io_map_file_path.empty()) {

      // check for unencrypted file
      io_map_file_path = 
          deviceTypeDirPath(device_target) / json_value;
      if(!FileUtils::FileExists(io_map_file_path)) {

        // check for encrypted file
        io_map_file_path += ".en";
        if(!FileUtils::FileExists(io_map_file_path)) {

          io_map_file_path.clear();
        }
      }
    }
  }
  // else, we assume that this is a legacy device data directory (< v2.8.0)
  else {
    // 3. find file in design dir:
    // check for unencrypted file
    io_map_file_path = 
        std::filesystem::path(std::filesystem::path(compiler->ProjManager()->projectPath()) /
                              std::string("..") /
                              std::string(device_target.device_variant.family + "_" +
                              device_target.device_variant.foundry + "_" +
                              device_target.device_variant.node + "_" +
                              device_target.device_variant.voltage_threshold + "_" +
                              device_target.device_variant.p_v_t_corner + "_" +
                              device_target.device_variant_layout.name + "_" +
                              std::string("fpga_io_map.xml")));
    if(!FileUtils::FileExists(io_map_file_path)) {

      // check for encrypted file
      io_map_file_path += ".en";
      if(!FileUtils::FileExists(io_map_file_path)) {

        io_map_file_path.clear();
      }
    }
    // 3a. if not found in design dir, search the device data:
    if(io_map_file_path.empty()) {
      // check for unencrypted file
      io_map_file_path = 
          std::filesystem::path(deviceTypeDirPath(device_target) /
                                std::string("fpga_io_map") /
                                std::string(device_target.device_variant.family + "_" +
                                device_target.device_variant.foundry + "_" +
                                device_target.device_variant.node + "_" +
                                device_target.device_variant.voltage_threshold + "_" +
                                device_target.device_variant.p_v_t_corner + "_" +
                                device_target.device_variant_layout.name + "_" +
                                std::string("fpga_io_map.xml")));
      if(!FileUtils::FileExists(io_map_file_path)) {

        // check for encrypted file
        io_map_file_path += ".en";
        if(!FileUtils::FileExists(io_map_file_path)) {

          io_map_file_path.clear();
        }
      }
    }
  }

  if(io_map_file_path.empty()) {
    compiler->ErrorMessage("Cannot find device io map file!");
    return empty_path;
  }
  else {
    // std::cout << "[zyxw]" << "using io map file: " << io_map_file_path.string() << std::endl;
  }

  return io_map_file_path;
}


std::filesystem::path QLDeviceManager::deviceSBMAPSFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path sb_maps_yml_file_path;

  // if we are in auto/custom layout generation mode, then return the generated SB_MAPS.yml filepath here
  // as we will not be using the (auto/custom) device's own SB_MAPS.yml, even if it exists.

  if(compiler->m_autoLayoutGenerationMode ||
    compiler->m_customLayoutGenerationMode){
    if(!FileUtils::FileExists(compiler->m_autoLayoutGeneratedSBMapsYMLPath)) {

      compiler->ErrorMessage("Cannot find generated SB_MAPS.yml file: " + compiler->m_autoLayoutGeneratedSBMapsYMLPath.string());
      return empty_path;
    }
    return compiler->m_autoLayoutGeneratedSBMapsYMLPath;
  }

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // find SB_MAPS.yml as "<device-type-dir>/filename-from-json-value" (use config.json name)

  // check for config.json (it should exist at v2.9.0+)
  std::filesystem::path device_target_config_json_filepath = 
      deviceConfigJSONPath(device_target);
  if(device_target_config_json_filepath.empty()) {

    compiler->ErrorMessage("no compatible 'config.json' found in device data, cannot use this device!");
    return empty_path;
  }


  // read config JSON (handles plaintext config.json or encrypted config.json.en)
  json device_target_config_json;
  if(!loadDeviceConfigJSON(device_target, device_target_config_json)) {
    compiler->ErrorMessage("failed to load config.json (or config.json.en) for current device!");
    return empty_path;
  }
  // get json value
  std::string json_value;
  if( device_target_config_json.contains("SB_MAPS") ) {

    json_value = device_target_config_json["SB_MAPS"].get<std::string>();
  }


  // check for filename as in config.json from the device
  sb_maps_yml_file_path = 
      deviceTypeDirPath(device_target) / json_value;
  if(!FileUtils::FileIsRegular(sb_maps_yml_file_path)) {
      // Try .en encrypted variant
      std::filesystem::path en_path(sb_maps_yml_file_path.string() + ".en");
      if(FileUtils::FileIsRegular(en_path)) {
          sb_maps_yml_file_path = en_path;
      } else {
          sb_maps_yml_file_path.clear();
      }
  }

  if(sb_maps_yml_file_path.empty()) {
    compiler->Message("Cannot find device SB_MAPS file: " + sb_maps_yml_file_path.string());
  }
  else {
    compiler->Message("Using device SB_MAPS file: " + sb_maps_yml_file_path.string());
  }

  return sb_maps_yml_file_path;
}


std::filesystem::path QLDeviceManager::deviceSBTemplatesDir(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path sb_templates_dir_path;

  if(compiler->m_autoLayoutGenerationMode ||
    compiler->m_customLayoutGenerationMode){
    // The CSV switchbox templates are reused unchanged from the source device,
    // so we return its CSV directory as-is (encrypted or plaintext). To keep the
    // generated routing-graph set consistent, the freshly generated SB_MAPS is
    // encrypted to match these templates when they are encrypted -- see the
    // generated-SB_MAPS handling in CompilerOpenFPGA_ql::Packing(). VPR selects
    // CRR encrypted-vs-plaintext decoding off the --sb_maps '.en' suffix, so
    // SB_MAPS and these templates must share the same encryption state.
  }

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // find SB_TEMPLATES_DIR as "<devicevariant-dir>/filename-from-json-value" (use config.json name)

  // check for config.json (it should exist at v2.9.0+)
  std::filesystem::path device_target_config_json_filepath = 
      deviceConfigJSONPath(device_target);
  if(device_target_config_json_filepath.empty()) {

    compiler->ErrorMessage("no compatible 'config.json' found in device data, cannot use this device!");
    return empty_path;
  }


  // read config JSON (handles plaintext config.json or encrypted config.json.en)
  json device_target_config_json;
  if(!loadDeviceConfigJSON(device_target, device_target_config_json)) {
    compiler->ErrorMessage("failed to load config.json (or config.json.en) for current device!");
    return empty_path;
  }
  // get json value
  std::string json_value;
  if( device_target_config_json.contains("CORNER_SB_TEMPLATE_DIR") ) {

    json_value = device_target_config_json["CORNER_SB_TEMPLATE_DIR"].get<std::string>();
  }


  // check for filename as in config.json from the device
  sb_templates_dir_path = 
    deviceVariantDirPath(device_target) / json_value;
  if(!FileUtils::FileIsDirectory(sb_templates_dir_path)) {
    sb_templates_dir_path.clear();
  }

  if(sb_templates_dir_path.empty()) {
    compiler->Message("Cannot find device SB_TEMPLATES dir: " + sb_templates_dir_path.string());
  }
  else {
    compiler->Message("Using device SB_TEMPLATES dir: " + sb_templates_dir_path.string());
  }

  return sb_templates_dir_path;
}


std::filesystem::path QLDeviceManager::deviceVPRRRGraphFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path vpr_rr_graph_file_path;

  // if we are in auto/custom layout generation mode, then return the generated rr_graph.bin filepath here
  // as we will not be using the (auto/custom) device's own rr_graph.bin, even if it exists.

  if(compiler->m_autoLayoutGenerationMode ||
    compiler->m_customLayoutGenerationMode){
    if(!FileUtils::FileExists(compiler->m_autoLayoutGeneratedRRGraphBinPath)) {

      compiler->ErrorMessage("Cannot find generated rr_graph file: " + compiler->m_autoLayoutGeneratedRRGraphBinPath.string());
      return empty_path;
    }
    return compiler->m_autoLayoutGeneratedRRGraphBinPath;
  }

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific rr_graph file, and note that we will have bin files (should we support xml?)
  //
  // note that we will have a special handling case here, because we still want to support
  // having multiple layouts in the same device (atleast for tsmc16) and it doesn't break
  // use-cases where we have decided that one device = one layout going forward.
  //
  // also, the rr_graph depends on the layout, as well as the channelwidth, so include both parameters in search
  //
  // so, we will not rely **only** on the 'config.json', but will actually do:
  // 1. find rr_graph file as : "<vt>/<corner>/<layoutname>_rr_graph_<channelwidth>.bin"
  // 2. else, find rr_graph file as : "<vt>/<corner>/<layoutname>_rr_graph.bin" (default channel width for current device is *assumed*)
  // 3. else, find rr_graph as "<vt>/<corner>/filename-from-json-value" (use config.json name)

  // check for config.json (it should exist at v2.9.0+)
  std::filesystem::path device_target_config_json_filepath = 
      deviceConfigJSONPath(device_target);
  if(device_target_config_json_filepath.empty()) {

    compiler->ErrorMessage("no compatible 'config.json' found in device data, cannot use this device!");
    return empty_path;
  }


  // read config JSON (handles plaintext config.json or encrypted config.json.en)
  json device_target_config_json;
  if(!loadDeviceConfigJSON(device_target, device_target_config_json)) {
    compiler->ErrorMessage("failed to load config.json (or config.json.en) for current device!");
    return empty_path;
  }
  // get json value
  std::string json_value;
  if( device_target_config_json.contains("CORNER_RRGRAPH_BIN") ) {

    json_value = device_target_config_json["CORNER_RRGRAPH_BIN"].get<std::string>();
  }


  // 1. check for specific layout and channelwidth
  // (**not** using the json value): <layoutname>_rr_graph_<channelwidth>.bin
  // get current channelwidth from JSON settings:
  std::string route_chan_width;
  if( !QLSettingsManager::getStringValue("vpr", "route", "route_chan_width").empty() ) {

    route_chan_width = QLSettingsManager::getStringValue("vpr", "route", "route_chan_width");
  }
  vpr_rr_graph_file_path = 
      deviceVariantDirPath(device_target) /
      std::string(device_target.device_variant_layout.name + "_rr_graph" + "_" + route_chan_width +".bin");

  if(!FileUtils::FileExists(vpr_rr_graph_file_path)) {

    vpr_rr_graph_file_path.clear();
  }


  // 2. check for specific layout
  // (**not** using the json value): <layoutname>_rr_graph.bin
  if(vpr_rr_graph_file_path.empty()) {

    vpr_rr_graph_file_path = 
        deviceVariantDirPath(device_target) /
        std::string(device_target.device_variant_layout.name + "_rr_graph.bin");

    if(!FileUtils::FileExists(vpr_rr_graph_file_path)) {

      vpr_rr_graph_file_path.clear();
    }
  }


  // 3. check for filename as in config.json from the device
  if(vpr_rr_graph_file_path.empty()) {

    vpr_rr_graph_file_path = 
        deviceVariantDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(vpr_rr_graph_file_path)) {

        compiler->Message("Cannot find device vpr rr_graph file: " + vpr_rr_graph_file_path.string());
        vpr_rr_graph_file_path.clear();
    }
  }

  if(vpr_rr_graph_file_path.empty()) {
    compiler->Message("Cannot find device vpr rr_graph file: " + vpr_rr_graph_file_path.string());
  }
  else {
    compiler->Message("Using device vpr rr_graph file: " + vpr_rr_graph_file_path.string());
  }

  // std::cout << "[zyxw]" << "using vpr rr_graph file: " << vpr_rr_graph_file_path.string() << std::endl;

  return vpr_rr_graph_file_path;
}


std::filesystem::path QLDeviceManager::deviceVPRRouterLookaheadFile(QLDeviceTarget device_target) {

  CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::filesystem::path empty_path;
  std::filesystem::path vpr_router_lookahead_file_path;


  // if we are in auto/custom layout generation mode, then return the generated router_lookahead.bin filepath here
  // as we will not be using the (auto/custom) device's own router_lookahead.bin, even if it exists.

  if(compiler->m_autoLayoutGenerationMode ||
    compiler->m_customLayoutGenerationMode){
    if(!FileUtils::FileExists(compiler->m_autoLayoutGeneratedRouterLookaheadBinPath)) {

      compiler->ErrorMessage("Cannot find generated rr_graph file: " + compiler->m_autoLayoutGeneratedRouterLookaheadBinPath.string());
      return empty_path;
    }
    return compiler->m_autoLayoutGeneratedRouterLookaheadBinPath;
  }

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  // use the device specific router_lookahead file, and note that we will have bin files (should we support xml?)
  //
  // note that we will have a special handling case here, because we still want to support
  // having multiple layouts in the same device (atleast for tsmc16) and it doesn't break
  // use-cases where we have decided that one device = one layout going forward.
  //
  // also, the router_lookahead depends on the layout, as well as the channelwidth, so include both parameters in search
  //
  // so, we will not rely **only** on the 'config.json', but will actually do:
  // 1. find router_lookahead file as : "<vt>/<corner>/<layoutname>_router_lookahead_<channelwidth>.bin"
  // 2. else, find router_lookahead file as : "<vt>/<corner>/<layoutname>_router_lookahead.bin" (default channel width for current device is *assumed*)
  // 3. else, find router_lookahead as "<vt>/<corner>/filename-from-json-value" (use config.json name)

  // check for config.json (it should exist at v2.9.0+)
  std::filesystem::path device_target_config_json_filepath = 
      deviceConfigJSONPath(device_target);
  if(device_target_config_json_filepath.empty()) {

    compiler->ErrorMessage("no compatible 'config.json' found in device data, cannot use this device!");
    return empty_path;
  }


  // read config JSON (handles plaintext config.json or encrypted config.json.en)
  json device_target_config_json;
  if(!loadDeviceConfigJSON(device_target, device_target_config_json)) {
    compiler->ErrorMessage("failed to load config.json (or config.json.en) for current device!");
    return empty_path;
  }
  // get json value
  std::string json_value;
  if( device_target_config_json.contains("CORNER_ROUTER_LOOKAHEAD_BIN") ) {

    json_value = device_target_config_json["CORNER_ROUTER_LOOKAHEAD_BIN"].get<std::string>();
  }


  // 1. check for specific layout and channelwidth
  // (**not** using the json value): <layoutname>_router_lookahead_<channelwidth>.bin
  // get current channelwidth from JSON settings:
  std::string route_chan_width;
  if( !QLSettingsManager::getStringValue("vpr", "route", "route_chan_width").empty() ) {

    route_chan_width = QLSettingsManager::getStringValue("vpr", "route", "route_chan_width");
  }
  vpr_router_lookahead_file_path = 
      deviceVariantDirPath(device_target) /
      std::string(device_target.device_variant_layout.name + "_router_lookahead" + "_" + route_chan_width +".bin");

  if(!FileUtils::FileExists(vpr_router_lookahead_file_path)) {

    vpr_router_lookahead_file_path.clear();
  }


  // 2. check for specific layout
  // (**not** using the json value): <layoutname>_router_lookahead.bin
  if(vpr_router_lookahead_file_path.empty()) {

    vpr_router_lookahead_file_path = 
        deviceVariantDirPath(device_target) /
        std::string(device_target.device_variant_layout.name + "_router_lookahead.bin");

    if(!FileUtils::FileExists(vpr_router_lookahead_file_path)) {

      vpr_router_lookahead_file_path.clear();
    }
  }


  // 3. check for filename as in config.json from the device
  if(vpr_router_lookahead_file_path.empty()) {

    vpr_router_lookahead_file_path = 
        deviceVariantDirPath(device_target) / json_value;
    if(!FileUtils::FileExists(vpr_router_lookahead_file_path)) {

        vpr_router_lookahead_file_path.clear();
    }
  }

  if(vpr_router_lookahead_file_path.empty()) {
    compiler->Message("Cannot find device vpr router_lookahead file: " + vpr_router_lookahead_file_path.string());
  }
  else {
    compiler->Message("Using device vpr router_lookahead file: " + vpr_router_lookahead_file_path.string());
  }

  // std::cout << "[zyxw]" << "using vpr router_lookahead file: " << vpr_router_lookahead_file_path.string() << std::endl;

  return vpr_router_lookahead_file_path;
}


QLDeviceType QLDeviceManager::deviceTypeTreeElement(QLDeviceTarget device_target) {

  QLDeviceType devicetype = QLDeviceType();

  std::string device_string = convertToDeviceString(device_target);

  for (QLDeviceType device: device_list) {
    for (QLDeviceVariant device_variant: device.device_variants) {
      for (QLDeviceVariantLayout device_variant_layout: device_variant.device_variant_layouts) {
        std::string current_device_string = DeviceString(device_variant.family,
                                                         device_variant.foundry,
                                                         device_variant.node,
                                                         device_variant.devicename,
                                                         device_variant.voltage_threshold,
                                                         device_variant.p_v_t_corner,
                                                         device_variant_layout.name);
        if(current_device_string == device_string) {
          return device;
        }
      }
    }
  }

  return devicetype;
}

  // future use (not file access APIs, but used together with them)
std::vector<std::string> QLDeviceManager::deviceCorners(QLDeviceTarget device_target) {

  std::vector<std::string> corners;
  return corners;
}


std::vector<std::filesystem::path> QLDeviceManager::deviceCornerPowerDataFiles(QLDeviceTarget device_target) {

  std::vector<std::filesystem::path> corner_power_data_filepaths;
  return corner_power_data_filepaths;
}


std::filesystem::path QLDeviceManager::deviceYosysModulesDirPath(QLDeviceTarget device_target) {

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  std::filesystem::path device_yosys_modules_dir_path;

  device_yosys_modules_dir_path = deviceTypeDirPath(device_target) /
                                  "yosys" /
                                  "quicklogic";

  return device_yosys_modules_dir_path;
}


std::string QLDeviceManager::deviceYosysFamilyName(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  std::string device_yosys_family;

  if(device_target.device_variant.family == "QLF_K6N10") {
    device_yosys_family = "qlf_k6n10f";
  }
  else if(device_target.device_variant.family == "QLF_K4N8") {
    device_yosys_family = "qlf_k4n8";
  }

  return device_yosys_family;
}


std::string QLDeviceManager::deviceSynplifyFamilyName(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  std::string device_synplify_family;

  if(device_target.device_variant.family == "QLF_K6N10") {
    device_synplify_family = "QLF_K6N10";
  }

  return device_synplify_family;
}


std::vector<std::filesystem::path> QLDeviceManager::deviceYosysModulesPathList(QLDeviceTarget device_target) {

  // CompilerOpenFPGA_ql* compiler = static_cast<CompilerOpenFPGA_ql*>(GlobalSession->GetCompiler());

  std::vector<std::filesystem::path> yosys_modules_pathlist;

  if( !isDeviceTargetValid(device_target) ) {
    device_target = this->device_target;
  }

  std::filesystem::path device_yosys_modules_dir_path;
  std::string device_yosys_family;
  

  if(device_target.device_variant.family == "QLF_K6N10") {
    device_yosys_family = "qlf_k6n10f";
  }

  device_yosys_modules_dir_path = deviceYosysModulesDirPath(device_target) /
                                  device_yosys_family;

  std::error_code ec;
  for (const std::filesystem::directory_entry& dir_entry :
      std::filesystem::recursive_directory_iterator(device_yosys_modules_dir_path,
                                                    std::filesystem::directory_options::skip_permission_denied,
                                                    ec)) {
    if(ec) {
      std::cout << std::string("failed listing contents of ") +
                              device_yosys_modules_dir_path.string() << std::endl;
      return yosys_modules_pathlist;
    }

    if(dir_entry.is_regular_file(ec)) {

      // match modules types:
      std::string verilog_pattern = ".*\\.v";
      std::string sv_pattern = ".*\\.sv";
      std::string txt_pattern = ".*\\.txt";
      
      if (std::regex_match(dir_entry.path().filename().string(),
                           std::regex(verilog_pattern, std::regex::icase))) {
        yosys_modules_pathlist.push_back(dir_entry.path());
      }

      if (std::regex_match(dir_entry.path().filename().string(),
                           std::regex(sv_pattern, std::regex::icase))) {
        yosys_modules_pathlist.push_back(dir_entry.path());
      }

      if (std::regex_match(dir_entry.path().filename().string(),
                           std::regex(txt_pattern, std::regex::icase))) {
        yosys_modules_pathlist.push_back(dir_entry.path());
      }
    }

    if(ec) {
      std::cout << std::string("error while checking: ") +  dir_entry.path().string() << std::endl;
      return yosys_modules_pathlist;
    }
  }

  // sort the entries for easier processing
  std::sort(yosys_modules_pathlist.begin(),yosys_modules_pathlist.end());

  return yosys_modules_pathlist;
}

} // namespace FOEDAG
