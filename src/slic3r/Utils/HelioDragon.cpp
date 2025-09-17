#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Print.hpp"
#include <boost/log/trivial.hpp>
#include "../GUI/PartPlate.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/Event.hpp"
#include "../GUI/Plater.hpp"
#include "../GUI/NotificationManager.hpp"
#include "wx/app.h"
#include "cstdio"


namespace Slic3r {

std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_printers;
std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_materials;

void HelioQuery::request_support_machine(const std::string helio_api_url, const std::string helio_api_key, int page)
{
    std::string query_body = R"( {
            "query": "query GetPrinters($page: Int) { printers(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Printer  { id name alternativeNames { qidistudio } } } } }",
            "variables": {"page": %1%}
		} )";

    query_body = boost::str(boost::format(query_body) % page);

    std::string url_copy  = helio_api_url;
    std::string key_copy  = helio_api_key;
    int         page_copy = page;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json").header("Authorization", "Bearer " + helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, page_copy](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "request_support_machine" << body;
            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                std::vector<HelioQuery::SupportedData> supported_printers;
                if (parsed_obj.contains("data") && parsed_obj["data"].contains("printers")) {
                    auto materials = parsed_obj["data"]["printers"];
                    if (materials.contains("objects") && materials["objects"].is_array()) {
                        for (const auto &pobj : materials["objects"]) {
                            HelioQuery::SupportedData sp;
                            if (pobj.contains("id") && !pobj["id"].is_null()) { sp.id = pobj["id"].get<std::string>(); }
                            if (pobj.contains("name") && !pobj["id"].is_null()) { sp.name = pobj["name"].get<std::string>(); }

                            if (pobj.contains("alternativeNames") && pobj["alternativeNames"].is_object()) {
                                auto alternativeNames = pobj["alternativeNames"];

                                if (alternativeNames.contains("qidistudio") && !alternativeNames["qidistudio"].is_null()) {
                                    sp.native_name = alternativeNames["qidistudio"].get<std::string>();
                                }
                            }

                            supported_printers.push_back(sp);
                        }
                    }

                    HelioQuery::global_supported_printers.insert(HelioQuery::global_supported_printers.end(), supported_printers.begin(), supported_printers.end());

                    if (materials.contains("pageInfo") && materials["pageInfo"].contains("hasNextPage") && materials["pageInfo"]["hasNextPage"].get<bool>()) {
                        HelioQuery::request_support_machine(url_copy, key_copy, page_copy + 1);
                    }
                }
            } catch (...) {}
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            // BOOST_LOG_TRIVIAL(info) << (boost::format("error: %1%, message: %2%") % error % body).str()
        })
        .perform();
}

void HelioQuery::request_support_material(const std::string helio_api_url, const std::string helio_api_key, int page)
{
    std::string query_body = R"( {
			"query": "query GetMaterias($page: Int) { materials(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Material  { id name alternativeNames { qidistudio } } } } }",
            "variables": {"page": %1%}
		} )";

    query_body = boost::str(boost::format(query_body) % page);

    std::string url_copy  = helio_api_url;
    std::string key_copy  = helio_api_key;
    int         page_copy = page;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json").header("Authorization", "Bearer " + helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, page_copy](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "request_support_material" << body;
            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                std::vector<HelioQuery::SupportedData> supported_materials;

                if (parsed_obj.contains("data") && parsed_obj["data"].contains("materials")) {
                    auto materials = parsed_obj["data"]["materials"];
                    if (materials.contains("objects") && materials["objects"].is_array()) {
                        for (const auto &pobj : materials["objects"]) {
                            HelioQuery::SupportedData sp;
                            if (pobj.contains("id") && !pobj["id"].is_null()) { sp.id = pobj["id"].get<std::string>(); }
                            if (pobj.contains("name") && !pobj["id"].is_null()) { sp.name = pobj["name"].get<std::string>(); }
                            if (pobj.contains("alternativeNames") && pobj["alternativeNames"].is_object()) {
                                auto alternativeNames = pobj["alternativeNames"];

                                //qidi materials
                                if (alternativeNames.contains("qidistudio") && !alternativeNames["qidistudio"].is_null()) {
                                    sp.native_name = alternativeNames["qidistudio"].get<std::string>();
                                }
                                //third party materials
                                else {
                                    if (pobj.contains("name") && !pobj["id"].is_null()) { sp.native_name = pobj["name"].get<std::string>(); }
                                }
                            }
                            supported_materials.push_back(sp);
                        }
                    }

                    HelioQuery::global_supported_materials.insert(HelioQuery::global_supported_materials.end(), supported_materials.begin(), supported_materials.end());

                    if (materials.contains("pageInfo") && materials["pageInfo"].contains("hasNextPage") && materials["pageInfo"]["hasNextPage"].get<bool>()) {
                        HelioQuery::request_support_material(url_copy, key_copy, page_copy + 1);
                    }
                }
            } catch (...) {}
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            // BOOST_LOG_TRIVIAL(info) << (boost::format("error: %1%, message: %2%") % error % body).str()
        })
        .perform();
}

std::string HelioQuery::get_helio_api_url()
{
    std::string helio_api_url;
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_api_url = "https://api.helioam.cn/graphql";
    } else {
        helio_api_url = "https://api.helioadditive.com/graphql";
    }
    return helio_api_url;
}

std::string HelioQuery::get_helio_pat()
{
    std::string helio_pat;
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_pat = GUI::wxGetApp().app_config->get("helio_pat_china");
    } else {
        helio_pat = GUI::wxGetApp().app_config->get("helio_pat_other");
    }
    return helio_pat;
}

void HelioQuery::set_helio_pat(std::string pat)
{
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        GUI::wxGetApp().app_config->set("helio_pat_china", pat);
    } else {
        GUI::wxGetApp().app_config->set("helio_pat_other", pat);
    }
}

void HelioQuery::request_pat_token(std::function<void(std::string)> func)
{
    std::string url_copy = "";

    if (GUI::wxGetApp().app_config->get("region") == "China") {
        url_copy = "https://api.helioam.cn/rest/auth/anonymous_token/qidistudio";
    }
    else {
        url_copy = "https://api.helioadditive.com/rest/auth/anonymous_token/qidistudio";
    }

    auto http = Http::get(url_copy);
    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, func](std::string body, unsigned status) {
        //success
        if (status == 200) {
            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                if (parsed_obj.contains("pat") && parsed_obj["pat"].is_string()) {
                    func(parsed_obj["pat"].get<std::string>());
                }
                else {
                    func("error");
                }

            }
            catch (...) {}
        }
        else if (status == 429) {
            func("not_enough");
        }
            })
        .on_error([func](std::string body, std::string error, unsigned status) {
        if (status == 429) {
            func("not_enough");
        }
        else {
            func("error");
        }
        //BOOST_LOG_TRIVIAL(info) << (boost::format("request pat token error: %1%, message: %2%") % error % body).str());
            })
        .perform();
}


HelioQuery::PresignedURLResult HelioQuery::create_presigned_url(const std::string helio_api_url, const std::string helio_api_key)
{
    HelioQuery::PresignedURLResult res;
    std::string                    query_body = R"( {
			"query": "query getPresignedUrl($fileName: String!) { getPresignedUrl(fileName: $fileName) { mimeType url key } }",
			"variables": {
				"fileName": "test.gcode"
			}
		} )";

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Authorization", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&res](std::string body, unsigned status) {
            try{
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                res.status = status;
                if (parsed_obj.contains("error")) {
                    res.error = parsed_obj["error"];
                }
                else {
                    res.key = parsed_obj["data"]["getPresignedUrl"]["key"];
                    res.mimeType = parsed_obj["data"]["getPresignedUrl"]["mimeType"];
                    res.url = parsed_obj["data"]["getPresignedUrl"]["url"];
                }
            }
            catch (...){}
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = (boost::format("error: %1%, message: %2%") % error % body).str();
            res.status = status;
        })
        .perform_sync();

    return res;
};

HelioQuery::UploadFileResult HelioQuery::upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url)
{
    UploadFileResult res;

    Http                    http = Http::put(upload_url);
    boost::filesystem::path file_path(file_path_string);
    http.header("Content-Type", "application/octet-stream");

    http.set_put_body(file_path)
        .on_complete([&res](std::string body, unsigned status) {
            if (status == 200)
                res.success = true;
            else
                res.success = false;
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error   = (boost::format("status: %1%, error: %2%, %3%") % status % body % error).str();
        })
        .perform_sync();

    return res;
}

HelioQuery::CreateGCodeResult HelioQuery::create_gcode(const std::string key,
                                                       const std::string helio_api_url,
                                                       const std::string helio_api_key,
                                                       const std::string printer_id,
                                                       const std::string filament_id)
{
    HelioQuery::CreateGCodeResult res;
    std::string                   query_body_template = R"( {
			"query": "mutation CreateGcode($input: CreateGcodeInput!) { createGcode(input: $input) {  errors gcode { id name sizeKb } } }",
			"variables": {
			  "input": {
				"name": "%1%",
				"printerId": "%2%",
				"materialId": "%3%",
				"gcodeKey": "%4%",
				"isSingleShell": true
			  }
			}
		  } )";

    std::vector<std::string> key_split;
    boost::split(key_split, key, boost::is_any_of("/"));

    std::string gcode_name = key_split.back();

    std::string query_body = (boost::format(query_body_template) % gcode_name % printer_id % filament_id % key).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Authorization", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&res](std::string body, unsigned status) {

            try{
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                res.status = status;
                if (parsed_obj.contains("errors")) {
                    res.error = parsed_obj["errors"].dump();
                    res.success = false;
                }
                else {

                    if (!parsed_obj["data"]["createGcode"]["gcode"].is_null()) {
                        res.success = true;
                        res.id = parsed_obj["data"]["createGcode"]["gcode"]["id"];
                        res.name = parsed_obj["data"]["createGcode"]["gcode"]["name"];
                    }
                    else {
                        res.success = false;
                        res.error = "";
                        for (const auto& err : parsed_obj["data"]["createGcode"]["criticalErrors"]) {
                            std::string error_msg = err.get<std::string>();
                            res.error_flags.push_back(error_msg);

                            res.error += " ";
                            res.error += error_msg;
                        }

                        for (const auto& err : parsed_obj["data"]["createGcode"]["criticalErrors"]) {
                            std::string error_msg = err.get<std::string>();
                            res.error_flags.push_back(error_msg);

                            res.error += " ";
                            res.error += error_msg;
                        }
                    }

                    for (const auto& err : parsed_obj["data"]["createGcode"]["warnings"]) {
                        std::string error_msg = err.get<std::string>();
                        res.warning_flags.push_back(error_msg);
                    }
                }
            }
            catch (...){}
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

#include <string>
#include <vector>
#include <boost/format.hpp>

std::string HelioQuery::generate_graphql_query(const std::string &gcode_id, float temperatureStabilizationHeight, float airTemperatureAboveBuildPlate, float stabilizedAirTemperature)
{
    std::string name = generateTimestampedString();

    std::string base_query = R"( {
        "query": "mutation CreateSimulation($input: CreateSimulationInput!) { createSimulation(input: $input) { id name progress status gcode { id name } printer { id name } material { id name } reportJsonUrl thermalIndexGcodeUrl estimatedSimulationDurationSeconds insertedAt updatedAt } }",
        "variables": {
            "input": {
                "name": "%1%",
                "gcodeId": "%2%",
                "simulationSettings": {
    )";

    std::vector<std::string> settings_fields;

    if (temperatureStabilizationHeight != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "temperatureStabilizationHeight": %1%)") % temperatureStabilizationHeight));
    }

    if (airTemperatureAboveBuildPlate != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "airTemperatureAboveBuildPlate": %1%)") % airTemperatureAboveBuildPlate));
    }

    if (stabilizedAirTemperature != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "stabilizedAirTemperature": %1%)") % stabilizedAirTemperature));
    }

    std::string settings_block;
    if (!settings_fields.empty()) { settings_block = boost::join(settings_fields, ",\n"); }

    std::string full_query = base_query + settings_block + R"(
                }
            }
        }
    } )";

    boost::format formatter(full_query);
    formatter % name % gcode_id;

    return formatter.str();
}

HelioQuery::CreateSimulationResult HelioQuery::create_simulation(const std::string helio_api_url,
                                                                 const std::string helio_api_key,
                                                                 const std::string gcode_id,
                                                                 const float       initial_room_airtemp,
                                                                 const float       layer_threshold,
                                                                 const float       object_proximity_airtemp)
{
    HelioQuery::CreateSimulationResult res;

    const float initial_room_temp_kelvin        = initial_room_airtemp == -1 ? -1 : initial_room_airtemp + 273.15;
    const float object_proximity_airtemp_kelvin = object_proximity_airtemp == -1 ? -1 : object_proximity_airtemp + 273.15;
    const float layer_threshold_meters          = layer_threshold / 1000;



    std::string query_body = generate_graphql_query(gcode_id,
                                                    layer_threshold_meters,
                                                    initial_room_temp_kelvin,
                                                    object_proximity_airtemp_kelvin
    );

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Authorization", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&res](std::string body, unsigned status) {
            
            try{
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                res.status = status;
                if (parsed_obj.contains("errors")) {
                    res.error = parsed_obj["errors"].dump();
                    res.success = false;
                }
                else {
                    res.success = true;
                    res.id = parsed_obj["data"]["createSimulation"]["id"];
                    res.name = parsed_obj["data"]["createSimulation"]["name"];
                }
            }
            catch (...){}
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

HelioQuery::CheckSimulationProgressResult HelioQuery::check_simulation_progress(const std::string helio_api_url,
                                                                                const std::string helio_api_key,
                                                                                const std::string simulation_id)
{
    HelioQuery::CheckSimulationProgressResult res;
    std::string                               query_body_template = R"( {
							"query": "query Simulation($id: ID!) { simulation(id: $id) { id name progress status thermalIndexGcodeUrl } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % simulation_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json").header("Authorization", helio_api_key).set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&res](std::string body, unsigned status) {
            try{
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                res.status = status;
                if (parsed_obj.contains("errors")) {
                    res.error = parsed_obj["errors"].dump();
                }
                else {
                    res.id = parsed_obj["data"]["simulation"]["id"];
                    res.name = parsed_obj["data"]["simulation"]["name"];
                    res.progress = parsed_obj["data"]["simulation"]["progress"];
                    res.is_finished = parsed_obj["data"]["simulation"]["status"] == "FINISHED";
                    if (res.is_finished)
                        res.url = parsed_obj["data"]["simulation"]["thermalIndexGcodeUrl"];
                }
            }
            catch (...){}
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

void HelioBackgroundProcess::helio_thread_start(std::mutex&                                slicing_mutex,
                                                std::condition_variable&                   slicing_condition,
                                                BackgroundSlicingProcess::State&           slicing_state,
                                                std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    m_thread = create_thread([this, &slicing_mutex, &slicing_condition, &slicing_state, &notification_manager] {
        this->helio_threaded_process_start(slicing_mutex, slicing_condition, slicing_state, notification_manager);
    });
}

void HelioBackgroundProcess::helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                                          std::condition_variable&                   slicing_condition,
                                                          BackgroundSlicingProcess::State&           slicing_state,
                                                          std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    set_state(STATE_RUNNING);

    std::unique_lock<std::mutex> slicing_lck(slicing_mutex);
    slicing_condition.wait(slicing_lck, [this, &slicing_state]() {
        return slicing_state == BackgroundSlicingProcess::STATE_FINISHED || slicing_state == BackgroundSlicingProcess::STATE_CANCELED ||
               slicing_state == BackgroundSlicingProcess::STATE_IDLE;
    });
    slicing_lck.unlock();

    if ((slicing_state == BackgroundSlicingProcess::STATE_FINISHED || slicing_state == BackgroundSlicingProcess::STATE_IDLE) &&
        !was_canceled()) {
        wxPostEvent(GUI::wxGetApp().plater(), GUI::SimpleEvent(GUI::EVT_HELIO_PROCESSING_STARTED));

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(0.0, _u8L("Helio: Process Started"));
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        BOOST_LOG_TRIVIAL(debug) << boost::format("url: %1%, key: %2%") % helio_api_url % helio_api_key;

        /*check api url*/
        if (helio_api_url.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Helio API endpoint is empty, please check the configuration."));
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        /*check Personal assecc token */
        if (helio_origin_key.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Personal assecc token is empty, please fill in the correct token."));
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        HelioQuery::PresignedURLResult create_presigned_url_res = HelioQuery::create_presigned_url(helio_api_url, helio_api_key);

        if (create_presigned_url_res.error.empty() && create_presigned_url_res.status == 200 && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(5, _u8L("Helio: Presigned URL Created"));
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            HelioQuery::UploadFileResult upload_file_res = HelioQuery::upload_file_to_presigned_url(m_gcode_result->filename,
                                                                                                    create_presigned_url_res.url);

            if (upload_file_res.success && !was_canceled()) {
                status = Slic3r::PrintBase::SlicingStatus(10, _u8L("Helio: file succesfully uploaded"));
                evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                wxQueueEvent(GUI::wxGetApp().plater(), evt);

                HelioQuery::CreateGCodeResult create_gcode_res = HelioQuery::create_gcode(create_presigned_url_res.key, helio_api_url,
                                                                                          helio_api_key, printer_id, filament_id);

                create_simulation_step(create_gcode_res, notification_manager);

            } else {
                set_state(STATE_CANCELED);

                Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                     _u8L("Helio: file upload failed"));
                wxQueueEvent(GUI::wxGetApp().plater(), evt);
            }
        } else {
            std::string presigned_url_message = (boost::format("error: %1%") % create_presigned_url_res.error).str();

            if (create_presigned_url_res.status == 401) {
                presigned_url_message += "\n ";
                presigned_url_message += _u8L("Please make sure you have the corrent API key set in preferences.");
            }

            set_state(STATE_CANCELED);

            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                 presigned_url_message);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
    } else {
        set_state(STATE_CANCELED);
    }
}

void HelioBackgroundProcess::create_simulation_step(HelioQuery::CreateGCodeResult              create_gcode_res,
                                                    std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    if (create_gcode_res.success && !was_canceled()) {
        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(15, _u8L("Helio: GCode created successfully"));
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        auto              print_config             = GUI::wxGetApp().preset_bundle->full_config();
        const std::string gcode_id                 = create_gcode_res.id;

        const float       chamber_temp             = simulation_input_data.chamber_temp; //User input1
        const float       layer_threshold          = 20; //Default values from Helio

        std::string bed_temp_key = Slic3r::get_bed_temp_1st_layer_key((Slic3r::BedType)(print_config.option("curr_bed_type")->getInt()));

        const float bed_temp             = print_config.option<ConfigOptionInts>(bed_temp_key)->get_at(0);
        float initial_room_airtemp = -1;
        if (chamber_temp > 0.0f) {
            initial_room_airtemp = (chamber_temp + bed_temp) / 2;
        }

        HelioQuery::CreateSimulationResult create_simulation_res = HelioQuery::create_simulation(helio_api_url, helio_api_key, gcode_id,
                                                                                                 initial_room_airtemp, layer_threshold,
                                                                                                 chamber_temp);

        if (create_simulation_res.success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(20, _u8L("Helio: simulation successfully created"));
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int times_tried            = 0;
            int max_unsuccessful_tries = 5;
            int times_queried          = 0;

            while (!was_canceled()) {
                HelioQuery::CheckSimulationProgressResult check_simulation_progress_res =
                    HelioQuery::check_simulation_progress(helio_api_url, helio_api_key, create_simulation_res.id);

                if (check_simulation_progress_res.status == 200) {
                    times_tried = 0;
                    if (check_simulation_progress_res.error.empty()) {
                        std::string trailing_dots = "";

                        for (int i = 0; i < (times_queried % 3); i++) {
                            trailing_dots += "....";
                        }

                        status = Slic3r::PrintBase::SlicingStatus(35 + (80 - 35) * check_simulation_progress_res.progress,
                                                                  _u8L("Helio: simulation working") + trailing_dots);
                        evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        if (check_simulation_progress_res.is_finished) {
                            // notification_manager->push_notification((boost::format("Helio: Simulation finished.")).str());
                            std::string simulated_gcode_path = HelioBackgroundProcess::create_path_for_simulated_gcode(
                                m_gcode_result->filename);

                            HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_simulation_progress_res.url,
                                                                                           simulated_gcode_path, m_gcode_result->filename,
                                                                                           notification_manager);
                            break;
                        }
                    } else {
                        set_state(STATE_CANCELED);

                        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "",
                                                                                             false, _u8L("Helio: simulation failed"));
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        break;
                    }
                } else {
                    times_tried++;

                    status = Slic3r::PrintBase::SlicingStatus(35, (boost::format("Helio: Simulation check failed, %1% tries left") %
                                                                   (max_unsuccessful_tries - times_tried))
                                                                      .str());
                    evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);

                    if (times_tried >= max_unsuccessful_tries)
                        break;
                }

                times_queried++;
                boost::this_thread ::sleep_for(boost::chrono::seconds(3));
            }

        } else {
            set_state(STATE_CANCELED);

            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                 (boost::format("Helio: Failed to create Simulation\n%1%") % create_simulation_res.error).str());
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    } else {
        set_state(STATE_CANCELED);

        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                             (boost::format("Helio: Failed to create GCode\n%1%") % create_gcode_res.error).str());
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(std::string                                file_download_url,
                                                                    std::string                                simulated_gcode_path,
                                                                    std::string                                tmp_path,
                                                                    std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    auto        http            = Http::get(file_download_url);
    unsigned    response_status = 0;
    std::string downloaded_gcode;
    std::string response_error;

    int number_of_attempts                  = 0;
    int max_attempts                        = 7;
    int number_of_seconds_till_next_attempt = 0;

    while (response_status != 200 && !was_canceled()) {
        if (number_of_seconds_till_next_attempt <= 0) {
            http.on_complete([&downloaded_gcode, &response_error, &response_status](std::string body, unsigned status) {
                    response_status = status;
                    if (status == 200) {
                        downloaded_gcode = body;
                    } else {
                        response_error = (boost::format("status: %1%, error: %2%") % status % body).str();
                    }
                })
                .on_error([&response_error, &response_status](std::string body, std::string error, unsigned status) {
                    response_status = status;
                    response_error  = (boost::format("status: %1%, error: %2%") % status % body).str();
                })
                .perform_sync();

            if (response_status != 200) {
                number_of_attempts++;
                Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(
                    80, (boost::format("Helio: Could not download file. Attempts left %1%") % (max_attempts - number_of_attempts)).str());
                Slic3r::SlicingStatusEvent* evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                wxQueueEvent(GUI::wxGetApp().plater(), evt);
                number_of_seconds_till_next_attempt = number_of_attempts * 5;
            }

            if (response_status == 200) {
                response_error = "";
                break;
            }

            else if (number_of_attempts >= max_attempts) {
                response_error = "Max attempts reached but file was not found";
                break;
            }

        } else {
            Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(80,
                                                                                       (boost::format("Helio: Next attemp in %1% seconds") %
                                                                                        number_of_seconds_till_next_attempt)
                                                                                           .str());
            Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
            number_of_seconds_till_next_attempt--;
    }

    if (response_error.empty() && !was_canceled()) {
        FILE* file = fopen(simulated_gcode_path.c_str(), "wb");
        fwrite(downloaded_gcode.c_str(), 1, downloaded_gcode.size(), file);
        fclose(file);

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, _u8L("Helio: GCode downloaded successfully"));
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
        HelioBackgroundProcess::load_simulation_to_viwer(simulated_gcode_path, tmp_path);
    } else {
        set_state(STATE_CANCELED);

        Slic3r::HelioCompletionEvent* evt =
            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                             (boost::format("Helio: GCode download failed: %1%") % response_error).str());
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::load_simulation_to_viwer(std::string simulated_file_path, std::string tmp_path)
{
    const Vec3d origin = GUI::wxGetApp().plater()->get_partplate_list().get_current_plate_origin();
    m_gcode_processor.set_xy_offset(origin(0), origin(1));
    m_gcode_processor.process_file(simulated_file_path);
    auto res       = &m_gcode_processor.result();
    m_gcode_result = res;

    set_state(STATE_FINISHED);
    Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, simulated_file_path,
                                                                         tmp_path, true);
    wxQueueEvent(GUI::wxGetApp().plater(), evt);
}

void HelioBackgroundProcess::set_helio_api_key(std::string api_key) { helio_api_key = api_key; }
void HelioBackgroundProcess::set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result) { m_gcode_result = gcode_result; }

} // namespace Slic3r
