#include "aiws/gui/build_worker.hpp"
#include <QMetaObject>
#include <QObject>
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiws::gui {
    //Constructing the worker and starting the single thread
    BuildWorker::BuildWorker(QObject* gui_rec, CompletionCall callback) 
    : gui_receiver_(gui_rec), comp_callback_(std::move(callback)){
        if(!gui_receiver_ || !comp_callback_){
            throw std::invalid_argument("Invalid worker callback");
        }
        worker_ = std::thread(&BuildWorker::worker_loop, this);
    }
    BuildWorker::~BuildWorker(){
        stop();
    }
    //Submitting new request without waiting for a build
    std::size_t BuildWorker::submit(BuildRequest request){
        std::size_t id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_){
                throw std::runtime_error("Worker is stopping");
            }
            ++gen_;
            id = gen_;
            pending_request_ = std::move(request);

        }
        cond_var_.notify_one();
        return id;
  }
  //Used to invalidate both running and pending work
  void BuildWorker::cancel(){
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++gen_;
    pending_request_.reset();
  }
  cond_var_.notify_one();
  }
  //Used to whether a request is still valid
  bool BuildWorker::is_current(std::size_t id){
    std::lock_guard<std::mutex> lock(mutex_);
    return (!stopping_) && (id == gen_);
  }
  void BuildWorker::stop(){
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        ++gen_;
        pending_request_.reset();
    }
    cond_var_.notify_one(); //wake worker even when no build currently running
    if(worker_.joinable()){
        worker_.join();
    }
  }
  void BuildWorker::worker_loop(){
    while(true){
        std::optional<BuildRequest> request;
        std::size_t id = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            //Sleeping until  request / shutdown arrives
            cond_var_.wait(lock, [this]{
                return stopping_ || pending_request_.has_value();
            });
            if(stopping_){
                return;
            }
            //Taking newest request
            request.emplace(std::move(*pending_request_));
            pending_request_.reset();
            id = gen_;
        }
        BuildResult result;
        result.revision = request->revision;
        result.gen = id;
        try{
            auto core = std::make_unique<ProcessingCore>();
            //checking cancellation between doc delays
            for(const auto& document : request->snap.documents()){
                (void) document;
                if(!is_current(id)){
                    break;
                }
                if(request->delay_ms > 0){
                    std::this_thread::sleep_for(std::chrono::milliseconds(request->delay_ms));

                }
                if(!is_current(id)){
                    break;
                }
                if(request->fail_4_testing){
                    throw std::runtime_error("Injected corpus build failure");
                }
            }
            if(!is_current(id)){
                continue;
            }
            core->rebuild(request->snap);
            if(!is_current(id)){
                continue;
            }
            result.core = std::move(core);
        } catch(const std::exception& error){
            result.error = error.what();
        } catch(...){
            result.error = "Unknown corpus build error";
        }
        if(!is_current(id)){ //discarding any result that was invalidated during the processing
            continue;
        }
        // Keeping the move only result in shared storage
        auto result_ptr = std::make_shared<BuildResult>(std::move(result));

        //Delivering completion on the GUI thread
        QMetaObject::invokeMethod(gui_receiver_, [callback = comp_callback_, result_ptr]() mutable {callback(std::move(*result_ptr));}, Qt::QueuedConnection);
    }
  }










} //namespace aiws::gui