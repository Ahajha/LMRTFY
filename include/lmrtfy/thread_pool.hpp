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
class thread_pool
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
	Returns the number of threads in the pool.
	*/
	[[nodiscard]] std::size_t size() const { return threads.size(); }
	
	/*!
	Returns the number of currently idle threads.
	*/
	[[nodiscard]] std::size_t n_idle() const { return n_idle_; }
	
	/*!
	Pushes a function and its arguments to the task queue. Returns the result as a future.
	If thread_id_t is void,	the thread ID is not passed to f.
	If thread_id_t is not void, f should take a thread_id_t as its first parameter, the thread
	ID is given through this. Thread IDs are numbered in [0, size()).
	Expects that thread_id_t's maximum value is at least (size() - 1), so that the thread IDs
	can be passed correctly, behaviour is undefined if not.
	*/
	template<class F, class... Args>
#ifdef __cpp_concepts
		// Strictly speaking, this shouldn't cause any compile errors, as std::invoke_result
		// will produce an error if this does. However, this is here as a nice-to-have, as it
		// should produce better error messages in C++20.
		requires (std::is_void_v<thread_id_t>
			? std::invocable<F,Args...>
			: std::invocable<F,thread_id_t,Args...>)
#endif
	std::future<std::conditional<std::is_void_v<thread_id_t>,
			std::invoke_result_t<F,Args...>, std::invoke_result_t<F,thread_id_t,Args...>>>
		push(F&& f, Args&&... args);
	
	// Various members are either not copyable or movable, thus the pool is neither.
	thread_pool(const thread_pool&) = delete;
	thread_pool(thread_pool&&) = delete;
	thread_pool& operator=(const thread_pool&) = delete;
	thread_pool& operator=(thread_pool&&) = delete;

private:
	std::vector<std::thread> threads;
	
	// Number of currently idle threads. Required to ensure the threads are "all-or-nothing",
	// that is, threads will only stop if they know all other threads will stop.
	std::size_t n_idle_;
	
	/*
	I would like to just implement the queue directly in the thread_pool class as
	std::queue<fu2::unique_function<std::conditional_t
		<std::is_void_v<thread_id_t>, void(), void(thread_id_t)>
	>> tasks;
	, however the evaluation of "void(thread_id_t)" when thread_id_t = void is invalid, which
	is bothersome because it is only an error in the situation that it isn't used anyways.
	To circumvent this without providing an entire separate specialization or
	adding extra classes visible outside this one, this struct is *partially* specialized
	with void, following the example here: https://stackoverflow.com/questions/33758287/
	The second parameter here is a dummy, to avoid it being a full specialization.
	*/
	template<class T, int _ = 0>
	struct task_queue;
	
	task_queue<thread_id_t> tasks;
	
	// Queue of tasks to be completed. fu2::unique_function is used to allow non-copyable
	// types to be inserted.
	//task_queue<thread_id_t> tasks;
	
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

template<class T>
template<class U, int _>
struct thread_pool<T>::task_queue
{
	std::queue<fu2::unique_function<void(T)>> queue;
};

template<class T>
template<int _>
struct thread_pool<T>::task_queue<void,_>
{
	std::queue<fu2::unique_function<void()>> queue;
};

}
