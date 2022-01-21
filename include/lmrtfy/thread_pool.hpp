#pragma once

#include <type_traits>
#include <condition_variable>
#include <future>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <functional>
#include <utility>
#include "function2/function2.hpp"

#ifdef __cpp_concepts
#include <concepts>
#endif

namespace lmrtfy
{

enum class _pool_state : uint8_t
{
	running, stopping, stopped
};

/*
Base class for thread pool implementations. Not intended to be used directly, though can't
do anything useful anyways. Omits push() and the task queue, which need to be specialized
by the template subclass.
*/
template<class task_queue_t, class... base_arg_ts>
class thread_pool_base
{
public:
	explicit thread_pool_base() : n_idle_(0), state(_pool_state::running) {}
	
	/*!
	Waits for all tasks in the queue to be finished, then stops.
	*/
	~thread_pool_base();
	
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
	
	/*!
	Pushes a function and its arguments to the task queue. Returns the
	result as a future. f should take all specified base parameters,
	followed by the parameters passed to push(), as its arguments.
	*/
	template<class f_t, class... arg_ts>
		requires std::invocable<f_t, base_arg_ts..., arg_ts...>
	std::future<std::invoke_result_t<f_t, base_arg_ts..., arg_ts...>> push(f_t&& f, arg_ts&&... args);
	
protected:
	std::vector<std::thread> threads;
	
	// Number of currently idle threads. Required to ensure the threads are "all-or-nothing",
	// that is, threads will only stop if they know all other threads will stop.
	std::size_t n_idle_;
	
	// Initially set to running, set to stopping when the destructor is called, then set to
	// stopped once all threads are idle and there is no more work to be done.
	_pool_state state;
	
	// Used for waking up threads when new tasks are available or the pool is stopping.
	// Note that according to
	// https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one
	// , signals shouldn't need to be guarded by holding a mutex.
	std::condition_variable signal;
	
	// Used for atomic operations on the task queue. Any modifications done to the
	// thread pool are done while holding this mutex.
	std::mutex mut;
	
	// Queue of tasks to be completed.
	task_queue_t tasks;
	
	friend class _worker_thread;
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
class thread_pool : public thread_pool_base<std::queue<fu2::unique_function<void(thread_id_t)>>, thread_id_t>
{
public:
	/*!
	Creates a thread pool with a given number of threads. Default attempts to use all threads
	on the given hardware, based on the implementation of std::thread::hardware_concurrency().
	*/
	explicit thread_pool(std::size_t n_threads = std::thread::hardware_concurrency());
};

/*!
Thread pool class.
Thread_id_t is used to determine the signature of callable objects it accepts. More
information about this parameter in push().
*/
template<>
class thread_pool<void> : public thread_pool_base<std::queue<fu2::unique_function<void()>>>
{
public:
	/*!
	Creates a thread pool with a given number of threads. Default attempts to use all threads
	on the given hardware, based on the implementation of std::thread::hardware_concurrency().
	*/
	inline explicit thread_pool(std::size_t n_threads = std::thread::hardware_concurrency());
};

struct _worker_thread
{
	template<class task_queue_t, class... base_arg_ts>
	void operator()(thread_pool_base<task_queue_t, base_arg_ts...>* pool, base_arg_ts... args) const
	{
		std::unique_lock mutex_lock(pool->mut);
		
		while (true)
		{
			// Try to get a task
			if (pool->tasks.empty())
			{
				// No tasks, this thread is now idle.
				++pool->n_idle_;
				
				// If this was the last worker running and the pool is stopping, wake
				// up all other threads, who are waiting for the others to finish.
				if (pool->n_idle_ == pool->size() && pool->state == _pool_state::stopping)
				{
					pool->state = _pool_state::stopped;
					
					// Minor optimization, unlock now instead of having it release
					// automatically. This also allows the notification to not require
					// the lock. We also must return now, since it can't be woken up later.
					mutex_lock.unlock();
					pool->signal.notify_all();
					return;
				}
				
				// Wait for a signal (wait unlocks and relocks the mutex). A signal is
				// sent when a new task comes in, the last worker finishes the last task,
				// or the threads are told to stop.
				pool->signal.wait(mutex_lock);
				
				if (pool->state == _pool_state::stopped) return;
				
				--pool->n_idle_;
			}
			else
			{
				// Grab the next task and run it.
				auto task = std::move(pool->tasks.front());
				pool->tasks.pop();
				
				mutex_lock.unlock();
				
				task(args...);
				
				mutex_lock.lock();
			}
		}
	}
};

template<class thread_id_t>
#ifdef __cpp_concepts
	requires (std::is_void_v<thread_id_t> || std::integral<thread_id_t>)
#endif
thread_pool<thread_id_t>::thread_pool(std::size_t n_threads)
{
	this->threads.reserve(n_threads);
	for (std::size_t thread_id = 0; thread_id < n_threads; ++thread_id)
	{
		this->threads.emplace_back(_worker_thread{},
			this, static_cast<thread_id_t>(thread_id));
	}
}

thread_pool<void>::thread_pool(std::size_t n_threads)
{
	this->threads.reserve(n_threads);
	for (std::size_t thread_id = 0; thread_id < n_threads; ++thread_id)
	{
		this->threads.emplace_back(_worker_thread{}, this);
	}
}

template<class task_queue_t, class... base_arg_ts>
template<class f_t, class... arg_ts>
	requires std::invocable<f_t, base_arg_ts..., arg_ts...>
std::future<std::invoke_result_t<f_t, base_arg_ts..., arg_ts...>>
	thread_pool_base<task_queue_t, base_arg_ts...>::push(f_t&& f, arg_ts&&... args)
{
	using package_t = std::packaged_task<
		std::invoke_result_t<f_t, base_arg_ts..., arg_ts...>(base_arg_ts...)
	>;
	
	auto task = package_t([f = std::forward<f_t>(f), ... args = std::forward<arg_ts>(args)]
		(base_arg_ts... base_args)
		{
			return f(base_args..., args...);
		}
	);
	
	auto fut = task.get_future();
	
	{
		std::lock_guard lock(this->mut);
		this->tasks.emplace(std::move(task));
	}
	
	// Notify one waiting thread so it can wake up and take this new task.
	this->signal.notify_one();
	
	return fut;
}

template<class task_queue_t, class... base_arg_ts>
thread_pool_base<task_queue_t, base_arg_ts...>::~thread_pool_base()
{
	// Give the waiting threads a command to finish.
	{
		std::lock_guard lock(this->mut);
		
		this->state = (this->n_idle_ == this->size() && this->tasks.empty())
			? _pool_state::stopped
			: _pool_state::stopping;
	}
	
	// Signal everyone in case any have gone to sleep.
	this->signal.notify_all();
	
	// Wait for the computing threads to finish.
	for (auto& thr : this->threads)
	{
		if (thr.joinable())
			thr.join();
	}
}

}
