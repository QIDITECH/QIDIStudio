#include "PrinterTaskDispatcher.hpp"

#include <boost/log/trivial.hpp>
#include <unordered_map>

#include "QDSDeviceManager.hpp"
#include "StatusPanel.hpp"
//#include "LocalesUtils.hpp"
#include "nlohmann/json.hpp"

#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

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
    if (task.type != PrinterTaskType::StatusPanel && task.type != PrinterTaskType::SetBox && task.type != PrinterTaskType::RefreshRfid && task.type != PrinterTaskType::DeletePrinterFiles) {
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
        {EVTSET_INSERT_READ, "SAVE_VARIABLE VARIABLE=auto_read_rfid VALUE=\"%d\""},
        {EVTSET_BOOT_READ, "SAVE_VARIABLE VARIABLE=auto_init_detect VALUE=\"%d\""},
        {EVTSET_AUTO_FILAMENT, "SAVE_VARIABLE VARIABLE=auto_reload_detect VALUE=\"%d\""},
        {EVTSET_COOLLINGFAN_SPEED, "SET_FAN_SPEED FAN=cooling_fan SPEED="},
        {EVTSET_AUXILIARYFAN_SPEED, "SET_FAN_SPEED FAN=auxiliary_cooling_fan SPEED="},
        {EVTSET_CHAMBERFAN_SPEED, "SET_FAN_SPEED FAN=chamber_circulation_fan SPEED="},
        // 打印速度：Marlin M220 S<percent>，int_value 为 50/100/124/166
        {EVTSET_PRINT_SPEED, "M220 S%d"}
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
        }

        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        if (task.string_key == "type") {
            int type_index = task.int_value;
            std::vector<std::string> types{ "pause", "resume", "cancel" };
            if (type_index < 0 || type_index > 2)
                type_index = 0;
            task.local_action_type = types[type_index];
        }
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::SetBox) {
        std::string script;
        if (task.event_type == EVT_SET_COLOR) {
            script = "SAVE_VARIABLE VARIABLE=color_slot" + std::to_string(task.slot_index) + " VALUE=\"" + std::to_string(task.filament_index) + "\"";
        } else if (task.event_type == EVTSET_FILAMENT_VENDOR) {
            script = "SAVE_VARIABLE VARIABLE=vendor_slot" + std::to_string(task.slot_index) + " VALUE=\"" + std::to_string(task.filament_index) + "\"";
        } else if (task.event_type == EVTSET_FILAMENT_TYPE) {
            script = "SAVE_VARIABLE VARIABLE=filament_slot" + std::to_string(task.slot_index) + " VALUE=\"" + std::to_string(task.filament_index) + "\"";
        } else if (task.event_type == EVTSET_FILAMENT_LOAD) {
            script = "E_LOAD slot=" + std::to_string(task.slot_index);
        } else if (task.event_type == EVTSET_FILAMENT_UNLOAD) {
            script = "E_UNLOAD slot=" + std::to_string(task.slot_index);
        //cj_3
        } else if (task.event_type == EVTSET_FILAMENT_EJECT) {
            script = "E_BOX slot=" + std::to_string(task.slot_index);
        } else {
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported set-box local event" };
        }


        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::RefreshRfid) {
        std::string script = "RFID_READ SLOT=slot" + std::to_string(task.slot_index);
        task.local_commands.push_back(LocalCommand{ false, "script", script, "" });
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::DeletePrinterFiles) {
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
            {EVTSET_AUTO_FILAMENT, "/ams/auto/filament/enable"}
        };

        auto it = cloud_event_to_path.find(task.event_type);
        if (it == cloud_event_to_path.end())
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported status-panel cloud event" };

        task.cloud_task_path = it->second;
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

        task.cloud_body = task.string_key.empty() ? "{}" : body_json.dump();
        return { true, PrinterTaskErrorCode::None, "" };
    }

    if (task.type == PrinterTaskType::SetBox) {
        static const std::unordered_map<wxEventType, std::string> cloud_box_event_to_path = {
            {EVT_SET_COLOR, "/set/filament/color"},
            {EVTSET_FILAMENT_TYPE, "/set/filament/type"},
            {EVTSET_FILAMENT_VENDOR, "/set/filament/vendor"},
            {EVTSET_FILAMENT_LOAD, "/set/filament/load"},
            {EVTSET_FILAMENT_UNLOAD, "/set/filament/unload"},
            //cj_3
            {EVTSET_FILAMENT_EJECT, "/set/filament/eject"}
        };

        auto it = cloud_box_event_to_path.find(task.event_type);
        if (it == cloud_box_event_to_path.end())
            return { false, PrinterTaskErrorCode::UnsupportedEvent, "unsupported set-box cloud event" };

        task.cloud_task_path = it->second;
        body_json["slotIndex"] = task.slot_index;
        //cj_3
        if (task.event_type == EVT_SET_COLOR || task.event_type == EVTSET_FILAMENT_TYPE || task.event_type == EVTSET_FILAMENT_VENDOR) {
            body_json["idx"] = task.filament_index;
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
        body_json["files"] = task.file_paths;
        task.cloud_body = body_json.dump();
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
            m_device_manager->sendCommand(task.device_id, cmd.script_name, cmd.script, cmd.method);
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
    if (task.device_id.empty() || task.cloud_task_path.empty())
        return { false, PrinterTaskErrorCode::InvalidArgument, "invalid cloud request arguments" };

    HttpData httpData;
    httpData.env = env;
    httpData.target = target;
    httpData.taskPath = task.cloud_task_path;
    httpData.body = task.cloud_body.empty() ? "{}" : task.cloud_body;

    bool isSucceed = false;
    MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
    if (!isSucceed) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " http error" << isSucceed << std::endl;
        return { false, PrinterTaskErrorCode::SendFailed, "http post task failed" };
    }

    return { true, PrinterTaskErrorCode::None, "" };
}
#endif

}} // namespace Slic3r::GUI
