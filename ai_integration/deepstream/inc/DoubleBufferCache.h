/*
 * @Description: Double Buffer Cache Implement.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-29 08:51:01
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-07-16 13:52:32
 */
#pragma once

#include <mutex>
#include <atomic>
#include <memory>
#include <list>

/** 
 * @brief Shared-buffer cache manager.
 */
template<typename T>
class DoubleBufCache {
public:
    /**
     * @brief: constructor
     * @Author: Ricardo Lu
     * @param[in] notify_func When a new buffer is fed, it triggers the function handle.
     * @return {*}
     */    
    DoubleBufCache(std::function<bool()> notify_func =
            std::function<bool()>{nullptr}, std::string debug_info = "") noexcept : 
            debug_info(debug_info), swap_ready(false) {
        this->notify_func = notify_func;
    }

    /**
     * @brief deconstructor
     * @Author: Ricardo Lu
     */
    ~DoubleBufCache() noexcept {
        if (!debug_info.empty() ) {
            printf("DoubleBufCache %s destroyed.", debug_info.c_str());
        }
    }

    /**
     * @brief Put the latest buffer into cache queue to be processed.
     * Giving up control of previous front buffer.
     * @Author: Ricardo Lu
     * @param[in] pending - The latest buffer.
     */
    void feed(std::shared_ptr<T> pending) {
        if (nullptr == pending.get()) {
            throw "ERROR: feed an empty buffer to DoubleBufCache";
        }

        swap_mtx.lock();
        front_sp = pending;
        swap_mtx.unlock();
        swap_ready = true;
        if (notify_func) {
            notify_func();
        }
        return;
    }

    /**
     * @brief Get the front buffer.
     * @Author: Ricardo Lu
     * @return Front buffer.
     * */
    std::shared_ptr<T> front()  noexcept {
        return front_sp;
    }

    /**
     * @brief Fetch the shared back buffer.
     * @Author: Ricardo Lu
     * @return Back buffer.
     */
    std::shared_ptr<T> fetch()  noexcept {
        if (swap_ready) {
            swap_mtx.lock();
            back_sp = front_sp;
            swap_mtx.unlock();
            swap_ready = false;
        }
        return back_sp;
    }

private:
    //! Notification function will be called, if a new buffer fed.
    std::function<bool()> notify_func;
    //! The buffer cache can be swapped if the flag is equal to true.
    std::atomic<bool> swap_ready;
    //! Swapping mutex lock for thread safety.
    std::mutex swap_mtx;
    //! Front buffer for previous results saving.
    std::shared_ptr<T> front_sp;
    //! Back buffer to be fetched.
    std::shared_ptr<T> back_sp;
public:
    //! Indicate the name of an instantiated object for debug.
    std::string debug_info;
};