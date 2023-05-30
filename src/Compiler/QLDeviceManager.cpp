#include "QLDeviceManager.h"

#include <QObject>
#include <QWidget>
#include <QPalette>
#include <QDialog>
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

#include <iostream>
#include <set>
#include <regex>
#include <filesystem>

#include <CRFileCryptProc.hpp>
#include "CompilerOpenFPGA_ql.h"
#include "Compiler/Compiler.h"
#include "Utils/FileUtils.h"
#include "Utils/LogUtils.h"
#include "Utils/StringUtils.h"
#include "MainWindow/Session.h"

extern FOEDAG::Session* GlobalSession;

namespace FOEDAG {


// singleton init
QLDeviceManager* QLDeviceManager::instance = nullptr;


// static access function
QLDeviceManager* QLDeviceManager::getInstance(bool initialize) {

  // creation
  if(instance == nullptr) {
    std::cout << "create new QLDeviceManager()" << std::endl;
    instance = new QLDeviceManager();
  }

  // init, if needed
  if(initialize == true) {
    instance->parseDeviceData();
  }

  return instance;
}


QLDeviceManager::QLDeviceManager(QObject *parent)
    : QObject(parent) {}


QLDeviceManager::~QLDeviceManager() {
  std::cout << "~QLDeviceManager()" << std::endl;
  if(device_manager_widget != nullptr) {
    delete device_manager_widget;
  }
}


QWidget* QLDeviceManager::createDeviceSelectionWidget() {

  std::cout << "QLDeviceManager::createDeviceSelectionWidget()++" << std::endl;

  // always create a new widget, so many people can use.
  // it will always be owned by someone else, so should be ok.
  device_manager_widget = new QWidget();

  // // GUI element creation only
  // if(device_manager_widget == nullptr) {
  //   device_manager_widget = new QWidget();
  // }
  // else {
  //   // clear everything and recreate GUI
  //   qDeleteAll(device_manager_widget->children());
  // }

  QWidget* dlg = device_manager_widget;

  dlg->setWindowTitle("Device Selection");

  QVBoxLayout* dlg_toplevellayout = new QVBoxLayout();
  dlg->setLayout(dlg_toplevellayout);

  QHBoxLayout* dlg_familylayout = new QHBoxLayout();
  QHBoxLayout* dlg_foundrynodelayout = new QHBoxLayout();
  QHBoxLayout* dlg_voltage_thresholdlayout = new QHBoxLayout();
  QHBoxLayout* dlg_p_v_t_cornerlayout = new QHBoxLayout();
  QHBoxLayout* dlg_layoutlayout = new QHBoxLayout();
  QHBoxLayout* dlg_buttonslayout = new QHBoxLayout();

  QLabel* m_combobox_family_label = new QLabel("Family");
  QLabel* m_combobox_foundry_node_label = new QLabel("Foundry-Node");
  QLabel* m_combobox_voltage_threshold_label = new QLabel("Voltage Threshold");
  QLabel* m_combobox_p_v_t_corner_label = new QLabel("Corner");
  QLabel* m_combobox_layout_label = new QLabel("Layout");
  m_combobox_family = new QComboBox();
  m_combobox_foundry_node = new QComboBox();
  m_combobox_voltage_threshold = new QComboBox();
  m_combobox_p_v_t_corner = new QComboBox();
  m_combobox_layout = new QComboBox();
  m_combobox_family->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_foundry_node->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_voltage_threshold->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_p_v_t_corner->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combobox_layout->setSizeAdjustPolicy(QComboBox::AdjustToContents);

  families.clear();
  m_combobox_family->clear();
  for (QLDeviceType device: this->device_list) {
    families.insert(device.family);
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

  // the below code causes trigger of currentTextChanged for m_combobox_family on first GUI creation.
  // m_combobox_family->blockSignals(true);
  // m_combobox_family->setCurrentIndex(-1);
  // m_combobox_family->blockSignals(false);
  // m_combobox_family->setCurrentIndex(0);

  dlg_familylayout->addWidget(m_combobox_family_label);
  dlg_familylayout->addStretch();
  dlg_familylayout->addWidget(m_combobox_family);

  dlg_foundrynodelayout->addWidget(m_combobox_foundry_node_label);
  dlg_foundrynodelayout->addStretch();
  dlg_foundrynodelayout->addWidget(m_combobox_foundry_node);

  dlg_voltage_thresholdlayout->addWidget(m_combobox_voltage_threshold_label);
  dlg_voltage_thresholdlayout->addStretch();
  dlg_voltage_thresholdlayout->addWidget(m_combobox_voltage_threshold);

  dlg_p_v_t_cornerlayout->addWidget(m_combobox_p_v_t_corner_label);
  dlg_p_v_t_cornerlayout->addStretch();
  dlg_p_v_t_cornerlayout->addWidget(m_combobox_p_v_t_corner);

  dlg_layoutlayout->addWidget(m_combobox_layout_label);
  dlg_layoutlayout->addStretch();
  dlg_layoutlayout->addWidget(m_combobox_layout);

  m_message_label = new QLabel();
  m_message_label->setWordWrap(true);
  QPalette m_message_label_palette;
  m_message_label_palette.setColor(QPalette::Window, Qt::white);
  m_message_label_palette.setColor(QPalette::WindowText, Qt::red);
  m_message_label->setAutoFillBackground(true);
  m_message_label->setPalette(m_message_label_palette);
  //m_message_label->setStyleSheet("QLabel { background-color : red; color : blue; }");
  m_message_label->hide();

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

  dlg_toplevellayout->addLayout(dlg_familylayout);
  dlg_toplevellayout->addLayout(dlg_foundrynodelayout);
  dlg_toplevellayout->addLayout(dlg_voltage_thresholdlayout);
  dlg_toplevellayout->addLayout(dlg_p_v_t_cornerlayout);
  dlg_toplevellayout->addLayout(dlg_layoutlayout);
  dlg_toplevellayout->addWidget(m_message_label);
  dlg_toplevellayout->addLayout(dlg_buttonslayout);
  dlg_toplevellayout->addStretch();

  // trigger a self UI update according to the currently selected device:
  setCurrentDeviceTarget(device_target);

  std::cout << "QLDeviceManager::createDeviceSelectionWidget()--" << std::endl;

  return dlg;
}

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
  m_combobox_foundry_node->blockSignals(true);
  m_combobox_foundry_node->clear();

  family = family_qstring.toStdString();

  for (QLDeviceType device: this->device_list) {
    if(device.family == family) {

      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      foundrynodes.insert(_foundrynode);
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

  // when 'foundry - node' changes, repopulate all the 'voltage_threshold' entries accordingly

  // std::cout << "foundrynodechanged: " << foundrynode_qstring.toStdString() << std::endl;

  foundrynode = foundrynode_qstring.toStdString();
  std::vector<std::string> foundrynode_vector = convertFromFoundryNode(foundrynode);
  foundry = foundrynode_vector[0];
  node = foundrynode_vector[1];
  voltage_thresholds.clear();
  m_combobox_voltage_threshold->blockSignals(true);
  m_combobox_voltage_threshold->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        for (QLDeviceVariant variant : device.device_variants) {
            voltage_thresholds.insert(variant.voltage_threshold);
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
  m_combobox_p_v_t_corner->blockSignals(true);
  m_combobox_p_v_t_corner->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        for (QLDeviceVariant variant : device.device_variants) {
          if (variant.voltage_threshold == voltage_threshold) {
            p_v_t_corners.insert(variant.p_v_t_corner);
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
  m_combobox_layout->blockSignals(true);
  m_combobox_layout->clear();

  for (QLDeviceType device: this->device_list) {
    if (device.family == family) {
      std::string _foundrynode = convertToFoundryNode(device.foundry, device.node);
      if (_foundrynode == foundrynode) {
        for (QLDeviceVariant variant : device.device_variants) {
          if (variant.voltage_threshold == voltage_threshold) {
            if(variant.p_v_t_corner == p_v_t_corner) {
              for(QLDeviceVariantLayout _layout : variant.device_variant_layouts) {
                layouts.insert(_layout.name);
              }
            }
          }
        }
      }
    }
  }

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

  layout = layout_qstring.toStdString();

  if(currentDeviceTargetUpdateInProgress) {

    currentDeviceTargetUpdateInProgress = false; // done.
  }
  else {

    QLDeviceTarget _device_target = convertToDeviceTarget(family,
                                                          foundry,
                                                          node,
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
      // std::cout << " >> [voltage_threshold]   " << device_target.device_variant.voltage_threshold << std::endl;
      // std::cout << " >> [p_v_t_corner]        " << device_target.device_variant.p_v_t_corner << std::endl;
      // std::cout << " >> [layout name]         " << device_target.device_variant_layout.name << std::endl;
      // std::cout << " >> [layout width]        " << device_target.device_variant_layout.width << std::endl;
      // std::cout << " >> [layout height]       " << device_target.device_variant_layout.height << std::endl;
    }
  }

  // now check if the selected device_target (via GUI) is different from the currently set device_target,
  // if so, enable the buttons, and show info text to user:
  if(device_manager_widget != nullptr) {

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

        m_message_label->setText("Device Selection has changed!\n\nClick 'Apply' to set the new Device\nClick 'Reset' to reset to the original device");
        m_message_label->show();
        device_manager_widget->adjustSize();
      }
      else {
        // current device_target of the project is the same as the one selected in the GUI, no changes to apply.
        m_button_reset->setDisabled(true);
        m_button_apply->setDisabled(true);

        m_message_label->setText("");
        m_message_label->hide();
      }
    }
    else if(!device_target_selected_string.empty()) {
      // no currently set device target from JSON, probably new project.
      // then, if the device_target_selected is via GUI, allow user to click apply
      // so that we can do rest of the settings for selected device target...
      m_button_apply->setDisabled(false);

      m_message_label->setText("New Device Selection!\n\nClick 'Apply' to set the new Device!\n");
      m_message_label->show();
    }
  }
}


void QLDeviceManager::resetButtonClicked() {

  setCurrentDeviceTarget(device_target);

}


void QLDeviceManager::applyButtonClicked() {

  // device_target should be set to device_target_selected
  setCurrentDeviceTarget(device_target_selected);
  
  // this should also be triggering the reset of the 
  // settings JSON, using the 'template' JSON from the respective
  // device_type's directory...
  // TODO...

}


void QLDeviceManager::parseDeviceData() {

  std::string family;
  std::string foundry;
  std::string node;

  std::error_code ec;

  // get to the device_data dir path of the installation
  std::filesystem::path root_device_data_dir_path = 
      GlobalSession->Context()->DataPath();

  // clear the list before parsing
  device_list.clear();

  // each dir in the device_data is a family
  //    for each family, check for foundry dirs
  //        for each foundry, check for node 
  //            for each family-foundry-node dir, check the device_variants
  
  // look at the directories inside the device_data_dir_path for 'family' entries
  for (const std::filesystem::directory_entry& dir_entry_family : 
                    std::filesystem::directory_iterator(root_device_data_dir_path)) {
    
    if(dir_entry_family.is_directory()) {
      
      // we would see family at this level
      family = dir_entry_family.path().filename().string();

      // look at the directories inside the 'family' dir for 'foundry' entries
      for (const std::filesystem::directory_entry& dir_entry_foundry : 
                    std::filesystem::directory_iterator(dir_entry_family.path())) {

        if(dir_entry_foundry.is_directory()) {
      
          // we would see foundry at this level
          foundry = dir_entry_foundry.path().filename().string();

          // look at the directories inside the 'foundry' dir for 'node' entries
          for (const std::filesystem::directory_entry& dir_entry_node : 
                          std::filesystem::directory_iterator(dir_entry_foundry.path())) {

            if(dir_entry_node.is_directory()) {
            
              // we would see devices at this level
              node = dir_entry_node.path().filename().string();

              // get all the device_variants for this device:
              std::vector<QLDeviceVariant> device_variants = listDeviceVariants(family,
                                                                                foundry,
                                                                                node);

              if(device_variants.empty()) {
                // display error, but continue with other devices.
                std::cout << "error in parsing variants for device: " + family + "," + foundry + "," + node +"\n" << std::endl;
              }
              else {

                QLDeviceType device;
                device.family = family;
                device.foundry = foundry;
                device.node = node;
                device.device_variants = device_variants;

                device_list.push_back(device);
              }
            }
          }
        }
      }
    }
  }

  // DEBUG
  // for (QLDeviceType device: device_list) {
  //     std::cout << "Device: " + device.family + " " + device.foundry + " " + device.node << std::endl;
  //     for (QLDeviceVariant variant: device.device_variants) {
  //       std::cout << "  Variant: " + variant.family + " " + variant.foundry + " " + variant.node + " " + variant.voltage_threshold + " " + variant.p_v_t_corner << std::endl;
  //       for (QLDeviceVariantLayout layout: variant.device_variant_layouts) {
  //         std::cout << "    " + layout.name + " " + std::to_string(layout.width) + " " + std::to_string(layout.height) << std::endl;
  //       }
  //     }
  //     std::cout << "\n" << std::endl;
  // }
  //DEBUG
}


std::vector<QLDeviceVariant> QLDeviceManager::listDeviceVariants(
    std::string family,
    std::string foundry,
    std::string node) {

  // prep an empty list of device variants for the current 'device'
  std::vector<QLDeviceVariant> device_variants;

  // std::string device_string = DeviceString(family,
  //                                          foundry,
  //                                          node,
  //                                          "",
  //                                          "",
  //                                          "");
  // std::cout << "parsing variants for: " + device_string << std::endl;

  // get to the device_data dir path of the installation
  std::filesystem::path root_device_data_dir_path = 
     GlobalSession->Context()->DataPath();

  // calculate the device_data dir path for specified device
  std::filesystem::path device_data_dir_path = root_device_data_dir_path / family / foundry / node;
  // std::cout << "device_data dir: " + device_data_dir_path.string() << std::endl;

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
    device_variant.voltage_threshold = voltage_threshold;
    device_variant.p_v_t_corner = p_v_t_corner;

    // list and store all the layouts available in this device_variant:
    device_variant.device_variant_layouts = listDeviceVariantLayouts(family,
                                                                     foundry,
                                                                     node,
                                                                     voltage_threshold,
                                                                     p_v_t_corner);

    // add the variant to the list
    device_variants.push_back(device_variant);
  }

  // sort the devices found - this needs custom sort function or < overloading for QLDeviceVariant, TODO.
  // std::sort(device_variants.begin(),device_variants.end());

  // debug prints
  // std::cout << std::endl;
  // std::cout << "device variants parsed:" << std::endl;
  // std::cout << "<family>,<foundry>,<node>,[voltage_threshold],[p_v_t_corner]" << std::endl;
  // int index = 1;
  // for (auto device_variant: device_variants) {
  //   std::cout << index << ". " << device_variant << std::endl;
  //   index++;
  // }
  // std::cout << std::endl;

  // [2][c] check other required and optional XML files for the device:
  // required:
  std::filesystem::path fixed_sim_openfpga_xml = 
      device_data_dir_path_c / "fixed_sim_openfpga.xml";
  std::filesystem::path fixed_sim_openfpga_xml_en = 
      device_data_dir_path_c / "fixed_sim_openfpga.xml.en";
  if(!std::filesystem::exists(fixed_sim_openfpga_xml) &&
     !std::filesystem::exists(fixed_sim_openfpga_xml_en)) {
    std::cout << "fixed_sim_openfpga.xml not found in source_device_data_dir_path!!!" << std::endl;
    return device_variants;
  }

  // optional: not checking these for now, if needed we can add in later.
  //std::filesystem::path bitstream_annotation_xml = 
  //    source_device_data_dir_path_c / "bitstream_annotation.xml";
  //std::filesystem::path repack_design_constraint_xml = 
  //    source_device_data_dir_path_c / "repack_design_constraint.xml";
  //std::filesystem::path fabric_key_xml = 
  //    source_device_data_dir_path_c / "fabric_key.xml";

  return device_variants;
}


std::vector<QLDeviceVariantLayout> QLDeviceManager::listDeviceVariantLayouts(std::string family,
                                                                             std::string foundry,
                                                                             std::string node,
                                                                             std::string voltage_threshold,
                                                                             std::string p_v_t_corner) {
  std::vector<QLDeviceVariantLayout> device_variant_layouts = {};
  
  std::filesystem::path root_device_data_dir_path = 
      GlobalSession->Context()->DataPath();
  
  std::filesystem::path device_data_dir_path = root_device_data_dir_path / family / foundry / node;;

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
    vpr_xml_filepath = ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->GenerateTempFilePath();

    std::filesystem::path m_cryptdbPath = 
        CRFileCryptProc::getInstance()->getCryptDBFileName(device_data_dir_path.string(),
                                                          family + "_" + foundry + "_" + node);

    if (!CRFileCryptProc::getInstance()->loadCryptKeyDB(m_cryptdbPath.string())) {
      std::cout << "load cryptdb failed!" << std::endl;
      ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles();
      return device_variant_layouts;
    }

    if (!CRFileCryptProc::getInstance()->decryptFile(source_vpr_xml_filepath, vpr_xml_filepath)) {
      std::cout << "decryption failed!" << std::endl;
      ((CompilerOpenFPGA_ql* )GlobalSession->GetCompiler())->CleanTempFiles();
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

          // similarly, we need to get: IOs, BRAM, DSP and CLB (and ... ?)

          device_variant_layouts.push_back(layout);
      }
  }

  return device_variant_layouts;
}

std::string QLDeviceManager::DeviceString(std::string family,
                                          std::string foundry,
                                          std::string node,
                                          std::string voltage_threshold,
                                          std::string p_v_t_corner,
                                          std::string layout_name) {

  // form the string representation of the device
  std::string device_string = family + "," + foundry + "," + node;

  if(!voltage_threshold.empty() && !p_v_t_corner.empty()) {
    device_string += "," + voltage_threshold + "," + p_v_t_corner;
  }

  if(!layout_name.empty()) {
    device_string += "," + layout_name;
  }

  return device_string;
}


bool QLDeviceManager::DeviceExists(std::string family,
                                   std::string foundry,
                                   std::string node,
                                   std::string voltage_threshold,
                                   std::string p_v_t_corner,
                                   std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,voltage_threshold,p_v_t_corner,layout_name);

  return DeviceExists(device_string);
}


bool QLDeviceManager::DeviceExists(std::string device_string) {

  QLDeviceTarget device_target = convertToDeviceTarget(device_string);
  if(isDeviceTargetValid(device_target)) {
       return true;
  }

  return false;
}

QLDeviceTarget QLDeviceManager::convertToDeviceTarget(std::string family,
                                                std::string foundry,
                                                std::string node,
                                                std::string voltage_threshold,
                                                std::string p_v_t_corner,
                                                std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,voltage_threshold,p_v_t_corner,layout_name);

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
                                             std::string voltage_threshold,
                                             std::string p_v_t_corner,
                                             std::string layout_name) {

  // form the string representation of the device
  std::string device_string = 
      DeviceString(family,foundry,node,voltage_threshold,p_v_t_corner,layout_name);
  
  setCurrentDeviceTarget(device_string);
}

void QLDeviceManager::setCurrentDeviceTarget(std::string device_string) {

  QLDeviceTarget device_target = convertToDeviceTarget(device_string);

  setCurrentDeviceTarget(device_target);

}

void QLDeviceManager::setCurrentDeviceTarget(QLDeviceTarget device_target) {

  if(isDeviceTargetValid(device_target)) {
    
    this->device_target = device_target;
    this->device_target_selected = device_target; // selected (via GUI) device is the same as the current device.

    // update GUI:
    if(device_manager_widget != nullptr) {
      int index;
      index = m_combobox_family->findText(QString::fromStdString(device_target.device_variant.family));
      // std::cout << "m_combobox_family index" << index << std::endl;
      m_combobox_family->blockSignals(true);
      m_combobox_family->setCurrentIndex(-1);
      m_combobox_family->blockSignals(false);
      currentDeviceTargetUpdateInProgress = true;
      m_combobox_family->setCurrentIndex(index);
    // rest of the items are set off by a cascade of signal-slots of the comboboxes.
    }
  }
  else {
    // update GUI:
    // invalid target device in "device_target" - JSON is incorrect, or missing?
    if(device_manager_widget != nullptr) {
      // trigger GUI to default selection (index0 of all fields...)
      m_combobox_family->blockSignals(true);
      m_combobox_family->setCurrentIndex(-1);
      m_combobox_family->blockSignals(false);
      m_combobox_family->setCurrentIndex(0);
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

  if(isDeviceTargetValid(device_target)) {

    device_string = device_target.device_variant.family + "," + 
                    device_target.device_variant.foundry + "," + 
                    device_target.device_variant.node + "," +
                    device_target.device_variant.voltage_threshold + "," + 
                    device_target.device_variant.p_v_t_corner + "," +
                    device_target.device_variant_layout.name;
  }

  return device_string;

}

} // namespace FOEDAG