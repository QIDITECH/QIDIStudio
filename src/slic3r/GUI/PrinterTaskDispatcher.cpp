#include "PrinterTaskDispatcher.hpp"

#include <boost/log/trivial.hpp>
#include <random>
#include <unordered_map>

#include "libslic3r/Utils.hpp"
#include "QDSDeviceManager.hpp"
#include "StatusPanel.hpp"
//#include "LocalesUtils.hpp"
#include "nlohmann/json.hpp"

#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

namespace {
//cj_4
// NFC-normalized UTF-8 so cloud JSON and Moonraker paths match Unicode file names (CJK, etc.).
void normalize_delete_task_paths_to_unicode_utf8(PrinterTask& task)
{
    if (task.type != PrinterTaskType::DeletePrinterFiles || task.file_paths.empty())
        return;
    for (std::string& p : task.file_paths) {
        if (p.empty())
            continue;
        p = normalize_utf8_nfc(p.c_str());
    }
}

// SetPrintOptions: maps the 0-based selection index to the klipper SAVE_VARIABLE
// level string (LOW/MEDIUM/HIGH), identical to DeviceSettingDialog::sensitivity_level_to_msg_string.
enum class PrintOptSensitivity { LOW = 0, MEDIUM = 1, HIGH = 2 };
std::string print_opt_sensitivity_to_string(PrintOptSensitivity level)
{
    switch (level) {
        case PrintOptSensitivity::LOW:    return "LOW";
        case PrintOptSensitivity::MEDIUM: return "MEDIUM";
        case PrintOptSensitivity::HIGH:   return "HIGH";
        default: return "";
    }
}
//y84
} // namespace


PrinterTaskDispatcher::PrinterTaskDispatcher(QDSDeviceManager* device_manager)
    : m_device_manager(device_manager)
{
}

PrinterTaskResult PrinterTaskDispatcher::dispatch(const PrinterTask& input_task) const
{
    if (input_task.transport == PrinterTaskTransport::Cloud) {
        return { false, PrinterTaskErrorCode::MissingEnvironment, "dispatch cloud task requires environment parameters" };
    }

    PrinterTask task = input_task;
    PrinterTaskResult result = build_local_task(task);
    if (!result.success)
        return result;

    return dispatch_local(task);
}

#if QDT_RELEASE_TO_PUBLIC
PrinterTaskResult PrinterTaskDispatcher::dispatch(const PrinterTask& input_task, Environment env, TargetType target) const
{
    PrinterTask task = input_task;
    if (task.transport == PrinterTaskTransport::Local) {
        PrinterTaskResult result = build_local_task(task);
        if (!result.success)
            return result;

        return dispatch_local(task);
    }

    PrinterTaskResult result = build_cloud_task(task);
    if (!result.success)
        return result;

    return dispatch_cloud(task, env, target);
}
#endif

PrinterTaskResult PrinterTaskDispatcher::build_local_task(PrinterTask& task) const
{
    if (task.type != PrinterTaskType::StatusPanel && task.type != PrinterTaskType::SetBox && task.type != PrinterTaskType::RefreshRfid && task.type != PrinterTaskType::DeletePrinterFiles && task.type != PrinterTaskType::SetPrintOptions) {
        return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported local task type" };
    }

    if (!task.local_commands.empty() || !task.local_action_type.empty())
        return { true, PrinterTaskErrorCode::None, "" };

    static const std::unordered_map<wxEventType, std::string> local_event_to_script = {
        {EVTSET_EXTRUDER_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=%d"},
        {EVTSET_HEATERBED_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=%d"},
        {EVTSET_CHAMBER_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=chamber TARGET=%d"},
        {EVTSET_CASE_LIGHT, "SET_PIN PIN=caselight VALUE=%d"},
        //cj_3
        {EVTSET_COOLER_SWITCH, "SET_PIN PIN=polar_cooler VALUE=%d"},
        {EVTSET_X_AXIS, "G91\nG1 X%d F7800\nG90"},
        {EVTSET_Y_AXIS, "G91\nG1 Y%d F7800\nG90"},
        {EVTSET_Z_AXIS, "G91\nG1 Z%d F600\nG90"},
        {EVTSET_RETURN_SAFEHOME, "G28"},
        {EVTSET_INSERT_READ, "SET_ENABLE_RFID_READ ENABLE=\"%d\""},
        {EVTSET_BOOT_READ, "SET_ENABLE_INIT_READ_RFID ENABLE=\"%d\""},
        {EVTSET_AUTO_FILAMENT, "SET_ENABLE_AUTO RELOAD ENABLE=\"%d\""},
        {EVTSET_COOLLINGFAN_SPEED, "SET_FAN_SPEED FAN=cooling_fan SPEED="},
        {EVTSET_AUXILIARYFAN_SPEED, "SET_FAN_SPEED FAN=auxiliary_cooling_fan SPEED="},
        {EVTSET_CHAMBERFAN_SPEED, "SET_FAN_SPEED FAN=chamber_circulation_fan SPEED="},
        {EVTSET_PRINT_CONTROL, "PAUSE"},
        // 打印速度：Marlin M220 S<percent>，int_value 为 50/100/124/166
        {EVTSET_PRINT_SPEED, "M220 S%d"},
        // cj_4 Exclude print object: use EXCLUDE_OBJECT NAME=<object_name>
        {EVTSET_EXCLUDE_PRINT_OBJECT, "EXCLUDE_OBJECT NAME=%s"}
    };

    if (task.type == PrinterTaskType::StatusPanel) {
        auto it = local_event_to_script.find(task.event_type);
        if (it == local_event_to_script.end())
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported status-panel local event" };

        std::string script = it->second;
        if (task.event_type == EVTSET_EXTRUDER_TEMPERATURE ||
            task.event_type == EVTSET_HEATERBED_TEMPERATURE ||
            task.event_type == EVTSET_CHAMBER_TEMPERATURE ||
            task.event_type == EVTSET_CASE_LIGHT ||
            //cj_3
            task.event_type == EVTSET_COOLER_SWITCH ||
            task.event_type == EVTSET_X_AXIS ||
            task.event_type == EVTSET_Y_AXIS ||
            task.event_type == EVTSET_Z_AXIS ||
            task.event_type == EVTSET_INSERT_READ ||
            task.event_type == EVTSET_BOOT_READ ||
            task.event_type == EVTSET_AUTO_FILAMENT ||
            task.event_type == EVTSET_PRINT_SPEED) {
            const std::string placeholder = "%d";
            size_t pos = script.find(placeholder);
            if (pos != std::string::npos) {
                script.replace(pos, placeholder.length(), std::to_string(task.int_value));
            } else {
                script += std::to_string(task.int_value);
            }
        } else if (task.event_type == EVTSET_COOLLINGFAN_SPEED || task.event_type == EVTSET_CHAMBERFAN_SPEED || task.event_type == EVTSET_AUXILIARYFAN_SPEED) {
            //cj_2 使用固定小数点，避免欧洲等区域 Locale 下 wxFormat / stream 使用逗号导致下发脚本非法
            script += Slic3r::float_to_string_decimal_point(double(task.int_value) / 100.0, 2);
        } else if (task.event_type == EVTSET_EXCLUDE_PRINT_OBJECT) {
            // cj_4 Exclude print object: string_key contains object name
            const std::string placeholder = "%s";
            size_t pos = script.find(placeholder);
            if (pos != std::string::npos && !task.string_key.empty()) {
                script.replace(pos, placeholder.length(), task.string_key);
            }
        }

        if (task.string_key == "type") {
            int type_index = task.int_value;
            std::vector<std::string> types{ "pause", "resume", "cancel" };
            if (type_index < 0 || type_index > 2)
                type_index = 0;
            task.local_action_type = types[type_index];
            script = types[type_index];
        }
        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::SetBox) {
        std::string script;
        //y84
        if (task.event_type == EVTSET_FILAMENT_INFO) {
            script = "UPDATE_FILAMENT_INFORMATION SLOT=" + std::to_string(task.slot_index)
                   + " COLOR='" + task.filament_color + "'"
                   + " VENDOR='" + task.filament_vendor + "'"
                   + " FILAMENT='" + task.filament_type + "'";
        } else if (task.event_type == EVTSET_FILAMENT_LOAD) {
            script = "LOAD_FILAMENT_CONTROL SLOT=" + std::to_string(task.slot_index);
        } else if (task.event_type == EVTSET_FILAMENT_UNLOAD) {
            script = "UNLOAD_FILAMENT_CONTROL SLOT=" + std::to_string(task.slot_index);
        //cj_3
        } else if (task.event_type == EVTSET_FILAMENT_EJECT) {
            script = "EJECT_FILAMENT_CONTROL SLOT=" + std::to_string(task.slot_index);
        } else {
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported set-box local event" };
        }


        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::RefreshRfid) {
        std::string script = "READ_RFID SLOT=slot" + std::to_string(task.slot_index);
        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::DeletePrinterFiles) {
        normalize_delete_task_paths_to_unicode_utf8(task);
        for (const std::string& file_path : task.file_paths) {
            LocalCommand cmd;
            cmd.use_custom_method = true;
            cmd.script_name = "path";
            cmd.method = "server.files.delete_file";
            cmd.script = file_path;
            task.local_commands.push_back(cmd);
        }
        return { true, PrinterTaskErrorCode::None, "" };
    }

    //y84
    if (task.type == PrinterTaskType::SetPrintOptions) {
        auto send_var = [&](const std::string& name, const std::string& value) {
            if (value.empty())
                return;
            task.local_commands.push_back(LocalCommand{ false, "script", "SAVE_VARIABLE VARIABLE=" + name + " VALUE=" + value, "" });
        };

        send_var("enable_noodle_detection",      task.print_opt_spaghetti ? "1" : "0");
        send_var("enable_pre_print_model_check", task.print_opt_fod ? "1" : "0");

        int sel = task.print_opt_sensitivity;
        std::string lel = "\"'" + print_opt_sensitivity_to_string((PrintOptSensitivity) sel) + "'\"";
        send_var("noodle_sensitivity_level", lel);
        return { true, PrinterTaskErrorCode::None, "" };
    }

    return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported local task" };
}

#if QDT_RELEASE_TO_PUBLIC
PrinterTaskResult PrinterTaskDispatcher::build_cloud_task(PrinterTask& task) const
{
    if (!task.cloud_task_path.empty())
        return { true, PrinterTaskErrorCode::None, "" };

    if (task.device_id.empty())
        return { false, PrinterTaskErrorCode::InvalidArgument, "device_id is empty" };

    json body_json;
    body_json["serialNumber"] = task.device_id;
    // y84
    body_json["commandId"] = make_filament_info_command_id(task.device_id);

    if (task.type == PrinterTaskType::StatusPanel) {
        static const std::unordered_map<wxEventType, std::string> cloud_event_to_path = {
            {EVTSET_EXTRUESION, "/set/extrusion"},
            {EVTSET_BACK, "/set/back"},
            //cj_3
            {EVTSET_COOLER_SWITCH, "/set/cooler/switch"},
            {EVTSET_LEVELING_ENABLE, "/set/leveling/enable"},
            {EVTSET_AMS_ENABLE, "/set/ams/enable"},
            {EVTSET_COOLLINGFAN_SPEED, "/set/coolingFan/speed"},
            {EVTSET_CHAMBERFAN_SPEED, "/set/chamberFan/speed"},
            {EVTSET_AUXILIARYFAN_SPEED, "/set/auxiliaryFan/speed"},
            {EVTSET_CASE_LIGHT, "/set/case/light"},
            {EVTSET_BEEPER_SWITHC, "/set/beeper/switch"},
            {EVTSET_EXTRUDER_TEMPERATURE, "/set/extruder/temperature"},
            {EVTSET_PRINT_SPEED, "/set/print/speed"},
            {EVTSET_HEATERBED_TEMPERATURE, "/set/heaterBed/temperature"},
            {EVTSET_CHAMBER_TEMPERATURE, "/set/chamber/temperature"},
            {EVTSET_RETURN_SAFEHOME, "/set/return/safeHome"},
            {EVTSET_X_AXIS, "/set/x/axis"},
            {EVTSET_Y_AXIS, "/set/y/axis"},
            {EVTSET_Z_AXIS, "/set/z/axis"},
            {EVTSET_PRINT_CONTROL, "/set/print/control"},
            {EVTSET_INSERT_READ, "/ams/insert/filament/read/enable"},
            {EVTSET_BOOT_READ, "/ams/boot/read/enable"},
            {EVTSET_AUTO_FILAMENT, "/ams/auto/filament/enable"},
            // cj_4 Exclude print object uses common control param one interface
            {EVTSET_EXCLUDE_PRINT_OBJECT, "/common/control/param/one"}
        };

        auto it = cloud_event_to_path.find(task.event_type);
        if (it == cloud_event_to_path.end())
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported status-panel cloud event" };

        task.cloud_task_path = it->second;
        
        // cj_4 Special handling for exclude print object
        if (task.event_type == EVTSET_EXCLUDE_PRINT_OBJECT) {
            // paramValue from string_key (object name)
            body_json["paramValue"] = task.string_key;
            // command fixed to exclude_print_object
            body_json["command"] = "exclude_print_object";
            // serialNumber already set above
        } else {
            // Original handling for other events
            if (task.string_key == "value") {
                body_json["value"] = task.int_value;
            } else if (task.string_key == "enable") {
                body_json["enable"] = bool(task.int_value);
            } else if (task.string_key == "type") {
                int type_index = task.int_value;
                std::vector<std::string> types{ "pause", "resume", "cancel" };
                if (type_index < 0 || type_index > 2)
                    type_index = 0;
                body_json["type"] = types[type_index];
            }
        }

        task.cloud_body = task.string_key.empty() ? "{}" : body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::SetBox) {
        static const std::unordered_map<wxEventType, std::string> cloud_box_event_to_path = {
            // y84
            {EVTSET_FILAMENT_INFO, "/ams/filament/info/edit/new"},
            {EVTSET_FILAMENT_LOAD, "/set/filament/load"},
            {EVTSET_FILAMENT_UNLOAD, "/set/filament/unload"},
            //cj_3
            {EVTSET_FILAMENT_EJECT, "/set/filament/eject"}
        };

        auto it = cloud_box_event_to_path.find(task.event_type);
        if (it == cloud_box_event_to_path.end())
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported set-box cloud event" };

        task.cloud_task_path = it->second;

//y84
        if (task.event_type == EVTSET_FILAMENT_INFO) {
            // Body matches cloud contract /community/v1/printer/ams/filament/info/edit/new
            // serialNumber is already injected at the top of build_cloud_task (== task.device_id)
            body_json["slot"]       = task.slot_index;
            body_json["vendor"]     = task.filament_vendor;
            body_json["material"]   = task.filament_type;
            body_json["color"]      = task.filament_color;
            body_json["mqttDirect"] = false;
        } else {
            body_json["slotIndex"] = task.slot_index;
        }

        task.cloud_body = body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };

    }

    if (task.type == PrinterTaskType::RefreshRfid) {
        task.cloud_task_path = "/set/filament/rfid";
        body_json["slotIndex"] = task.slot_index;
        task.cloud_body = body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::UnbindDevice) {
        task.cloud_task_path = "/unbind";
        body_json["source"] = "QIDIStudio";
        task.cloud_body = body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::DeletePrinterFiles) {
        task.cloud_task_path = "/delete/file/batch";
        normalize_delete_task_paths_to_unicode_utf8(task);
        body_json["files"] = task.file_paths;
        task.cloud_body = body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };
    }

//y84
    if (task.type == PrinterTaskType::SetPrintOptions) {
        if (task.device_id.empty())
            return { false, PrinterTaskErrorCode::InvalidArgument, "device_id is empty" };

        json enable_noodle = { {"serialNumber", task.device_id}, {"commandId", make_filament_info_command_id(task.device_id)}, {"enable", task.print_opt_spaghetti} };
        json sens           = { {"serialNumber", task.device_id}, {"commandId", make_filament_info_command_id(task.device_id)}, {"sensitivity", task.print_opt_sensitivity + 1} };
        json enable_fod     = { {"serialNumber", task.device_id}, {"commandId", make_filament_info_command_id(task.device_id)}, {"enable", task.print_opt_fod} };

        task.cloud_requests.emplace_back("/detect/noodles/enable",        enable_noodle.dump());
        task.cloud_requests.emplace_back("/detect/noodles/sensitivity",   sens.dump());
        task.cloud_requests.emplace_back("/detect/foreign/matter/enable", enable_fod.dump());
        return { true, PrinterTaskErrorCode::None, "" };
    }

    return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported cloud task type" };
}
#endif

PrinterTaskResult PrinterTaskDispatcher::dispatch_local(const PrinterTask& task) const
{
    if (m_device_manager == nullptr)
        return { false, PrinterTaskErrorCode::MissingDispatcherDependency, "device manager is null" };

    if (task.device_id.empty())
        return { false, PrinterTaskErrorCode::InvalidArgument, "device_id is empty" };

    for (const LocalCommand& cmd : task.local_commands) {
        if (cmd.script.empty())
            continue;

        if (cmd.use_custom_method) {
            if (!m_device_manager->sendCommand(task.device_id, cmd.script_name, cmd.script, cmd.method))
                return { false, PrinterTaskErrorCode::SendFailed, "failed to send printer command" };
        } else {
            m_device_manager->sendCommand(task.device_id, cmd.script);
        }
    }

    if (!task.local_action_type.empty())
        m_device_manager->sendActionCommand(task.device_id, task.local_action_type);

    return { true, PrinterTaskErrorCode::None, "" };
}

#if QDT_RELEASE_TO_PUBLIC
PrinterTaskResult PrinterTaskDispatcher::dispatch_cloud(const PrinterTask& task, Environment env, TargetType target) const
{
//y84
    if (task.device_id.empty())
        return { false, PrinterTaskErrorCode::InvalidArgument, "invalid cloud request arguments" };

    auto post_one = [&](const std::string& path, const std::string& body) -> bool {
        HttpData httpData;
        httpData.env = env;
        httpData.target = target;
        httpData.taskPath = path;
        httpData.body = body.empty() ? "{}" : body;

        bool isSucceed = false;
        MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
        return isSucceed;
    };

    // SetPrintOptions issues several independent cloud calls at once.
    if (!task.cloud_requests.empty()) {
        for (const auto& req : task.cloud_requests) {
            if (!post_one(req.first, req.second)) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Request to machine is error, the task is : " << req.first << " , " << req.second << std::endl;
                return { false, PrinterTaskErrorCode::SendFailed, "http post task failed" };
            }
        }
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.cloud_task_path.empty())
        return { false, PrinterTaskErrorCode::InvalidArgument, "invalid cloud request arguments" };

    if (!post_one(task.cloud_task_path, task.cloud_body)) {
        return { false, PrinterTaskErrorCode::SendFailed, "http post task failed" };
    }
//y84
    return { true, PrinterTaskErrorCode::None, "" };
}
#endif

}} // namespace Slic3r::GUI
