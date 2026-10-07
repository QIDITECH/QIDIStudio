#ifndef slic3r_PrinterTaskDispatcher_hpp_
#define slic3r_PrinterTaskDispatcher_hpp_

#include <string>
#include <vector>
#include <utility>

#include <wx/event.h>

#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetworkTypes.hpp"
#endif

namespace Slic3r { namespace GUI {

class QDSDeviceManager;

enum class PrinterTaskType {
    StatusPanel,
    SetBox,
    RefreshRfid,
    UnbindDevice,
    DeletePrinterFiles,
    SetPrintOptions
};

enum class PrinterTaskTransport {
    Local,
    Cloud
};

struct LocalCommand {
    bool use_custom_method{ false };
    std::string script_name{ "script" };
    std::string script;
    std::string method;
};

enum class PrinterTaskErrorCode {
    None,
    InvalidArgument,
    MissingDispatcherDependency,
    UnsupportedEvent,
    MissingEnvironment,
    TransportUnavailable,
    SendFailed
};

struct PrinterTaskResult {
    bool success{ false };
    PrinterTaskErrorCode code{ PrinterTaskErrorCode::None };
    std::string message;
};

struct PrinterTask {
    PrinterTaskType type{ PrinterTaskType::StatusPanel };
    PrinterTaskTransport transport{ PrinterTaskTransport::Local };
    std::string device_id;

    wxEventType event_type{ wxEVT_NULL };
    int int_value{ 0 };
    std::string string_key;
    std::string string_value;

    int slot_index{ 0 };
    int filament_index{ -1 };
    std::vector<std::string> file_paths;

    std::vector<LocalCommand> local_commands;
    std::string local_action_type;

    std::string cloud_task_path;
    std::string cloud_body{ "{}" };

// y84
    std::string filament_color;
    std::string filament_vendor;
    std::string filament_type;
    // SetPrintOptions payload (mirrors DeviceSettingDialog::apply_print_options_to_device)
    bool print_opt_spaghetti{ false };   // enable_noodle_detection
    bool print_opt_fod{ false };          // enable_pre_print_model_check
    int  print_opt_sensitivity{ 0 };      // 0-based selection index (LOW/MEDIUM/HIGH)

    // Multi-request cloud batch: SetPrintOptions issues several independent cloud calls,
    // each as a (path, body) pair. Empty for the single-call task types above.
    std::vector<std::pair<std::string, std::string>> cloud_requests;
//y84
};

class PrinterTaskDispatcher {
public:
    explicit PrinterTaskDispatcher(QDSDeviceManager* device_manager);

    PrinterTaskResult dispatch(const PrinterTask& task) const;

#if QDT_RELEASE_TO_PUBLIC
    PrinterTaskResult dispatch(const PrinterTask& task, Environment env, TargetType target = PRINTERTYPE) const;
#endif

private:
    PrinterTaskResult dispatch_local(const PrinterTask& task) const;

    PrinterTaskResult build_local_task(PrinterTask& task) const;

#if QDT_RELEASE_TO_PUBLIC
    PrinterTaskResult dispatch_cloud(const PrinterTask& task, Environment env, TargetType target) const;
    PrinterTaskResult build_cloud_task(PrinterTask& task) const;
#endif

private:
    QDSDeviceManager* m_device_manager{ nullptr };
};

}} // namespace Slic3r::GUI

#endif // slic3r_PrinterTaskDispatcher_hpp_
