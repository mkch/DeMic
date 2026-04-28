#pragma once
#include <boost/asio.hpp>
#include <mutex>
#include <list>
#include <ranges>

// co_event is a synchronization primitive that allows multiple coroutines to wait for an event and be notified when the event occurs.
template<class T>
class co_event {
private:
    using handler_type = void(boost::system::error_code, T);
    struct waiter {
		boost::asio::any_io_executor executor;
        std::function<handler_type> handler;
        bool invoked = false;
    };
    std::mutex waitersLock;
    std::list<std::shared_ptr<waiter>> waiters;
    boost::asio::io_context& ioc;
public:
    co_event(boost::asio::io_context& ioc) : ioc(ioc) {}
    co_event(const co_event&) = delete;
public:
	// async_wait starts an asynchronous wait on the event.
    template <typename CompletionToken>
    auto async_wait(CompletionToken&& token) {
        namespace asio = boost::asio;
        return asio::async_initiate<CompletionToken, handler_type>(
            [this](auto&& handler) {
                auto slot = asio::get_associated_cancellation_slot(handler);
                auto waiter = std::make_shared<co_event::waiter>();
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
                            if (waiter->invoked) {
								return; // Avoid double-invocation.
                            }
                            waiter->handler(boost::asio::error::operation_aborted, T{});
                            waiter->invoked = true;
                        });
                    });
                }
            }, token);
    }

    // notify_all resume all waiting coroutines with the provided value. 
    // This function is thread-safe and can be called from any thread.
    void notify_all(T&& value) {
        namespace asio = boost::asio;

        std::list<std::shared_ptr<waiter>> waiters_to_resume;
        {
            std::lock_guard<std::mutex> guard(waitersLock);
            waiters.swap(waiters_to_resume);
        }

        for (auto& waiter : waiters_to_resume) {
            asio::post(waiter->executor, [waiter, value = value]() mutable {
                if (waiter->invoked) {
                    return; // Avoid double-invocation.
                }
                waiter->handler({}, value);
                waiter->invoked = true;
            });
        }
    }
};

