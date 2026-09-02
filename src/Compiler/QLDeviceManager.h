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


// the device package's layout-generation settings, read from its config.json:
//   "DEVICE_TYPE": "CUSTOM"                      -- may this fabric be re-shaped at all?
//   "DEVICE_TYPE_SETTINGS": {"LAYOUT_MODE": ...} -- how should the layout be produced?
// two independent axes with overlapping vocabularies, normalised separately - see
// normalizeDeviceType() / normalizeLayoutMode() in QLDeviceManager.cpp.
class QLDeviceLayoutSettings {
  public:
    // config.json was found and parsed.
    bool config_found = false;
    // config.json exists but did not parse. Distinct from absent: absent is a
    // pre-contract package and takes the layout-name path; corrupt must fail loudly.
    bool config_parse_failed = false;
    std::string config_parse_error;
    // "DEVICE_TYPE" was present and understood. when false, 'device_type' is
    // empty and the package predates the layout-mode contract.
    bool device_type_present = false;
    // "DEVICE_TYPE_SETTINGS" was present at all. Reported separately from LAYOUT_MODE,
    // which is only one of the keys the section can carry.
    bool device_type_settings_present = false;
    // "DEVICE_TYPE_SETTINGS.LAYOUT_MODE" was present and understood.
    bool layout_mode_present = false;
    // a key carried a value we do not understand. The caller must fail rather than guess.
    bool invalid = false;
    // the offending key and its verbatim value, for the error message.
    std::string invalid_key;
    std::string invalid_value;
    // canonical "CUSTOM" or "FIXED".
    std::string device_type;
    // canonical "AUTO", "CUSTOM" or "RESOURCES".
    std::string layout_mode;
    // the fabric the package records for itself, verbatim: "DEVICE_SIZE"
    // ("30x30" - ARRAY_X x ARRAY_Y, without the arch file's IO ring) and the
    // resource column lists ("12,25"). Descriptive metadata, not a mode
    // selector, so a value that does not parse is left for the reader to reject
    // and never sets 'invalid': failing every run on a package that mis-spelled
    // one would be far worse than whatever reads it declining to.
    bool device_size_present = false;
    std::string device_size;
    bool bram_cols_present = false;
    std::string bram_cols;
    bool dsp_cols_present = false;
    std::string dsp_cols;
    // the config.json this was read from, reported in errors and passed to
    // add_layout.py as --device_config. set even when the file is absent.
    std::filesystem::path config_json_path;
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
  // 'device_data_dir_path' is the device-type directory (<root>/<family>/<foundry>/<node>/<devicename>)
  // that this variant belongs to. it MUST be supplied while parsing device data, because the
  // encrypted vpr.xml is decrypted using the _Supp.db key database that sits in that same
  // directory - deriving it from a global root would read the wrong device's key under
  // multi-root discovery. when left empty, the owning root is looked up by device coordinates.
  std::vector<QLDeviceVariantLayout> listDeviceVariantLayouts(std::string family,
                                                            std::string foundry,
                                                            std::string node,
                                                            std::string devicename,
                                                            std::string voltage_threshold,
                                                            std::string p_v_t_corner,
                                                            std::filesystem::path device_data_dir_path = std::filesystem::path());
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

  // the ordered list of device-data roots that are searched for devices.
  // order (highest precedence first):
  //   [1] $AURORA2_DEVICE_DATA_DIR - when set and valid this is EXCLUSIVE: it replaces the
  //       whole list, preserving the long-standing drop-in-place override behaviour that CI
  //       and the device-data validation gate rely on.
  //   [2] the installation's device_data dir (built-in devices, authoritative)
  //   [3] roots registered by install_device (per-user registry)
  //   [4] $AURORA2_DEVICE_DATA_PATH - additive, ':'-separated
  // roots that do not exist are dropped here (with a warning), so callers never have to
  // guard against a stale registry entry or a mistyped env var.
  std::vector<std::filesystem::path> deviceDataRootDirPathList();

  // first entry of deviceDataRootDirPathList(), i.e. the root that "owns" newly added
  // devices and the fallback when a device's own root is not known.
  // kept for the many call sites that legitimately want a single root.
  std::filesystem::path deviceDataRootDirPath();

  // install a device kit (.tar.gz produced by 'generate_device_kit') into a directory of the
  // user's choosing, OUTSIDE the Aurora installation, and register that directory so the
  // device is visible in later sessions without the user setting anything.
  //
  // the kit is already encrypted: this places files, it does not transform them. that is why
  // addDevice() cannot be reused - it re-encrypts on the way in and would need the plaintext
  // the customer does not have.
  //
  // returns 0 on success, -1 on failure. nothing is written to the target unless the archive
  // checksum, the manifest and the Aurora version check all pass first.
  int installDevice(std::string kit_archive_path, std::string target_dir_path, bool force);

  // remove a device that was installed with install_device, and deregister its root once no
  // devices remain under it.
  //
  // this is the only destructive command in the feature: it deletes from a directory the USER
  // named, not one Aurora owns. so it refuses to touch the installation, deletes only paths it
  // can attribute to the device, and supports a dry run.
  int uninstallDevice(std::string family, std::string foundry, std::string node,
                      std::string devicename, bool dry_run, bool force);

  // --- external device data root registry (per-user) -------------------------------------
  // install_device records the directory it installed into here, so the device stays visible
  // in later sessions without the user setting anything. uninstall_device removes it again.

  // <home>/.aurora/device_roots.json. empty when there is no usable home directory, in which
  // case the registry degrades to "not available" rather than failing.
  std::filesystem::path deviceRootRegistryFilePath();

  // registered roots, in registration order. a malformed registry is reported and treated as
  // empty rather than aborting - a corrupt config file must not make Aurora unusable.
  std::vector<std::filesystem::path> readDeviceRootRegistry();

  // add/remove a root. both are atomic and safe against concurrent Aurora processes.
  // adding an already-registered root succeeds without duplicating it.
  bool registerDeviceRoot(const std::filesystem::path& root_dir_path);
  bool unregisterDeviceRoot(const std::filesystem::path& root_dir_path);

  // the root that the given device was actually discovered under, looked up from device_list
  // by device coordinates. falls back to deviceDataRootDirPath() when the device is not (yet)
  // in the list - which is the case while parseDeviceData() is still running.
  std::filesystem::path deviceTypeRootDirPath(const std::string& family,
                                              const std::string& foundry,
                                              const std::string& node,
                                              const std::string& devicename);

  bool deviceFileIsEncrypted(std::filesystem::path filepath);

  // Load the device's plaintext `config.json` into `out_config_json`.
  // config.json is device data and is never encrypted.
  // Returns true on success; false if the file is absent or JSON parsing fails.
  // When `out_parse_error` is supplied it distinguishes the two failures: it is
  // set to the parser's message when the file exists but does not parse, and
  // left untouched when the file is simply absent.
  bool loadDeviceConfigJSON(QLDeviceTarget device_target, json& out_config_json,
                            std::string* out_parse_error = nullptr);

  std::filesystem::path deviceConfigJSONPath(QLDeviceTarget device_target = QLDeviceTarget());
  // DSP version supported by the device, from "DSP_VERSION" in config.json, or
  // "DSP_TYPE" on packages predating that rename. Returns "<major>_<minor>"
  // (e.g. "v4.0" -> "4_0", "DSPV2" -> "2_0"). Defaults to "1_0" when neither is set.
  std::string deviceDSPVersion(QLDeviceTarget device_target = QLDeviceTarget());
  // CRR (custom routing resource) version of the device, from the
  // "CRR_VERSION" entry in config.json. Returns "<major>.<minor>"
  // (e.g. "v2.4" -> "2.4", "v2" -> "2.0"), or an empty string when the device
  // does not declare one.
  std::string deviceCRRVersion(QLDeviceTarget device_target = QLDeviceTarget());

  // Raw device-data version -> "<major>.<minor>": "v2.4" -> "2.4", "v2" -> "2.0",
  // no digits -> empty. Pure, so the parsing is testable without a device on disk.
  static std::string normalizeVersionString(const std::string& value);

  // "<major>.<minor>" -> numeric parts. False, outputs untouched, on anything else.
  static bool parseVersionString(const std::string& version, int& major, int& minor);

  // An architecture counts the IO ring in its layout size and a device config does
  // not: add_layout.py's 'WIDTH = ARRAY_X + 4' (two tiles per side). A fabric of
  // this size or smaller is all ring and no core.
  static constexpr int kLayoutIORingTiles = 4;

  // A resource column list as a device config spells it: ascending, comma
  // separated, no spaces.
  static std::string joinLayoutColumnList(const std::set<long>& columns);

  // Split a resource column list into its numbers, matching add_layout.py's
  // split_cols(): commas and/or whitespace. False on anything not a whole number.
  static bool parseLayoutColumnList(const std::string& value, std::set<long>& out_columns);

  // One bounded whole number, for the dimension keys.
  static bool parseLayoutDimension(const std::string& value, long& out_value);

  // The BRAM/DSP columns a generated architecture actually placed, read back out
  // of its <fixed_layout>, in device-config spelling.
  static bool readGeneratedLayoutResourceColumns(const std::filesystem::path& vpr_xml_filepath,
                                                 const std::string& layout_name,
                                                 std::string& out_bram_cols,
                                                 std::string& out_dsp_cols);

  // Does the override ask for the fabric the device already has? Equal means all
  // four values are present on both sides and match; an omitted key is not equal.
  static bool customLayoutMatchesDeviceGeometry(const json& custom_layout_json,
                                                const QLDeviceLayoutSettings& layout_settings,
                                                std::string& out_geometry);
  // Layout-generation settings of the device package, from "DEVICE_TYPE" and
  // "DEVICE_TYPE_SETTINGS.LAYOUT_MODE" in config.json. An absent key is
  // reported as not-present (a pre-2026.3 package), an unrecognised value as
  // invalid - never defaulted, because a wrong default here silently re-shapes
  // a device.
  QLDeviceLayoutSettings deviceLayoutSettings(QLDeviceTarget device_target = QLDeviceTarget());
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
  // device_only=true returns the device's own repack design constraint file,
  // skipping the project- and TCL-directory lookups. Used when the file is
  // wanted as a *template* to rewrite: the generated result is written into the
  // project directory, so consulting that directory here would feed a previous
  // run's output back in as its own template.
  // report_missing=false suppresses the ErrorMessage when the device has no such
  // file, for callers where it is optional.
  std::filesystem::path deviceOpenFPGARepackDesignConstraintFile(QLDeviceTarget device_target = QLDeviceTarget(),
                                                                bool device_only = false,
                                                                bool report_missing = true);
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
