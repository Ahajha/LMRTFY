# LMRTFY
Let Me Run That For You: A C++17 Thread Pool Library

## What is a Thread Pool?
A thread pool is a container that keeps a set of internal threads, and dispatches tasks given to it to its threads, which are reused until the pool is destroyed. Thread pools aim to mostly eliminate the overhead of starting and stopping threads every time you want to run a task, lowering the overhead to just that of inserting and removing from the pool. They allow for simpler implementations of highly parallel algorithms.

## Examples
`lmrtfy::thread_pool`'s default constructor uses all available hardware threads, which is a suitable default for most situations. To add an invocable object (function, lambda, functor, etc.) to the pool, simply pass it to `push()`. Here is a simple "Hello, World!":
```C++
#include <iostream>
#include "lmrtfy/thread_pool.hpp"

void say_hello()
{
  std::cout << "Hello, World!\n";
}

int main()
{
  lmrtfy::thread_pool pool;
  
  pool.push(say_hello);
}
```
`lmrtfy::thread_pool`'s destructor ensures all tasks are run before leaving the scope.

If you need to get the result from the task, `push()` returns a `std::future` to the result. Also, if you need to pass arguments to the function as well, just pass them along with the function. Here is an example of both of these together:
```C++
#include <iostream>
#include "lmrtfy/thread_pool.hpp"

int add(int x, int y)
{
  return x + y;
}

int main()
{
  lmrtfy::thread_pool pool;
  
  std::future& sum = pool.push(add, 2, 2);
  
  std::cout << sum.get() << '\n';
}
```
This is the same example, but with a lambda:
```C++
#include <iostream>
#include "lmrtfy/thread_pool.hpp"

int main()
{
  lmrtfy::thread_pool pool;
  
  std::future& sum = pool.push([](int x, int y)
  {
    return x + y;
  }, 2, 2);
  
  std::cout << sum.get() << '\n';
}
```
Here is another example that takes use of all the threads in the pool to add up the elements in a vector by splitting it into sections, summing them individually, then adding the results. Note the use of `size()` to query the total number of threads in the pool.
```C++
#include <numeric>
#include <vector>
#include <iterator>
#include "lmrtfy/thread_pool.hpp"

int parallel_vec_sum(const std::vector<int>& vec)
{
  lmrtfy::thread_pool pool;
  
  std::vector<std::future<int>> futures;
  
  for (std::size_t i = 0; i < pool.size(); ++i)
  {
  	auto begin = vec.begin(), end = vec.begin();
  	std::advance(begin, (vec.size() * i) / pool.size());
    std::advance(end  , (vec.size() * (i + 1)) / pool.size());
    
    futures.push_back(pool.push(std::accumulate<decltype(vec.begin()), int>, begin, end, 0));
  }
  
  int sum = 0;
  for (auto& fut : futures)
  {
    sum += fut.get();
  }
  return sum;
}
```
The thread pool also supports recursively adding tasks. This is a basic example that just splits off in a binary fashion, stopping at a certain depth:
```C++
#include <iostream>
#include "lmrtfy/thread_pool.hpp"

lmrtfy::thread_pool pool;
  
void recurse(int level)
{
  if (level < 4)
  {
    pool.push(recurse, level + 1);
    pool.push(recurse, level + 1);
  }
  
  static std::mutex io_mut;
  std::lock_guard lock(io_mut);
  std::cout << "Level " << level << '\n';
}

int main()
{
  pool.push(recurse, 0);
}
```
The pool guarantees that the threads are either all running or all stopped. So inserting only one task won't cause the other threads to exit, they will continue to wait until all tasks are fully complete.

Additionally, you can specify that thread IDs be passed into the tasks by giving `lmrtfy::thread_pool` a template parameter of what data type you want the thread ID to be cast to. Thread IDs are numbered from 0 (inclusive) to size() (exclusive). This can be useful for accessing one-per-thread variables, which are often used as caches for threaded work. (Also consider using thread_local variables, as another way of creating one-per-thread variables.) Here is a basic example of this in use, which pushes 'task' IDs to the vector associated with the thread that ran it:

```C++
#include <iostream>
#include <vector>
#include "lmrtfy/thread_pool.hpp"

lmrtfy::thread_pool<std::size_t> pool;

std::vector<std::vector<int>> nums(pool.size());

int main()
{
  for (std::size_t i = 0; i < pool.size() * 100; ++i)
  {
    pool.push([](std::size_t id)
    {
      nums[id].push_back(i);
    }, i);
  }
}
```
