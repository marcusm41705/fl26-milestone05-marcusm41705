#pragma once
#include "aiws/workspace.hpp"
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace aiws::gui {
struct BuildRequest { //The info needed to perform a corpus build
Workspace snap;
std::size_t revision;
int delay_ms;
bool fail_4_testing;
};

class BuildWorker{
    public:
    BuildWorker();
    ~BuildWorker();

    //Prevents the copying of a worker that owns thread
    BuildWorker(const BuildWorker&) = delete;
    BuildWorker& operator=(const BuildWorker&) = delete;
    void submit(BuildRequest request); //used to submit new build request
    void cancel(); //invalidate pending + running work
    void stop(); //Stop and join the worker thread.

    private:
    //this function is executed by the worker thread
    void worker_loop();
    std::mutex mutex_;
    std::condition_variable cond_var_;
    //latest request waiting for processing
    std::optional<BuildRequest> pending_request_;
    bool stopping_{false};
    std::size_t gen_{0}; //used to detect canceled / outdated requests
    std::thread worker_; //single persistent worker thread
};
} //namespace aiws::gui