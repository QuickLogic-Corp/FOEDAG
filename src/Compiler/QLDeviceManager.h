#include <QWidget>
#include <QString>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>


#include <string>
#include <vector>
#include <set>
#include <filesystem>

#include "nlohmann_json/json.hpp"

#ifndef QLDEVICEMANAGER_H
#define QLDEVICEMANAGER_H

using json = nlohmann::ordered_json;

namespace FOEDAG {

class QLSettingsManager;


struct LayoutInfoHelper {
LayoutInfoHelper(const std::string& name): name(name){}
std::string name;
int clb;
int dsp;
int bram;
int io;
};

class QLDeviceVariantLayout {
    public:
    std::string name;
    int width = 0;
    int height = 0;
    int bram = 0;
    int dsp = 0;
    int clb = 0;
    int io = 0;
};

class QLDeviceVariant {
    public:
    std::string family;
    std::string foundry;
    std::string node;
    std::string devicename;
    std::string voltage_threshold;
    std::string p_v_t_corner;
    std::vector<QLDeviceVariantLayout> device_variant_layouts;
};

class QLDeviceType {
    public:
    std::string family;
    std::string foundry;
    std::string node;
    std::string devicename;
    std::vector<QLDeviceVariant> device_variants;
    std::filesystem::path device_root_path;
};


class QLDeviceTarget  {
  public:
    QLDeviceVariant device_variant;
    QLDeviceVariantLayout device_variant_layout;
};


class QLDeviceManager : public QObject {
  Q_OBJECT
 public:
  static QLDeviceManager* getInstance(bool initialize=false);
  static bool compareLayouts(const std::string& layout_1, const std::string& layout_2);
  ~QLDeviceManager();

 private:
  QLDeviceManager(QObject *parent = nullptr);
  // QLDeviceVariantLayout* findDeviceLayoutVariantPtr(const std::string& family, 
  //                                                   const std::string& foundry,
  //                                                   const std::string& node,
  //                                                   const std::string& devicename,
  //                                                   const std::string& voltage_threshold,
  //                                                   const std::string& p_v_t_corner,
  //                                                   const std::string& layoutName);
  // void collectDeviceVariantAvailableResources(const QLDeviceVariant& device_variant);
  // std::vector<std::shared_ptr<LayoutInfoHelper>> ExtractDeviceAvailableResourcesFromVprLogContent(const std::string&) const;

 public:
  void initialize();
  QWidget* createDeviceSelectionWidget(bool newProjectMode);
  void giveupDeviceSelectionWidget();
  void parseDeviceData();
  int addDevice(std::string family, std::string foundry, std::string node, std::string devicename,
                std::string device_data_source, bool force);
  int encryptDevice(std::string family, std::string foundry, std::string node, std::string devicename,
                    std::string device_data_source, std::string device_data_target,
                    std::string customer_id = "");
  std::vector<QLDeviceVariant> listDeviceVariants(std::string family,
                                                 std::string foundry,
                                                 std::string node,
                                                 std::string devicename);
  std::vector<QLDeviceVariant> listDeviceVariantsInDeviceDirectory(std::string family,
                                                 std::string foundry,
                                                 std::string node,
                                                 std::string devicename,
                                                 std::filesystem::path device_data_dir_path);
  std::vector<QLDeviceVariantLayout> listDeviceVariantLayouts(std::string family,
                                                            std::string foundry,
                                                            std::string node,
                                                            std::string devicename,
                                                            std::string voltage_threshold,
                                                            std::string p_v_t_corner);
  std::string DeviceString(std::string family,
                           std::string foundry,
                           std::string node,
                           std::string devicename,
                           std::string voltage_threshold,
                           std::string p_v_t_corner,
                           std::string layout_name);
  std::string DeviceTypeString(std::string family,
                              std::string foundry,
                              std::string node,
                              std::string devicename);
  bool DeviceExists(std::string family,
                    std::string foundry,
                    std::string node,
                    std::string devicename,
                    std::string voltage_threshold,
                    std::string p_v_t_corner,
                    std::string layout_name);
  bool DeviceExists(std::string device_string);
  bool DeviceExists(QLDeviceTarget device_target);
  QLDeviceTarget convertToDeviceTarget(std::string family,
                                 std::string foundry,
                                 std::string node,
                                 std::string devicename,
                                 std::string voltage_threshold,
                                 std::string p_v_t_corner,
                                 std::string layout_name);
  QLDeviceTarget convertToDeviceTarget(std::string device_string);
  std::string convertToDeviceString(QLDeviceTarget device_target = QLDeviceTarget());
  std::string convertToDeviceTypeString(QLDeviceTarget device_target = QLDeviceTarget());
  bool isDeviceTargetValid(QLDeviceTarget device_target);
  void setCurrentDeviceTarget(std::string family,
                              std::string foundry,
                              std::string node,
                              std::string devicename,
                              std::string voltage_threshold,
                              std::string p_v_t_corner,
                              std::string layout_name);
  void setCurrentDeviceTarget(std::string device_string);
  void setCurrentDeviceTarget(QLDeviceTarget device_target);
  // std::pair<std::filesystem::path, bool> GetArchitectureFileForDeviceVariant(const QLDeviceVariant& device_variant);
  std::string getCurrentDeviceTargetString();
  QLDeviceTarget getCurrentDeviceTarget();
  // only for GUI usage:
  std::string convertToFoundryNode(std::string foundry, std::string node);
  std::vector<std::string> convertFromFoundryNode(std::string foundrynode);

  // device files access API to have a uniform way of getting the required files
  public:

  std::filesystem::path deviceDataRootDirPath();

  bool deviceFileIsEncrypted(std::filesystem::path filepath);

  // Load the device's plaintext `config.json` into `out_config_json`.
  // config.json is device data and is never encrypted.
  // Returns true on success; false if the file is absent or JSON parsing fails.
  bool loadDeviceConfigJSON(QLDeviceTarget device_target, json& out_config_json);

  std::filesystem::path deviceConfigJSONPath(QLDeviceTarget device_target = QLDeviceTarget());
  // DSP version supported by the device, derived from the "DSP_TYPE" entry in
  // config.json. Returns "<major>_<minor>" (e.g. "DSPV2" -> "2_0",
  // "DSPV1.1" -> "1_1"). Defaults to "1_0" (DSPV1.0) when not specified.
  std::string deviceDSPVersion(QLDeviceTarget device_target = QLDeviceTarget());
  std::vector<std::tuple<std::string, int>> deviceResourceInformation(QLDeviceTarget device_target = QLDeviceTarget());
  
  std::filesystem::path deviceTypeDirPath(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceVariantDirPath(QLDeviceTarget device_target = QLDeviceTarget());
  
  std::filesystem::path deviceYosysModulesDirPath(QLDeviceTarget device_target = QLDeviceTarget());
  std::string deviceYosysFamilyName(QLDeviceTarget device_target = QLDeviceTarget());
  std::string deviceSynplifyFamilyName(QLDeviceTarget device_target = QLDeviceTarget());
  std::vector<std::filesystem::path> deviceYosysModulesPathList(QLDeviceTarget device_target = QLDeviceTarget());

  std::filesystem::path deviceYosysScriptFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceSynplifyScriptFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGAScriptFile(QLDeviceTarget device_target = QLDeviceTarget());

  std::filesystem::path deviceSettingsTemplateFile(QLDeviceTarget device_target);
  std::filesystem::path devicePowerTemplateFile(QLDeviceTarget device_target);

  std::filesystem::path deviceVPRArchitectureFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGAArchitectureFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGABitstreamAnnotationFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGARepackDesignConstraintFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGAFixedSimFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGAFabricKeyFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGABitstreamRemappingFile(QLDeviceTarget device_target = QLDeviceTarget());

  std::filesystem::path deviceOpenFPGAPinTableFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceOpenFPGAIOMapFile(QLDeviceTarget device_target = QLDeviceTarget());

  std::filesystem::path deviceSBMAPSFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceSBTemplatesDir(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceVPRRRGraphFile(QLDeviceTarget device_target = QLDeviceTarget());
  std::filesystem::path deviceVPRRouterLookaheadFile(QLDeviceTarget device_target = QLDeviceTarget());

  // future use (not file access APIs, but used together with them)
  QLDeviceType deviceTypeTreeElement(QLDeviceTarget device_target = QLDeviceTarget());
  std::vector<std::string> deviceCorners(QLDeviceTarget device_target);
  std::vector<std::filesystem::path> deviceCornerPowerDataFiles(QLDeviceTarget device_target);

 public:
 void triggerUIUpdate();
 void familyChanged(const QString& family_qstring);
 void foundrynodeChanged(const QString& foundrynode_qstring);
 void devicenameChanged(const QString& devicename_qstring);
 void voltage_thresholdChanged(const QString& voltagetheshold_qstring);
 void p_v_t_cornerChanged(const QString& p_v_t_corner_qstring);
 void layoutChanged(const QString& layout_qstring);
 void resetButtonClicked();
 void applyButtonClicked();

 public:
  // singleton instance of ourself
  static QLDeviceManager* instance;

  // hold a reference to singleton QLSettingsManager
  QLSettingsManager* settings_manager = nullptr;

  // hieracrchical list of all devices available in the installation
  std::vector <QLDeviceType> device_list;

  // flat list of all device targets (future?)
  //std::vector <QLDeviceTarget> device_target_list;

  // hold the current device_target
  QLDeviceTarget device_target;

  // hold the json object for the current device_target
  json device_target_json;

  // GUI objects and state maintenance
  QWidget* device_manager_widget = nullptr;

  // hold the 'selected' device_target via GUI
  QLDeviceTarget device_target_selected;

  // hold the 'selected' device parameters via GUI
  std::vector <std::string> families;
  std::string family;
  std::vector <std::string> foundrynodes;
  std::string foundrynode;
  std::string foundry;
  std::string node;
  std::vector <std::string> devicenames;
  std::string devicename;
  std::vector <std::string> voltage_thresholds;
  std::string voltage_threshold;
  std::vector <std::string> p_v_t_corners;
  std::string p_v_t_corner;
  std::vector <std::string> layouts;
  std::string layout;
  std::set <std::string> singularity;
  
  QComboBox* m_combobox_family;
  QComboBox* m_combobox_foundry_node;
  QComboBox* m_combobox_devicename;
  QComboBox* m_combobox_voltage_threshold;
  QComboBox* m_combobox_p_v_t_corner;
  QComboBox* m_combobox_layout;

  QLabel* m_device_resources_label;

  QPushButton* m_button_reset;
  QPushButton* m_button_apply;
  QLabel* m_message_label;

  bool currentDeviceTargetUpdateInProgress = false;
  bool newProjectMode = false;
};



}

#endif // QLDEVICEMANAGER_H
