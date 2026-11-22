#pragma once
#include <chrono>
#include <exception>
#include <functional>
#include <stdexcept>
#include <thread>

namespace aiws::gui{

struct BuildCancel:std::exception {
    const char* what() const noexcept override {
        return "Corpus build canceled";
    }
};

struct BuildControl{
    int delay_ms{0};
    bool fail_4_testing{false};
    std::function<bool()> is_cancelled;
};
//each thread has current build control
inline thread_local const BuildControl* current_build = nullptr;
//Used to install the current control settings for one rebuild call
class ScopedBuildControl{
    public: 
    explicit ScopedBuildControl(const BuildControl& control): previous_(current_build){
        current_build = &control;
    }
    ~ScopedBuildControl(){
        current_build = previous_;
    }
    ScopedBuildControl(const ScopedBuildControl&) = delete;
    ScopedBuildControl& operator=(const ScopedBuildControl&) = delete;

    private:
    const BuildControl* previous_;
};
inline void check_cancelled(){
    if(current_build && current_build->is_cancelled && current_build->is_cancelled()){
        throw BuildCancel{};
    }
} //used to check whether or not the current build should stop
inline void before_document(){
    check_cancelled();
    if(!current_build){
        return;
    }
    if(current_build->delay_ms > 0){ //sim the specified per-doc build delay
        std::this_thread::sleep_for(std::chrono::milliseconds(current_build->delay_ms));
    }
    check_cancelled();
    if(current_build->fail_4_testing){
        throw std::runtime_error("Injected corpus build failure");

    }
}


} //namespace aiws::gui
