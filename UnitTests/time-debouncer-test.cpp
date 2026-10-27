#include "pch.h"
#include "../TimeDebouncer.h"

#include <windows.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

// SetTimer timers are quantized to the system clock tick (about 15.6 ms),
// so the intervals used here are kept well above it to avoid flakiness.
constexpr UINT kInterval = 100;
constexpr UINT kLongInterval = 300;
constexpr DWORD kTimeout = 5000;

// TimeDebouncer runs onComplete from the message queue of the thread owning the
// message window, so the tests have to pump messages to let it run.
bool PumpUntil(const std::function<bool()>& done, DWORD timeoutMs = kTimeout) {
	const ULONGLONG deadline = GetTickCount64() + timeoutMs;
	for (;;) {
		if (done()) {
			return true;
		}
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		if (done()) {
			return true;
		}
		const ULONGLONG now = GetTickCount64();
		if (now >= deadline) {
			return false;
		}
		MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(deadline - now), QS_ALLINPUT, 0);
	}
}

// Pumps messages for the given period, giving pending timers a chance to fire.
void PumpFor(DWORD durationMs) {
	PumpUntil([] { return false; }, durationMs);
}

std::function<void()> NoLog() {
	return [] {};
}

} // namespace

TEST(TimeDebouncerTest, CallsOnCompleteWithEmittedArgumentsAfterInterval) {
	std::vector<std::pair<int, std::string>> received;
	TimeDebouncer<int, std::string> debouncer(kInterval,
		[&received](int number, std::string text) { received.emplace_back(number, std::move(text)); },
		NoLog());

	debouncer.Emit(42, "hello");

	PumpFor(kInterval / 4);
	EXPECT_TRUE(received.empty()) << "onComplete must not run before the interval elapses";

	ASSERT_TRUE(PumpUntil([&received] { return !received.empty(); }));
	ASSERT_EQ(1u, received.size());
	EXPECT_EQ(42, received[0].first);
	EXPECT_EQ("hello", received[0].second);

	// A single Emit must produce a single completion.
	EXPECT_FALSE(PumpUntil([&received] { return received.size() > 1; }, kInterval * 2));
	EXPECT_EQ(1u, received.size());
}

TEST(TimeDebouncerTest, RestartsIntervalOnEachEmit) {
	std::vector<int> received;
	TimeDebouncer<int> debouncer(kLongInterval,
		[&received](int value) { received.push_back(value); },
		NoLog());

	for (int i = 0; i < 5; ++i) {
		debouncer.Emit(i);
		PumpFor(kLongInterval / 6); // Shorter than the interval, so the debouncer keeps restarting.
	}
	EXPECT_TRUE(received.empty()) << "onComplete must be deferred while Emit keeps being called";

	ASSERT_TRUE(PumpUntil([&received] { return !received.empty(); }));
	EXPECT_FALSE(PumpUntil([&received] { return received.size() > 1; }, kLongInterval));
	EXPECT_EQ((std::vector<int>{4}), received) << "onComplete must get the last emitted arguments";
}

TEST(TimeDebouncerTest, CompletesAgainAfterANewEmit) {
	std::vector<int> received;
	TimeDebouncer<int> debouncer(kInterval,
		[&received](int value) { received.push_back(value); },
		NoLog());

	debouncer.Emit(1);
	ASSERT_TRUE(PumpUntil([&received] { return received.size() == 1; }));

	debouncer.Emit(2);
	ASSERT_TRUE(PumpUntil([&received] { return received.size() == 2; }));

	EXPECT_EQ((std::vector<int>{1, 2}), received);
}

TEST(TimeDebouncerTest, EmitInsideOnCompleteIsSafe) {
	std::vector<int> received;
	TimeDebouncer<int> debouncer(kInterval,
		[&received, &debouncer](int value) {
			received.push_back(value);
			if (value < 3) {
				debouncer.Emit(value + 1);
			}
		},
		NoLog());

	debouncer.Emit(0);

	ASSERT_TRUE(PumpUntil([&received] { return received.size() >= 4; }));
	EXPECT_FALSE(PumpUntil([&received] { return received.size() > 4; }, kInterval * 2));
	EXPECT_EQ((std::vector<int>{0, 1, 2, 3}), received);
}

TEST(TimeDebouncerTest, CancelInsideOnCompleteStopsCompletion) {
	int count = 0;
	TimeDebouncer<> debouncer(kInterval,
		[&count, &debouncer] {
			++count;
			debouncer.Cancel();
		},
		NoLog());

	debouncer.Emit();

	ASSERT_TRUE(PumpUntil([&count] { return count == 1; }));
	EXPECT_FALSE(PumpUntil([&count] { return count > 1; }, kInterval * 2));
	EXPECT_EQ(1, count);
}

TEST(TimeDebouncerTest, CancelInsideOnCompleteDoesNotReportError) {
	int completions = 0;
	int errors = 0;
	TimeDebouncer<> debouncer(kInterval,
		[&completions, &debouncer] {
			++completions;
			debouncer.Cancel(); // The timer has already been killed by TimerProc.
		},
		[&errors] { ++errors; });

	debouncer.Emit();

	ASSERT_TRUE(PumpUntil([&completions] { return completions == 1; }));
	PumpFor(kInterval * 2);
	EXPECT_EQ(1, completions);
	EXPECT_EQ(0, errors) << "Cancel() must not report an error when no timer is pending";
}

TEST(TimeDebouncerTest, CancelAfterEmitInsideOnCompleteStopsCompletion) {
	int completions = 0;
	TimeDebouncer<> debouncer(kInterval,
		[&completions, &debouncer] {
			++completions;
			if (completions == 1) {
				debouncer.Emit(); // Schedule another completion.
			}
		},
		NoLog());

	debouncer.Emit();
	ASSERT_TRUE(PumpUntil([&completions] { return completions == 1; }));

	// The timer scheduled by Emit() inside onComplete must be cancelable.
	debouncer.Cancel();

	EXPECT_FALSE(PumpUntil([&completions] { return completions > 1; }, kInterval * 2));
	EXPECT_EQ(1, completions);
}

TEST(TimeDebouncerTest, CancelPreventsCompletion) {
	std::vector<int> received;
	TimeDebouncer<int> debouncer(kInterval,
		[&received](int value) { received.push_back(value); },
		NoLog());

	debouncer.Emit(1);
	debouncer.Cancel();

	EXPECT_FALSE(PumpUntil([&received] { return !received.empty(); }, kInterval * 3));
	EXPECT_TRUE(received.empty());

	// A canceled debouncer can be used again.
	debouncer.Emit(2);
	ASSERT_TRUE(PumpUntil([&received] { return !received.empty(); }));
	EXPECT_EQ((std::vector<int>{2}), received);
}

TEST(TimeDebouncerTest, CancelWithoutPendingEmitIsHarmless) {
	std::vector<int> received;
	TimeDebouncer<int> debouncer(kInterval,
		[&received](int value) { received.push_back(value); },
		NoLog());

	debouncer.Cancel(); // No timer has been started yet.

	debouncer.Emit(1);
	ASSERT_TRUE(PumpUntil([&received] { return !received.empty(); }));
	EXPECT_EQ((std::vector<int>{1}), received);
}

TEST(TimeDebouncerTest, RepeatedCancelDoesNotReportError) {
	int errors = 0;
	TimeDebouncer<> debouncer(kInterval, [] {}, [&errors] { ++errors; });

	debouncer.Emit();
	debouncer.Cancel();
	debouncer.Cancel(); // Nothing is scheduled anymore.

	EXPECT_EQ(0, errors);
}

TEST(TimeDebouncerTest, DoesNotReportErrorWhenNoTimerIsPending) {
	int completions = 0;
	int errors = 0;
	TimeDebouncer<> debouncer(kInterval,
		[&completions] { ++completions; },
		[&errors] { ++errors; });

	debouncer.Cancel(); // No timer has ever been started.
	debouncer.Emit();
	ASSERT_TRUE(PumpUntil([&completions] { return completions == 1; }));
	debouncer.Cancel(); // The timer has already fired.

	EXPECT_EQ(1, completions);
	EXPECT_EQ(0, errors);
}

TEST(TimeDebouncerTest, DestructionCancelsPendingCompletion) {
	std::vector<int> received;
	{
		TimeDebouncer<int> debouncer(kInterval,
			[&received](int value) { received.push_back(value); },
			NoLog());
		debouncer.Emit(1);
	}

	PumpFor(kInterval * 3);
	EXPECT_TRUE(received.empty());
}

TEST(TimeDebouncerTest, InstancesAreIndependent) {
	std::vector<std::string> received;
	TimeDebouncer<std::string> first(kInterval,
		[&received](std::string value) { received.push_back("first:" + value); },
		NoLog());
	TimeDebouncer<std::string> second(kInterval,
		[&received](std::string value) { received.push_back("second:" + value); },
		NoLog());

	first.Emit("a");
	second.Emit("b");
	first.Emit("c"); // Restarts only the first debouncer.

	ASSERT_TRUE(PumpUntil([&received] { return received.size() >= 2; }));
	EXPECT_FALSE(PumpUntil([&received] { return received.size() > 2; }, kInterval * 2));

	std::sort(received.begin(), received.end());
	EXPECT_EQ((std::vector<std::string>{"first:c", "second:b"}), received);
}
