#include "aiws/gui/build_worker.hpp"
#include "aiws/gui/build_control.hpp"
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
        //R2: Only keep the newest pending request,
        //advancing the generation also would invalidate the older build
        // which allows the worker to abandon any outdated processing elements
        std::size_t id;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_ ){
                throw std::runtime_error("Worker is stopping");
            }
            ++gen_;

            id =  gen_;
            pending_request_ = std::move(request);

        }
        cond_var_.notify_one();
        return id;
  }

  void BuildWorker::worker_loop(){
    while(true){
        std::optional<BuildRequest> request;
        std::size_t id = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            phase_ = WorkerPhase::Waiting;
            progress_cv_.notify_all();
            //R1/R2: The single worker will wait for a new build request
            //or shutdown signal w/o polling
            cond_var_.wait(lock, [this]{
                return stopping_ || pending_request_.has_value();
            });
            if(stopping_){
                phase_ = WorkerPhase::Stopped;
                progress_cv_.notify_all();
                return;
            }
            //Taking ownership of the newest pending request while holding the mutex so the GUI 
            //can't modify it simulataneously
            request.emplace(std::move(*pending_request_));
            pending_request_.reset();
            id = gen_;
            phase_ = WorkerPhase::Starting; //worker has received a request but has not started processing
        }
        BuildResult result;
        result.revision = request->revision;
        result.gen = id;
        try{
            auto core = std::make_unique<ProcessingCore>();
            //providing the delay and cancellation settings for this build
            //R3: Installing the cancel controls for the current build, a generation mismatch would mean 
            //reset or workspace change which invalidates the running request
          const aiws::gui::BuildControl control{
            request->delay_ms, request->fail_4_testing, [this, id](){
               std::lock_guard<std::mutex> lock(mutex_);
               //Notify tests when processing begin
               if(phase_ != WorkerPhase::Running){
                phase_ = WorkerPhase::Running;
                progress_cv_.notify_all();
               }
               #ifdef AIWS_FAULT_NO_CANCEL
               //Ignore canceled builds
               return stopping_;
               #else 
               //Normal behavior: cancelling outdated builds
               return stopping_ || id != gen_;
               #endif
            }

          };
            //installing the controls on the worker thread
            aiws::gui::ScopedBuildControl scope(control);
            core->rebuild(request->snap); //The real build will now check the cancellation per doc
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
  void BuildWorker::stop(){
    {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    ++gen_;
    pending_request_.reset();
     }
     #ifndef AIWS_FAULT_SILENT_SHUTDOWN
     // wakes the idle worker normally so it can exit
     //Worker is awoken even if waiting for work
     //R4: Wake worker if idle, so it sees the shutdown flag, then terminate it before join() returns
     cond_var_.notify_one();
     #endif
     if(worker_.joinable()){ //joining the worker before destruction
        worker_.join();
     }
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
  

  bool BuildWorker::wait_until_running_4_test(int timeout_ms){
    std::unique_lock<std::mutex> lock(mutex_);
    return progress_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this](){
        return phase_ ==  WorkerPhase::Running;

    });
  }
  bool BuildWorker::wait_until_idle_4_test(int timeout_ms){
    std::unique_lock<std::mutex> lock(mutex_);
    return progress_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this](){
        return phase_ == WorkerPhase::Waiting;

    });
  }









} //namespace aiws::gui