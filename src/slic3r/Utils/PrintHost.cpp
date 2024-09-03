#include "PrintHost.hpp"

#include <vector>
#include <thread>
#include <exception>
#include <boost/optional.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <wx/string.h>
#include <wx/app.h>
#include <wx/arrstr.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Channel.hpp"
#include "OctoPrint.hpp"
#include "Duet.hpp"
#include "FlashAir.hpp"
#include "AstroBox.hpp"
#include "Repetier.hpp"
#include "MKS.hpp"
#include "../GUI/PrintHostDialogs.hpp"

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;
using boost::optional;
using Slic3r::GUI::PrintHostQueueDialog;

namespace Slic3r {


PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    PrinterTechnology tech = ptFFF;

    {
        const auto opt = config->option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        if (opt != nullptr) {
            tech = opt->value;
        }
    }

    if (tech == ptFFF) {
        const auto opt = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
        const auto host_type = opt != nullptr ? opt->value : htOctoPrint;

        switch (host_type) {
            case htOctoPrint: return new OctoPrint(config, true);
            case htDuet:      return new Duet(config);
            case htFlashAir:  return new FlashAir(config);
            case htAstroBox:  return new AstroBox(config);
            case htRepetier:  return new Repetier(config);
            case htPrusaLink: return new PrusaLink(config);
            case htMKS:       return new MKS(config);
            default:          return nullptr;
        }
    } else {
        return new SL1Host(config);
    }
}
PrintHost *PrintHost::get_print_host_url(std::string url, std::string local_ip) { return new OctoPrint(url, local_ip); }
wxString PrintHost::format_error(const std::string &body, const std::string &error, unsigned status) const
{
    if (status != 0) {
        auto wxbody = wxString::FromUTF8(body.data());
        return wxString::Format("HTTP %u: %s", status, wxbody);
    } else {
        return wxString::FromUTF8(error.data());
    }
}


//B52
std::string PrintHost::make_url(const std::string &path, const std::string& m_host) const
{

    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/') {
            return (boost::format("%1%%2%") % m_host % path).str();
        } else {
            return (boost::format("%1%/%2%") % m_host % path).str();
        }
    } else {
        return (boost::format("http://%1%/%2%") % m_host % path).str();
    }
}

//B45
std::string PrintHost::get_status(wxString &msg, const wxString &buttonText, const wxString &ip) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const std::string name = buttonText.ToStdString();

    bool res = true;
    std::string print_state = "standby";
    std::string m_host = ip.ToStdString();
    auto url = make_url("printer/objects/query?print_stats=state", m_host);

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    // set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
                                            body;
            print_state = "offline";
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got print_stats: %2%") % name % body;

            try {
                // All successful HTTP requests will return a json encoded object in the form of :
                // {result: <response data>}
                std::stringstream ss(body);
                pt::ptree         ptree;
                pt::read_json(ss, ptree);
                if (ptree.front().first != "result") {
                    msg = "Could not parse server response";
                    print_state = "offline";
                    return;
                }
                if (!ptree.front().second.get_optional<std::string>("status")) {
                    msg = "Could not parse server response";
                    print_state = "offline";
                    return;
                }
                print_state = ptree.get<std::string>("result.status.print_stats.state");
                BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % print_state;
                ;
            } catch (const std::exception &) {
                print_state = "offline";
                msg         = "Could not parse server response";
            }
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = wxString::FromUTF8(address);
        })
#endif // _WIN32
        .perform_sync();

    return print_state;
}

float PrintHost::get_progress(wxString &msg, const wxString &buttonText, const wxString &ip) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const std::string name = buttonText.ToStdString();

    bool res = true;
    auto url = make_url("printer/objects/query?display_status=progress", ip.ToStdString());
    float  process = 0;
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    // set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
                                            body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got display_status: %2%") % name % body;

            try {
                // All successful HTTP requests will return a json encoded object in the form of :
                // {result: <response data>}
                std::stringstream ss(body);
                pt::ptree         ptree;
                pt::read_json(ss, ptree);
                if (ptree.front().first != "result") {
                    msg = "Could not parse server response";
                    res = false;
                    return;
                }
                if (!ptree.front().second.get_optional<std::string>("status")) {
                    msg = "Could not parse server response";
                    res = false;
                    return;
                }
                process = std::stof(ptree.get<std::string>("result.status.display_status.progress"));
                BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % process;
            } catch (const std::exception &) {
                res = false;
                msg = "Could not parse server response";
            }
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = wxString::FromUTF8(address);
        })
#endif // _WIN32
        .perform_sync();

    return process;
}


struct PrintHostJobQueue::priv
{
    // XXX: comment on how bg thread works

    PrintHostJobQueue *q;

    Channel<PrintHostJob> channel_jobs;
    Channel<size_t> channel_cancels;
    size_t job_id = 0;
    int prev_progress = -1;
    fs::path source_to_remove;

    std::thread bg_thread;
    bool bg_exit = false;

    PrintHostQueueDialog *queue_dialog;

    priv(PrintHostJobQueue *q) : q(q) {}

    void emit_progress(int progress);
    void emit_error(wxString error);
    void emit_cancel(size_t id);
    void start_bg_thread();
    void stop_bg_thread();
    void bg_thread_main();
    void progress_fn(Http::Progress progress, bool &cancel);
    void remove_source(const fs::path &path);
    void remove_source();
    void perform_job(PrintHostJob the_job);
    bool cancel_fn();
    void emit_waittime(int waittime, size_t id);
    std::vector<PrintHostJob> vec_jobs;
    std::vector<size_t>       vec_jobs_id;
};

PrintHostJobQueue::PrintHostJobQueue(PrintHostQueueDialog *queue_dialog)
    : p(new priv(this))
{
    p->queue_dialog = queue_dialog;
}

PrintHostJobQueue::~PrintHostJobQueue()
{
    if (p) { p->stop_bg_thread(); }
}

void PrintHostJobQueue::priv::emit_progress(int progress)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_PROGRESS, queue_dialog->GetId(), job_id, progress);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_error(wxString error)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_ERROR, queue_dialog->GetId(), job_id, std::move(error));
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_cancel(size_t id)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_CANCEL, queue_dialog->GetId(), id);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::start_bg_thread()
{
    if (bg_thread.joinable()) { return; }

    std::shared_ptr<priv> p2 = q->p;
    bg_thread = std::thread([p2]() {
        p2->bg_thread_main();
    });
}

void PrintHostJobQueue::priv::stop_bg_thread()
{
    if (bg_thread.joinable()) {
        bg_exit = true;
        channel_jobs.push(PrintHostJob()); // Push an empty job to wake up bg_thread in case it's sleeping
        bg_thread.detach();                // Let the background thread go, it should exit on its own
    }
}

// 10
void PrintHostJobQueue::priv::emit_waittime(int waittime, size_t id)
{
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_WAIT, queue_dialog->GetId(), id, waittime, 0);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::bg_thread_main()
{
    // bg thread entry point

    try {
        // Pick up jobs from the job channel:
        while (! bg_exit) {
            auto job = channel_jobs.pop();   // Sleeps in a cond var if there are no jobs
            if (!vec_jobs.empty())
                vec_jobs.erase(vec_jobs.begin());
            if (!vec_jobs_id.empty())
                vec_jobs_id.erase(vec_jobs_id.begin());
            if (job.empty()) {
                // This happens when the thread is being stopped
                break;
            }

            source_to_remove = job.upload_data.source_path;

            BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue/bg_thread: Received job: [%1%]: `%2%` -> `%3%`, cancelled: %4%")
                % job_id
                % job.upload_data.upload_path
                % job.printhost->get_host()
                % job.cancelled;

            if (! job.cancelled) {
                perform_job(std::move(job));
            }

            remove_source();
            job_id++;
        }
    } catch (const std::exception &e) {
        emit_error(e.what());
    }

    // Cleanup leftover files, if any
    remove_source();
    auto jobs = channel_jobs.lock_rw();
    for (const PrintHostJob &job : *jobs) {
        remove_source(job.upload_data.source_path);
    }
}

void PrintHostJobQueue::priv::progress_fn(Http::Progress progress, bool &cancel)
{
    if (cancel) {
        // When cancel is true from the start, Http indicates request has been cancelled
        emit_cancel(job_id);
        return;
    }

    if (bg_exit) {
        cancel = true;
        return;
    }

    if (channel_cancels.size_hint() > 0) {
        // Lock both queues
        auto cancels = channel_cancels.lock_rw();
        auto jobs = channel_jobs.lock_rw();

        for (size_t cancel_id : *cancels) {
            if (cancel_id == job_id) {
                cancel = true;
            } else if (cancel_id > job_id) {
                const size_t idx = cancel_id - job_id - 1;
                if (idx < jobs->size()) {
                    jobs->at(idx).cancelled = true;
                    BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue: Job id %1% cancelled") % cancel_id;
                    emit_cancel(cancel_id);
                }
            }
        }

        cancels->clear();
    }

    if (! cancel) {
        int gui_progress = progress.ultotal > 0 ? 100*progress.ulnow / progress.ultotal : 0;
        if (gui_progress != prev_progress) {
            emit_progress(gui_progress);
            prev_progress = gui_progress;
        }
    }
        for (int i = 0; i < vec_jobs.size(); i++) {
            std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - vec_jobs[i].create_time);
            emit_waittime((vec_jobs[i].sendinginterval - (diff.count() / 1000)) > 0 ?
                                (vec_jobs[i].sendinginterval - (diff.count() / 1000)) :
                                0,
                            vec_jobs_id[i]);
        }
}

void PrintHostJobQueue::priv::remove_source(const fs::path &path)
{
    if (! path.empty()) {
        boost::system::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << boost::format("PrintHostJobQueue: Error removing file `%1%`: %2%") % path % ec;
        }
    }
}

void PrintHostJobQueue::priv::remove_source()
{
    remove_source(source_to_remove);
    source_to_remove.clear();
}

bool PrintHostJobQueue::priv::cancel_fn()
{
    bool cancel = false;
    if (channel_cancels.size_hint() > 0) {
        // Lock both queues
        auto cancels = channel_cancels.lock_rw();
        auto jobs    = channel_jobs.lock_rw();

        for (size_t cancel_id : *cancels) {
            if (cancel_id == job_id) {
                cancel = true;
            } else if (cancel_id > job_id) {
                const size_t idx = cancel_id - job_id - 1;
                if (idx < jobs->size()) {
                    jobs->at(idx).cancelled = true;
                    BOOST_LOG_TRIVIAL(error) << boost::format("PrintHostJobQueue: Job id %1% cancelled") % cancel_id;
                    emit_cancel(cancel_id);
                }
            }
        }
        if (!cancel) {
            cancels->clear();
    }
}
return cancel;
}

void PrintHostJobQueue::priv::perform_job(PrintHostJob the_job)
{
    while(true){
    std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
    auto                                  diff      = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - the_job.create_time);

    emit_waittime((the_job.sendinginterval - (diff.count() / 1000)) > 0 ? (the_job.sendinginterval - (diff.count() / 1000)) : 0, job_id);

    for (int i = 0; i < vec_jobs.size(); i++) {
        emit_waittime((vec_jobs[i].sendinginterval - (diff.count() / 1000)) > 0 ?
                            (vec_jobs[i].sendinginterval - (diff.count() / 1000)) :
                            0,
                        vec_jobs_id[i]);
    }
    

    if (diff.count() > (the_job.sendinginterval)* 1000) {
        BOOST_LOG_TRIVIAL(debug) << "task_manager: diff count = " << diff.count() << " milliseconds";
        break;
    }
    if (this->cancel_fn())
        break;
    boost::this_thread::sleep(boost::posix_time::seconds(1));
    }
    emit_progress(0);   // Indicate the upload is starting

    bool success = the_job.printhost->upload(std::move(the_job.upload_data),
        [this](Http::Progress progress, bool &cancel) { this->progress_fn(std::move(progress), cancel); },
        [this](wxString error) {
            emit_error(std::move(error));
        }
    );

    if (success) {
        emit_progress(100);
    }
}

void PrintHostJobQueue::enqueue(PrintHostJob job)
{
    p->start_bg_thread();
    p->queue_dialog->append_job(job);
    p->channel_jobs.push(std::move(job));
    p->vec_jobs.push_back(std::move(job));
    p->vec_jobs_id.push_back(p->job_id + p->vec_jobs.size());
}



void PrintHostJobQueue::cancel(size_t id)
{
    p->channel_cancels.push(id);
}

}
