#include "custom_chips/jitCompileWorker.h"

#include <chrono>
#include <cstdio>
#include <memory>

namespace
{
	struct State
	{
		std::mutex mutex;
		std::condition_variable cv;
		std::vector<int> started, destroyed;
		std::thread::id owner = std::this_thread::get_id();
		unsigned releasesOnOwner = 0;
		bool release = false;
	};
	struct Result
	{
		State& state;
		int id;
		~Result()
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			state.destroyed.push_back(id);
			state.releasesOnOwner += std::this_thread::get_id() == state.owner;
			state.cv.notify_all();
		}
	};
	using Worker = chips::JitCompileWorker<chips::JitCompile::Worker, int, std::unique_ptr<Result>>;
}

int main()
{
	State state;
	unsigned failures = 0;
	auto check = [&](bool value, const char* message)
	{
		if (!value)
		{
			++failures;
			std::printf("worker: %s\n", message);
		}
	};
	auto wait = [&](auto predicate)
	{
		std::unique_lock<std::mutex> lock(state.mutex);
		return state.cv.wait_for(lock, std::chrono::seconds(5), predicate);
	};

	int program = 1;
	auto fill = [&](int& snapshot) { snapshot = program; };
	Worker worker([&](const int& id)
	{
		std::unique_lock<std::mutex> lock(state.mutex);
		state.started.push_back(id);
		state.cv.notify_all();
		if (id == 1)
			state.cv.wait_for(lock, std::chrono::seconds(5), [&] { return state.release; });
		return std::unique_ptr<Result>(new Result{state, id});
	});
	auto pollUntil = [&](std::unique_ptr<Result>& result)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!worker.poll(fill, result) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return result != nullptr;
	};

	worker.kick(fill);
	check(wait([&] { return !state.started.empty(); }), "first compile starts");
	std::unique_ptr<Result> result;
	check(!worker.poll(fill, result), "polling does not wait for a blocked compile");
	program = 2;
	worker.kick(fill);
	program = 3;
	worker.kick(fill);
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.release = true;
	}
	state.cv.notify_all();
	check(pollUntil(result) && result->id == 3, "the latest program wins");
	check(wait([&] { return !state.destroyed.empty(); }), "the superseded result is released");
	worker.retire(std::move(result));
	check(wait([&] { return state.destroyed.size() == 2; }), "an idle worker disposes of retired code");
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		check(state.started == std::vector<int>({1, 3}), "kicks during a compile coalesce");
		check(state.destroyed == std::vector<int>({1, 3}), "results are released in order");
		check(state.releasesOnOwner == 0, "code is released off the calling thread");
	}

	worker.setSynchronous(true);
	program = 4;
	worker.kick(fill);
	check(worker.poll(fill, result) && result && result->id == 4, "a synchronous compile lands at once");
	result.reset();
	worker.setSynchronous(false);
	program = 5;
	worker.kick(fill);
	check(pollUntil(result) && result->id == 5, "the worker restarts");
	result.reset();

	chips::JitCompileWorker<chips::JitCompile::None, int, std::unique_ptr<Result>> none([&](const int&)
	{
		++failures;
		return std::unique_ptr<Result>();
	});
	none.kick(fill);
	check(!none.inFlight() && !none.poll(fill, result), "no back end, no compile");

	std::printf("compile worker failures=%u\n", failures);
	return failures ? 1 : 0;
}
