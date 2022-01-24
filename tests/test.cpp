#include "lmrtfy/thread_pool.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <functional>
#include <memory>
#include <atomic>
#include <thread>

TEST_CASE("construction and destruction without tasks")
{
	lmrtfy::thread_pool{};
	CHECK(true);
}

int add(int a, int b)
{
	return a + b;
}

TEST_CASE("adding using add()")
{
	lmrtfy::thread_pool pool;
	
	auto result = pool.push(add, 2, 2);
	
	CHECK(result.get() == 4);
}

TEST_CASE("adding using lambda")
{
	lmrtfy::thread_pool pool;
	
	auto result = pool.push([](int a, int b) {
		return a + b;
	}, 2, 2);
	
	CHECK(result.get() == 4);
}

TEST_CASE("adding using functor")
{
	lmrtfy::thread_pool pool;
	
	struct add_functor
	{
		int operator()(int a, int b) const
		{
			return a + b;
		}
	};
	
	auto result = pool.push(add_functor{}, 2, 2);
	
	CHECK(result.get() == 4);
}

TEST_CASE("perfect forwarding of prvalues")
{
	lmrtfy::thread_pool pool;
	
	pool.push([](auto&& value) {
		CHECK(*value == 5);
	}, std::make_unique<int>(5));
}

TEST_CASE("perfect forwarding of moved-from values")
{
	lmrtfy::thread_pool pool;
	
	auto val = std::make_unique<int>(7);
	
	pool.push([](auto&& value) {
		CHECK(*value == 7);
	}, std::move(val));
	
	CHECK(!val);
}

TEST_CASE("passing references to pool")
{
	int x = 2;
	{
	lmrtfy::thread_pool pool;
	
	pool.push([](int& y) { y = 4; }, std::ref(x));
	}
	CHECK(x == 4);
}

TEST_CASE("ensure all jobs from a large batch are run")
{
	std::atomic<int> n_jobs = 0;
	
	{
	lmrtfy::thread_pool pool;
	
	for (int i = 0; i < 100000; ++i)
	{
		pool.push([&]{ ++n_jobs; });
	}
	}
	
	CHECK(n_jobs == 100000);
}

TEST_CASE("ensure all jobs from a large batch are run")
{
	std::atomic<int> n_jobs = 0;
	
	{
	lmrtfy::thread_pool pool;
	
	for (int i = 0; i < 100000; ++i)
	{
		pool.push([&]{ ++n_jobs; });
	}
	}
	
	CHECK(n_jobs == 100000);
}

TEST_CASE("ensure a reasonable spread of jobs across threads")
{
	constexpr auto expected_n_jobs_per_thread = 10000;
	
	std::vector<int> n_jobs_per_thread(std::thread::hardware_concurrency(), 0);
	{
	lmrtfy::thread_pool<lmrtfy::thread_id<int>> pool;
	
	const auto total_jobs = pool.size() * expected_n_jobs_per_thread;
	
	for (int i = 0; i < total_jobs; ++i)
	{
		pool.push([&](int id){ ++n_jobs_per_thread[id]; });
	}
	}
	
	// We are very generous with the definition of "reasonable", we expect each
	// thread to have half the number of expected jobs at a minimum.
	// In practice it's usually above 80%.
	for (int n_jobs : n_jobs_per_thread)
	{
		CHECK(n_jobs >= expected_n_jobs_per_thread / 2);
	}
}

TEST_CASE("pass custom per-thread item constructor")
{
	constexpr auto construct = [](auto&, std::size_t id)
	{
		return std::vector<int>{static_cast<int>(id)};
	};

	lmrtfy::thread_pool<lmrtfy::thread_id<int>, decltype(construct)> pool;
	
	// This currently does not work as intented, objects have to be passed
	// in by the *exact* type they are created as, here that is a
	// std::vector<int>, no reference. A potential solution is to store
	// a single instance of each base argument type, and that would allow
	// these functions to return references.
	/*
	for (int i = 0; i < 100 * pool.size(); ++i)
	{
		pool.push([](std::size_t tid, std::vector<int> vec)
		{
			CHECK(vec.size() <= 1);

			if (vec.size() == 1)
			{
				CHECK(vec[0] == tid);
			}
		});
	}
	*/
}