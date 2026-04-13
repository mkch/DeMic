#pragma once
#include <boost/asio.hpp>
#include <mutex>
#include <list>
#include <ranges>

// CoEvent is a synchronization primitive that allows multiple coroutines to wait for an event and be notified when the event occurs.
template<class T>
class CoEvent {
private:
    using HandlerType = void(boost::system::error_code, T);
    struct Waiter {
		boost::asio::any_io_executor executor;
        std::function<HandlerType> handler;
        bool cancelled = false;
    };
    std::mutex waitersLock;
    std::list<std::shared_ptr<Waiter>> waiters;
    boost::asio::io_context& ioc;
public:
    CoEvent(boost::asio::io_context& ioc) : ioc(ioc) {}
    CoEvent(const CoEvent&) = delete;
public:
	// async_wait starts an asynchronous wait on the event.
    template <typename CompletionToken>
    auto async_wait(CompletionToken&& token) {
        namespace asio = boost::asio;
        return asio::async_initiate<CompletionToken, HandlerType>(
            [this](auto&& handler) {
                auto slot = asio::get_associated_cancellation_slot(handler);
                auto waiter = std::make_shared<Waiter>();
                waiter->executor = asio::get_associated_executor(handler, ioc.get_executor());

                auto sharedHandler = std::make_shared<std::decay_t<decltype(handler)>>(std::move(handler));
                waiter->handler = [sharedHandler](boost::system::error_code ec, T value) {
                    (*sharedHandler)(ec, value);
                };

                {
                    std::lock_guard<std::mutex> guard(waitersLock);
                    waiters.push_back(waiter);
                }

                // Register cancellation handler
                if (slot.is_connected()) {
                    slot.assign([this, waiter](asio::cancellation_type type) mutable {
                        if (!(type & asio::cancellation_type::all))
                            return;

                        // Remove from waiting list.
                        {
                            std::lock_guard<std::mutex> guard(waitersLock);
                            waiters.remove(waiter);
                        }

						// Resume handler in executor with cancellation indication.
                        asio::post(waiter->executor, [waiter]() mutable {
                            waiter->cancelled = true;
                            waiter->handler(boost::asio::error::operation_aborted, T{});
                        });
                    });
                }
            }, token);
    }

    // NotifyAll resume all waiting coroutines with the provided value. 
    // This function is thread-safe and can be called from any thread.
    void NotifyAll(T&& value) {
        namespace asio = boost::asio;

        std::list<std::shared_ptr<Waiter>> waitersToNotify;
        {
            std::lock_guard<std::mutex> guard(waitersLock);
            waiters.swap(waitersToNotify);
        }

        for (auto& waiter : waitersToNotify) {
            asio::post(asio::get_associated_executor(waiter->executor, ioc.get_executor()), [waiter, value = value]() mutable {
                if (!waiter->cancelled) {
                    waiter->handler({}, value);
                }
            });
        }
    }
};

