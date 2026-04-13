#include "pch.h"
#include "../WebRemote/CoEvent.h"
#include <thread>
#include <atomic>

TEST(CoEventTest, Simple) {
	std::atomic<std::shared_ptr<std::string>> coroutine1_received;
	std::atomic<std::shared_ptr<std::string>> coroutine2_received;

	boost::asio::io_context ioc{ 1 };
	co_event<std::string> event{ ioc };

	boost::asio::steady_timer wait_ready(ioc);
	wait_ready.expires_at(std::chrono::steady_clock::time_point::max());

	std::thread scheduler([&ioc, &event, &coroutine1_received, &coroutine2_received, &wait_ready] {
		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine1_received]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine1_received = std::make_shared<std::string>("coroutine1: " + v);
		}, boost::asio::detached);

		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine2_received]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine2_received = std::make_shared<std::string>("coroutine2: " + v);
		}, boost::asio::detached);

		// Run the scheduler until the coroutines are waiting on the event.
		ioc.poll();
		wait_ready.cancel();
		ioc.run();
	});
	// Wait for the coroutines to start and be waiting on the event.
	wait_ready.async_wait(boost::asio::use_future).wait();
	event.notify_all("abc");

	scheduler.join();

	EXPECT_EQ("coroutine1: abc", *coroutine1_received.load());
	EXPECT_EQ("coroutine2: abc", *coroutine2_received.load());
}

TEST(CoEventTest, CoroutineCancellation) {
	std::atomic<bool> coroutine1_exited;
	std::atomic<bool> coroutine2_exited;

	boost::asio::io_context ioc{ 1 };
	co_event<std::string> event{ ioc };

	boost::asio::steady_timer wait_ready(ioc);
	wait_ready.expires_at(std::chrono::steady_clock::time_point::max());

	boost::asio::cancellation_signal sig1, sig2;

	std::thread scheduler([&ioc, &event, &coroutine1_exited, &coroutine2_exited, &wait_ready, &sig1, &sig2] {
		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine1_exited, &sig1]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine1_exited = true; // never run.
		}, bind_cancellation_slot(sig1.slot(), boost::asio::detached));

		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine2_exited, &sig2]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine2_exited = true; // never run.
		}, bind_cancellation_slot(sig2.slot(), boost::asio::detached));

		// Run the scheduler until the coroutines are waiting on the event.
		ioc.poll();
		wait_ready.cancel();
		ioc.run();
		});
	// Wait for the coroutines to start and be waiting on the event.
	wait_ready.async_wait(boost::asio::use_future).wait();
	// Cancel the waiting coroutines.
	sig1.emit(boost::asio::cancellation_type::all);
	sig2.emit(boost::asio::cancellation_type::all);

	scheduler.join();

	EXPECT_FALSE(coroutine1_exited);
	EXPECT_FALSE(coroutine2_exited);
}

TEST(CoEventTest, Cancellation) {
	std::atomic<bool> coroutine1_exited;
	std::atomic<bool> coroutine2_exited;

	boost::asio::io_context ioc{ 1 };
	co_event<std::string> event{ ioc };

	boost::asio::steady_timer wait_ready(ioc);
	wait_ready.expires_at(std::chrono::steady_clock::time_point::max());

	boost::asio::cancellation_signal sig1, sig2;

	std::thread scheduler([&ioc, &event, &coroutine1_exited, &coroutine2_exited, &wait_ready, &sig1, &sig2] {
		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine1_exited, &sig1]()->boost::asio::awaitable<void> {
			try {
				co_await event.async_wait(bind_cancellation_slot(sig1.slot(), boost::asio::use_awaitable));
			} catch (boost::system::system_error e) {
				if (e.code() != boost::asio::error::operation_aborted) {
					throw e;
				}
			}
			coroutine1_exited = true; // should run.
		}, boost::asio::detached);

		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine2_exited, &sig2]()->boost::asio::awaitable<void> {
			try {
				co_await event.async_wait(bind_cancellation_slot(sig2.slot(), boost::asio::use_awaitable));
			} catch (boost::system::system_error e) {
				if (e.code() != boost::asio::error::operation_aborted) {
					throw e;
				}
			}
			coroutine2_exited = true; // should run.
			}, boost::asio::detached);

		// Run the scheduler until the coroutines are waiting on the event.
		ioc.poll();
		wait_ready.cancel();
		ioc.run();
		});
	// Wait for the coroutines to start and be waiting on the event.
	wait_ready.async_wait(boost::asio::use_future).wait();
	// Cancel the waiting coroutines.
	sig1.emit(boost::asio::cancellation_type::all);
	sig2.emit(boost::asio::cancellation_type::all);

	scheduler.join();

	EXPECT_TRUE(coroutine1_exited);
	EXPECT_TRUE(coroutine2_exited);
}