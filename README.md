# LMRTFY
Let Me Run That For You: A C++20 Thread Pool Library

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
  
  std::future<int> sum = pool.push(add, 2, 2);
  
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
  
  std::future<int> sum = pool.push([](int x, int y)
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

int main()
{
  lmrtfy::thread_pool<std::size_t> pool;
  
  std::vector<std::vector<int>> nums(pool.size());
  
  for (std::size_t i = 0; i < pool.size() * 100; ++i)
  {
    pool.push([&](std::size_t id, std::size_t i)
    {
      nums[id].push_back(i);
    }, i);
  }
}
```

### New features in v0.2.0
Version 0.2.0 is an update to C++20, using concepts universally, and adds more customization points. Thread pools can now accept extra template arguments specifying that extra parameters should be passed to each task. 3 built in parameter options are given:

#### thread_id\<T>
This specifies that the thread ID should be passed to each task, as a T. This is the previous example, but with thread_id.

```C++
#include "lmrtfy/thread_pool.hpp"

int main()
{
  lmrtfy::thread_pool<lmrtfy::thread_id<std::size_t>> pool;
  
  std::vector<std::vector<int>> nums(pool.size());
  
  for (std::size_t i = 0; i < pool.size() * 100; ++i)
  {
    pool.push([&](std::size_t id, std::size_t i)
    {
      nums[id].push_back(i);
    }, i);
  }
}
```
This is identical in functionality to the pre-0.2.0 syntax (thread_pool<T>). The previous is syntax is still supported (though more on that below).

#### per_thread\<T>
This specifies that each thread owns a T that is passed in. This the previous example, rewritten so that each vector is passed in directly instead of having to manually index an external vector:
```C++
#include "lmrtfy/thread_pool.hpp"

int main()
{
  lmrtfy::thread_pool<lmrtfy::per_thread<std::vector<int>>> pool;
  
  for (std::size_t i = 0; i < pool.size() * 100; ++i)
  {
    pool.push([&](std::vector<int>& nums, std::size_t i)
    {
      nums.push_back(i);
    }, i);
  }
}
```

#### pool_ref
This is a convenience/minor performance option designed for recursive tasks. Each task will be passed a reference to the pool it is running in. This avoids the need for the pool to be global or passed in manually.
```C++
#include <iostream>
#include "lmrtfy/thread_pool.hpp"

// An alias may be useful here as to not require your task to be templated
using pool_t = lmrtfy::thread_pool<lmrtfy::pool_ref> pool;
  
void recurse(pool_t& pool, int level)
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
  pool_t pool;
  
  pool.push(recurse, 0);
}
```

#### Multiple objects
Any number of these objects can be mixed and matched, just pass them in in the order they should be passed to the tasks. For example,
```C++
lmrtfy::thread_pool<lmrtfy::pool_ref, lmrtfy::per_thread<std::array<int, 10>>> pool;
```
passes a reference to the host pool and a `std::array<int, 10>&` to each task.

Note: Pre-0.2.0 syntax (thread_pool<int>) cannot be mixed with other objects, if multiple objects are used they must all use the new syntax.

#### Custom objects
If none of these built-in objects suit your needs, custom ones can be created. They just need to follow the following structure:
```C++
struct name
{
  explicit name(std::size_t n_threads) { ... }
  
  template<pool_t>
  auto operator()(pool_t& pool, std::size_t thread_id) { ... }
};
```
where `name` is the name of the custom object. This can then be passed into a `thread_pool`'s template argument list like any other.
Some small considerations to make on these is that the pool is not fully constructed when passed in, so some methods (like pool.n_threads()) may not work properly. Also, raw references cannot be used due to having to pass them as rvalue refs into threads, this can be easily avoided by returning `std::reference_wrapper`s to the object you want (you can simply write `return std::ref(thing_to_return)`).
