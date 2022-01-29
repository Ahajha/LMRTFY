#pragma once

#include <type_traits>
#include <condition_variable>
#include <future>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <functional>
#include <tuple>
#include <utility>
#include "function2/function2.hpp"

#ifdef __cpp_concepts
#include <concepts>
#endif

namespace lmrtfy
{

namespace detail
{

enum class pool_state : uint8_t
{
	running, stopping, stopped
};

/*
Base class for thread pool implementations. Not intended to be used directly, though can't
do anything useful anyways. Omits push() and the task queue, which need to be specialized
by the template subclass.
*/
template<template<class> class task_queue_t, class... base_arg_ts>
class thread_pool_base
{
public:
	explicit thread_pool_base() : n_idle_(0), state(pool_state::running) {}
	
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
	pool_state state;
	
	// Used for waking up threads when new tasks are available or the pool is stopping.
	// Note that according to
	// https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one
	// , signals shouldn't need to be guarded by holding a mutex.
	std::condition_variable signal;
	
	// Used for atomic operations on the task queue. Any modifications done to the
	// thread pool are done while holding this mutex.
	std::mutex mut;
	
	// Queue of tasks to be completed.
	task_queue_t<fu2::unique_function<void(base_arg_ts...)>> tasks;
	
	friend class worker_thread;
};

struct worker_thread
{
	template<template<class> class task_queue_t, class... base_arg_ts>
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
				if (pool->n_idle_ == pool->size() && pool->state == pool_state::stopping)
				{
					pool->state = pool_state::stopped;
					
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
				
				if (pool->state == pool_state::stopped) return;
				
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

template<template<class> class task_queue_t, class... base_arg_ts>
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

template<template<class> class task_queue_t, class... base_arg_ts>
thread_pool_base<task_queue_t, base_arg_ts...>::~thread_pool_base()
{
	// Give the waiting threads a command to finish.
	{
		std::lock_guard lock(this->mut);
		
		this->state = (this->n_idle_ == this->size() && this->tasks.empty())
			? pool_state::stopped
			: pool_state::stopping;
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

template<class, class, class>
struct thread_constructor;

// We use this to expand a tuple (tup) back into a parameter pack.
// This is intended to be templated with a std::make_integer_sequence<sizeof...(Ts)>,
// which creates a sequence 0,1,...,(sizeof...(Ts) - 1). This can then be used with
// std::get to get each item in the tuple and apply a function to it.
// This must be a struct because we require this to be a partial specialization.
template<class pool, class tuple, std::size_t... idxs>
struct thread_constructor<pool, tuple, std::integer_sequence<std::size_t, idxs...>>
{
	pool& p;
	tuple& tup;
	std::vector<std::thread>& threads;
	
	void construct(std::size_t tid) const
    {
        threads.emplace_back(worker_thread{}, &p, (std::get<idxs>(tup)(p, tid))...);
    }
};

// The thread pool must ensure this tuple is destroyed *after* the threads finish
// working, otherwise there may be lifetime issues. Since members must be destroyed
// before base classes, we get around this by privately inheriting from this base
// class, allowing the other base class to be destroyed first.
template<class... Ts>
struct tuple_base_class
{
	std::tuple<Ts...> tup;
};

} // namespace detail

/*!
Thread pool class. Base_arg_ts are structures with the following interface:
struct name
{
	explicit name(std::size_t n_threads) { ... }
	
	template<class pool>
	auto operator()(pool& p, std::size_t tid) { ... }
};
One copy of this struct is held, which is valid for the lifetime of the thread pool.
Operator() is called to construct an item to be passed to each thread using each
thread's thread ID.
*/
template<class... base_arg_ts>
class thread_pool : private detail::tuple_base_class<base_arg_ts...>,
	public detail::thread_pool_base<std::queue,
	std::invoke_result_t<base_arg_ts, thread_pool<base_arg_ts...>&, std::size_t>...>
{
public:
	/*!
	Creates a thread pool with a given number of threads. Default attempts to use all threads
	on the given hardware, based on the implementation of std::thread::hardware_concurrency().
	*/
	explicit thread_pool(std::size_t n_threads = std::thread::hardware_concurrency());
};

template<class... base_arg_ts>
thread_pool<base_arg_ts...>::thread_pool(std::size_t n_threads)
	: detail::tuple_base_class<base_arg_ts...>({base_arg_ts{n_threads}...})
{
	this->threads.reserve(n_threads);
	
	detail::thread_constructor<thread_pool, std::tuple<base_arg_ts...>,
		std::make_index_sequence<sizeof...(base_arg_ts)>>
		constructor{*this, this->tup, this->threads};
	
	for (std::size_t tid = 0; tid < n_threads; ++tid)
	{
		constructor.construct(tid);
	}
}

template<std::integral id_t>
struct thread_id
{
	explicit thread_id(std::size_t) {}
	
	template<class pool_t>
	id_t operator()(pool_t&, std::size_t id) const
	{
		return static_cast<id_t>(id);
	}
};

} // namespace lmrtfy
