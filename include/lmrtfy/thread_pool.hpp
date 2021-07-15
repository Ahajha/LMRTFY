#pragma once

#include <type_traits>
#include <condition_variable>
#include <future>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include "function2/function2.hpp"

#ifdef __cpp_concepts
#include <concepts>
#endif

namespace lmrtfy
{

/*
Base class for thread pool implementations. Not intended to be used directly, though can't
do anything useful anyways. Omits push() and the task queue, which need to be specialized
by the template subclass.
*/
class thread_pool_base
{
public:
	explicit thread_pool_base(std::size_t n_threads = std::thread::hardware_concurrency())
		: n_idle_(0), state(pool_state::running) {}
	
	~thread_pool_base() = default;
	
	/*!
	Returns the number of threads in the pool.
	*/
	[[nodiscard]] std::size_t size() const { return threads.size(); }
	
	/*!
	Returns the number of currently idle threads.
	*/
	[[nodiscard]] std::size_t n_idle() const { return n_idle_; }
	
	// Various members are either not copyable or movable, thus the pool is neither.
	thread_pool_base(const thread_pool_base&) = delete;
	thread_pool_base(thread_pool_base&&) = delete;
	thread_pool_base& operator=(const thread_pool_base&) = delete;
	thread_pool_base& operator=(thread_pool_base&&) = delete;
	
protected:
	std::vector<std::thread> threads;
	
	// Number of currently idle threads. Required to ensure the threads are "all-or-nothing",
	// that is, threads will only stop if they know all other threads will stop.
	std::size_t n_idle_;
	
	// Initially set to running, set to stopping when the destructor is called, then set to
	// stopped once all threads are idle and there is no more work to be done.
	enum class pool_state : uint8_t
	{
		running, stopping, stopped
	} state;
	
	// Used for waking up threads when new tasks are available or the pool is stopping.
	// Note that according to
	// https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one
	// , signals shouldn't need to be guarded by holding a mutex.
	std::condition_variable signal;
	
	// Used for atomic operations on the task queue. Any modifications done to the
	// thread pool are done while holding this mutex.
	std::mutex mut;
};

/*!
Thread pool class.
Thread_id_t is used to determine the signature of callable objects it accepts. More
information about this parameter in push().
*/
template<class thread_id_t = void>
#ifdef __cpp_concepts
	// In C++17, the thread ID is "duck-typed" to probably only compile if it is an integral,
	// but this is not enforced. In C++20, this is enforced, and will likely give more
	// helpful error messages.
	requires (std::is_void_v<thread_id_t> || std::integral<thread_id_t>)
#endif
class thread_pool : public thread_pool_base
{
public:
	/*!
	Creates a thread pool with a given number of threads. Default attempts to use all threads
	on the given hardware, based on the implementation of std::thread::hardware_concurrency().
	*/
	explicit thread_pool(std::size_t n_threads = std::thread::hardware_concurrency());
	
	/*!
	Waits for all tasks in the queue to be finished, then stops.
	*/
	~thread_pool();
	
	/*!
	Pushes a function and its arguments to the task queue. Returns the result as a future.
	f should take a thread_id_t as its first parameter, the thread ID is given through this.
	Thread IDs are numbered in [0, size()). Expects that thread_id_t's maximum value is at least
	(size() - 1), so that the thread IDs can be passed correctly, behaviour is undefined if not.
	*/
	template<class F, class... Args>
#ifdef __cpp_concepts
		// Strictly speaking, this shouldn't cause any compile errors, as std::invoke_result
		// will produce an error if this does. However, this is here as a nice-to-have, as it
		// should produce better error messages in C++20.
		requires std::invocable<F,thread_id_t,Args...>
#endif
	std::future<std::invoke_result_t<F,thread_id_t,Args...>> push(F&& f, Args&&... args);
	
	// Various members are either not copyable or movable, thus the pool is neither.
	thread_pool(const thread_pool&) = delete;
	thread_pool(thread_pool&&) = delete;
	thread_pool& operator=(const thread_pool&) = delete;
	thread_pool& operator=(thread_pool&&) = delete;

private:
	// Queue of tasks to be completed. fu2::unique_function is used to allow non-copyable
	// types to be inserted.
	std::queue<fu2::unique_function<void(thread_id_t)>> tasks;
};

/*!
Thread pool class.
Thread_id_t is used to determine the signature of callable objects it accepts. More
information about this parameter in push().
*/
template<>
class thread_pool<void> : public thread_pool_base
{
public:
	/*!
	Creates a thread pool with a given number of threads. Default attempts to use all threads
	on the given hardware, based on the implementation of std::thread::hardware_concurrency().
	*/
	explicit thread_pool(std::size_t n_threads = std::thread::hardware_concurrency());
	
	/*!
	Waits for all tasks in the queue to be finished, then stops.
	*/
	~thread_pool();
	
	/*!
	Pushes a function and its arguments to the task queue. Returns the result as a future.
	*/
	template<class F, class... Args>
#ifdef __cpp_concepts
		// Strictly speaking, this shouldn't cause any compile errors, as std::invoke_result
		// will produce an error if this does. However, this is here as a nice-to-have, as it
		// should produce better error messages in C++20.
		requires std::invocable<F,Args...>
#endif
	std::future<std::invoke_result_t<F,Args...>> push(F&& f, Args&&... args);
	
	// Various members are either not copyable or movable, thus the pool is neither.
	thread_pool(const thread_pool&) = delete;
	thread_pool(thread_pool&&) = delete;
	thread_pool& operator=(const thread_pool&) = delete;
	thread_pool& operator=(thread_pool&&) = delete;

private:
	// Queue of tasks to be completed. fu2::unique_function is used to allow non-copyable
	// types to be inserted.
	std::queue<fu2::unique_function<void()>> tasks;
};

template<class thread_id_t>
#ifdef __cpp_concepts
	requires (std::is_void_v<thread_id_t> || std::integral<thread_id_t>)
#endif
thread_pool<thread_id_t>::thread_pool(std::size_t n_threads) : thread_pool_base(n_threads)
{
	threads.reserve(n_threads);
	for (std::size_t thread_id = 0; thread_id < n_threads; ++thread_id)
	{
		// Main loop for the threads
		threads.emplace_back([this, thread_id = static_cast<thread_id_t>(thread_id)]
		{
			std::unique_lock mutex_lock(mut);
			
			while (true)
			{
				// Try to get a task
				if (tasks.empty())
				{
					// No tasks, this thread is now idle.
					++n_idle_;
					
					// If this was the last worker running and the pool is stopping, wake
					// up all other threads, who are waiting for the others to finish.
					if (n_idle_ == size() && state == pool_state::stopping)
					{
						state = pool_state::stopped;
						
						// Minor optimization, unlock now instead of having it release
						// automatically. This also allows the notification to not require
						// the lock. We also must return now, since it can't be woken up later.
						mutex_lock.unlock();
						signal.notify_all();
						return;
					}
					
					// Wait for a signal (wait unlocks and relocks the mutex). A signal is
					// sent when a new task comes in, the last worker finishes the last task,
					// or the threads are told to stop.
					signal.wait(mutex_lock);
					
					if (state == pool_state::stopped) return;
					
					--n_idle_;
				}
				else
				{
					// Grab the next task and run it.
					auto task = std::move(tasks.front());
					tasks.pop();
					
					mutex_lock.unlock();
					
					task(thread_id);
					
					mutex_lock.lock();
				}
			}
		});
	}
}

thread_pool<void>::thread_pool(std::size_t n_threads) : thread_pool_base(n_threads)
{
	threads.reserve(n_threads);
	for (std::size_t thread_id = 0; thread_id < n_threads; ++thread_id)
	{
		// Main loop for the threads
		threads.emplace_back([this]
		{
			std::unique_lock mutex_lock(mut);
			
			while (true)
			{
				// Try to get a task
				if (tasks.empty())
				{
					// No tasks, this thread is now idle.
					++n_idle_;
					
					// If this was the last worker running and the pool is stopping, wake
					// up all other threads, who are waiting for the others to finish.
					if (n_idle_ == size() && state == pool_state::stopping)
					{
						state = pool_state::stopped;
						
						// Minor optimization, unlock now instead of having it release
						// automatically. This also allows the notification to not require
						// the lock. We also must return now, since it can't be woken up later.
						mutex_lock.unlock();
						signal.notify_all();
						return;
					}
					
					// Wait for a signal (wait unlocks and relocks the mutex). A signal is
					// sent when a new task comes in, the last worker finishes the last task,
					// or the threads are told to stop.
					signal.wait(mutex_lock);
					
					if (state == pool_state::stopped) return;
					
					--n_idle_;
				}
				else
				{
					// Grab the next task and run it.
					auto task = std::move(tasks.front());
					tasks.pop();
					
					mutex_lock.unlock();
					
					task();
					
					mutex_lock.lock();
				}
			}
		});
	}
}

template<class thread_id_t>
#ifdef __cpp_concepts
	requires (std::is_void_v<thread_id_t> || std::integral<thread_id_t>)
#endif
thread_pool<thread_id_t>::~thread_pool()
{
	// Give the waiting threads a command to finish.
	{
		std::lock_guard lock(mut);
		
		state = (n_idle_ == size() && tasks.empty())
			? pool_state::stopped
			: pool_state::stopping;
	}
	
	// Signal everyone in case any have gone to sleep.
	signal.notify_all();
	
	// Wait for the computing threads to finish.
	for (auto& thr : threads)
	{
		if (thr.joinable())
			thr.join();
	}
}

thread_pool<void>::~thread_pool()
{
	// Give the waiting threads a command to finish.
	{
		std::lock_guard lock(mut);
		
		state = (n_idle_ == size() && tasks.empty())
			? pool_state::stopped
			: pool_state::stopping;
	}
	
	// Signal everyone in case any have gone to sleep.
	signal.notify_all();
	
	// Wait for the computing threads to finish.
	for (auto& thr : threads)
	{
		if (thr.joinable())
			thr.join();
	}
}

}
