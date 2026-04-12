#include "pch.h"
#include "../WebRemote/CoEvent.h"
#include <thread>
#include <atomic>

TEST(CoEventTest, Simple) {
	std::atomic<std::shared_ptr<std::string>> coroutine1_received;
	std::atomic<std::shared_ptr<std::string>> coroutine2_received;

	boost::asio::io_context ioc{ 1 };
	CoEvent<std::string> event{ ioc };
	std::thread scheduler([&ioc, &event, &coroutine1_received, &coroutine2_received] {
		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine1_received]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine1_received = std::make_shared<std::string>("coroutine1: " + v);
		}, boost::asio::detached);

		boost::asio::co_spawn(ioc, [&ioc, &event, &coroutine2_received]()->boost::asio::awaitable<void> {
			std::string v = co_await event.async_wait(boost::asio::use_awaitable);
			coroutine2_received = std::make_shared<std::string>("coroutine2: " + v);
		}, boost::asio::detached);

		ioc.run();
	});
	// Wait for the coroutines to start and be waiting on the event.
	// TODO: better synchronization here.
	std::this_thread::sleep_for(std::chrono::seconds(2));
	event.NotifyAll("abc");

	scheduler.join();

	EXPECT_EQ("coroutine1: abc", *coroutine1_received.load());
	EXPECT_EQ("coroutine2: abc", *coroutine2_received.load());
}