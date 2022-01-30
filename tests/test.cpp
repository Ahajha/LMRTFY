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

TEST_CASE("pass raw references to pool")
{
	int x = 2;
	{
	lmrtfy::thread_pool pool;
	
	pool.push([](int& y) { y = 4; }, x);
	}
	CHECK(x == 2);
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

TEST_CASE("ensure a reasonable spread of jobs across threads, using legacy syntax")
{
	constexpr auto expected_n_jobs_per_thread = 10000;
	
	std::vector<int> n_jobs_per_thread(std::thread::hardware_concurrency(), 0);
	{
	lmrtfy::thread_pool<int> pool;
	
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

TEST_CASE("thread ids should be consistent")
{
	// This is to test the template system a bit
	lmrtfy::thread_pool<lmrtfy::thread_id<int>,
	                    lmrtfy::thread_id<int>,
	                    lmrtfy::thread_id<int>,
	                    lmrtfy::thread_id<int>,
	                    lmrtfy::thread_id<int>> pool;
	
	for (int i = 0; i < 1000 * pool.size(); ++i)
	{
		pool.push([](int id1, int id2, int id3, int id4, int id5)
		{
			CHECK(id1 == id2);
			CHECK(id1 == id3);
			CHECK(id1 == id4);
			CHECK(id1 == id5);
		});
	}
}

struct construct
{
	std::vector<std::vector<int>> vecs;
	
	explicit construct(std::size_t n_threads) : vecs(n_threads) {}
	
	template<class pool>
	auto operator()(pool&, std::size_t tid)
	{
		vecs[tid].push_back(static_cast<int>(tid));
		return std::ref(vecs[tid]);
	}

};

TEST_CASE("ensure lifetime of contained objects is not ended early")
{
	for (int i = 0; i < 100; ++i)
	{
		lmrtfy::thread_pool<lmrtfy::thread_id<int>, construct> pool;
		
		for (int i = 0; i < 10 * pool.size(); ++i)
		{
			pool.push([](int tid, std::vector<int>& vec)
			{
				CHECK(vec.size() == 1);

				if (vec.size() == 1)
				{
					CHECK(vec[0] == tid);
				}
			});
		}
	}
}

TEST_CASE("ensure reasonable task spread using per_thread")
{
	constexpr auto expected_n_jobs_per_thread = 10000;
	
	// Start by saying no workers have done any work
	std::atomic<std::size_t> n_lazy_workers = std::thread::hardware_concurrency();
	
	{
	lmrtfy::thread_pool<lmrtfy::per_thread<int>> pool;
	
	const auto total_jobs = pool.size() * expected_n_jobs_per_thread;
	
	for (int i = 0; i < total_jobs; ++i)
	{
		pool.push([&](int& n_completed_jobs){
			if (++n_completed_jobs == expected_n_jobs_per_thread / 2)
				--n_lazy_workers;
		});
	}
	}
	
	CHECK(n_lazy_workers == 0);
}

struct foo
{
	void bar() {}
};

TEST_CASE("pass member function as task")
{
	lmrtfy::thread_pool pool;
	
	auto f = foo{};
	pool.push(&foo::bar, &f);
}

template<class pool>
void recurse(pool& p, int level, std::atomic<int>& n_jobs)
{
	++n_jobs;
	if (level > 0)
	{
		p.push(recurse<pool>, level - 1, std::ref(n_jobs));
		p.push(recurse<pool>, level - 1, std::ref(n_jobs));
	}
}

TEST_CASE("run recursive tasks")
{
	std::atomic<int> n_jobs = 0;
	{
	lmrtfy::thread_pool<lmrtfy::pool_ref> pool;
	
	pool.push(recurse<decltype(pool)>, 13, std::ref(n_jobs));
	}
	CHECK(n_jobs == 16383);
}
