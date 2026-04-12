#pragma once
#include <boost/asio.hpp>
#include <mutex>

// CoEvent is a synchronization primitive that allows multiple coroutines to wait for an event and be notified when the event occurs.
template<class T>
class CoEvent {
private:
    std::mutex lock;
    std::vector<std::function<void(T)>> handlers;
    boost::asio::io_context& ioc;
public:
    CoEvent(boost::asio::io_context& ioc) : ioc(ioc) {}
    CoEvent(const CoEvent&) = delete;
public:
	// async_wait starts an asynchronous wait on the event.
    template <typename CompletionToken>
    auto async_wait(CompletionToken&& token) {
        return boost::asio::async_initiate<CompletionToken, void(T)>(
            [this](auto&& handler) {
                std::lock_guard<std::mutex> guard(this->lock);
                auto h = std::make_shared<std::decay_t<decltype(handler)>>(std::move(handler));
                this->handlers.push_back([h](T value) {
                    (*h)(value);
                 });
            }, token);
    }

    // NotifyAll resume all waiting coroutines with the provided value. 
    // This function is thread-safe and can be called from any thread.
    void NotifyAll(T&& value) {
        std::vector<std::function<void(T)>> handlersToNotify;
        {
            std::lock_guard<std::mutex> guard(lock);
            handlersToNotify.swap(handlers);
        }
        
        boost::asio::post(ioc, [handlersToNotify = std::move(handlersToNotify), value = std::move(value)]() mutable {
            for (auto& handler : handlersToNotify) {
                handler(value);
            }   
        });
        
    }
};

