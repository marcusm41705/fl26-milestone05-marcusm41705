#pragma once
#include "aiws/workspace.hpp"
#include "aiws/processing_core.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class QObject;

namespace aiws::gui {
struct BuildRequest { //The info needed to perform a corpus build
Workspace snap;
std::size_t revision{0};
bool fail_4_testing{false};
int delay_ms{0};

};
struct BuildResult{ //the result gets transferred back to the GUI thread
    std::size_t revision{0};
    std::size_t gen{0};
    std::unique_ptr<ProcessingCore> core;
    std::string error;
};
class BuildWorker{
    public:
    using CompletionCall = std::function<void(BuildResult)>;

    BuildWorker(QObject* gui_rec, CompletionCall callback);
    ~BuildWorker();
    //Prevents the copying of a worker that owns thread
    BuildWorker(const BuildWorker&) = delete;
    BuildWorker& operator=(const BuildWorker&) = delete;
    std::size_t submit(BuildRequest request); //used to submit new build request
    void cancel(); //invalidate pending + running work
    void stop(); //Stop and join the worker thread.
    bool is_current(std::size_t generation);
    private:
    //this function is executed by the worker thread
    void worker_loop();
    std::mutex mutex_; //protects data shared between GUI and worker
    std::condition_variable cond_var_; //allow worker to wait without polling
    std::optional<BuildRequest> pending_request_; //latest request waiting for processing
    bool stopping_{false};
    std::size_t gen_{0}; //used to detect canceled / outdated requests
    QObject* gui_receiver_{nullptr}; //gui receiver
    CompletionCall comp_callback_; //completion notification
    std::thread worker_; //single persistent worker thread / responsible for corpus building
};
} //namespace aiws::gui