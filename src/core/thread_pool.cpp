#include "wg/core/thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "wg/core/assert.hpp"

namespace wg::core {

namespace {

// Pool and worker index of the calling thread while it runs chunks, so that a nested
// parallel_for on the same pool runs inline instead of waiting for itself.
thread_local const void* tl_pool = nullptr;
thread_local std::int32_t tl_worker = 0;

class WorkerScope {
 public:
  WorkerScope(const void* pool, std::int32_t worker) noexcept
      : saved_pool_(tl_pool), saved_worker_(tl_worker) {
    tl_pool = pool;
    tl_worker = worker;
  }
  ~WorkerScope() {
    tl_pool = saved_pool_;
    tl_worker = saved_worker_;
  }
  WorkerScope(const WorkerScope&) = delete;
  WorkerScope& operator=(const WorkerScope&) = delete;
  WorkerScope(WorkerScope&&) = delete;
  WorkerScope& operator=(WorkerScope&&) = delete;

 private:
  const void* saved_pool_;
  std::int32_t saved_worker_;
};

void run_inline(std::int64_t begin, std::int64_t end, std::int64_t grain, const ChunkBody& body,
                std::int32_t worker) {
  const std::int64_t chunks = chunk_count(begin, end, grain);
  for (std::int64_t c = 0; c < chunks; ++c) {
    body(chunk_at(begin, end, grain, c, worker));
  }
}

}  // namespace

struct ThreadPool::Impl {
  // One parallel_for call; lives on the caller's stack until every worker has left it.
  struct Job {
    const ChunkBody* body = nullptr;
    std::int64_t begin = 0;
    std::int64_t end = 0;
    std::int64_t grain = 0;
    std::int64_t chunks = 0;
    std::atomic<std::int64_t> next{0};  // next unclaimed chunk
  };

  // Claims chunks in index order until none is left.
  static void run_chunks(Job& job, std::int32_t worker) {
    for (;;) {
      const std::int64_t c = job.next.fetch_add(1, std::memory_order_relaxed);
      if (c >= job.chunks) {
        return;
      }
      (*job.body)(chunk_at(job.begin, job.end, job.grain, c, worker));
    }
  }

  void worker_loop(std::int32_t worker) noexcept {
    const WorkerScope scope(this, worker);
    std::uint64_t seen = 0;
    std::unique_lock lock(mutex);
    for (;;) {
      wake.wait(lock, [&] { return stop || generation != seen; });
      if (stop) {
        return;
      }
      seen = generation;
      Job* current = job;
      if (current == nullptr) {
        continue;  // the job finished before this worker woke up
      }
      ++active;
      lock.unlock();
      run_chunks(*current, worker);
      lock.lock();
      if (--active == 0) {
        done.notify_all();
      }
    }
  }

  std::vector<std::thread> threads;
  std::mutex submit_mutex;  // one job at a time when several threads share the pool
  std::mutex mutex;         // guards job, generation, active and stop
  std::condition_variable wake;
  std::condition_variable done;
  Job* job = nullptr;
  std::uint64_t generation = 0;  // incremented for every published job
  std::int32_t active = 0;       // pool threads currently inside job
  bool stop = false;
};

ThreadPool::ThreadPool() : impl_(std::make_unique<Impl>()) {}

ThreadPool::~ThreadPool() {
  {
    const std::scoped_lock lock(impl_->mutex);
    impl_->stop = true;
  }
  impl_->wake.notify_all();
  for (std::thread& t : impl_->threads) {
    t.join();
  }
}

Expected<std::unique_ptr<ThreadPool>> ThreadPool::create(std::int32_t threads) noexcept {
  if (threads < 0 || threads > kMaxThreads) {
    return make_error(StatusCode::invalid_argument, "ThreadPool: threads must be in [0, 256]");
  }
  std::int32_t count = threads;
  if (count == 0) {
    const unsigned hardware = std::thread::hardware_concurrency();
    count = hardware == 0 ? 1 : static_cast<std::int32_t>(std::min(hardware, 256U));
  }
  std::unique_ptr<ThreadPool> pool;
  try {
    pool.reset(new ThreadPool());  // NOLINT(modernize-make-unique): private constructor
    Impl& impl = *pool->impl_;
    impl.threads.reserve(static_cast<std::size_t>(count - 1));
    for (std::int32_t w = 1; w < count; ++w) {
      impl.threads.emplace_back(&Impl::worker_loop, &impl, w);
    }
  } catch (const std::bad_alloc&) {
    return make_error(StatusCode::out_of_memory, "ThreadPool: allocation failed");
  } catch (const std::system_error&) {
    // pool, if created, joins the threads started so far.
    return make_error(StatusCode::out_of_memory, "ThreadPool: cannot start a thread");
  }
  return {std::move(pool)};
}

std::int32_t ThreadPool::concurrency() const noexcept {
  return static_cast<std::int32_t>(impl_->threads.size()) + 1;
}

void ThreadPool::parallel_for(std::int64_t begin, std::int64_t end, std::int64_t grain,
                              ChunkBody body) noexcept {
  WG_ASSERT(grain > 0 && begin <= end, "parallel_for needs grain > 0 and begin <= end");
  Impl& impl = *impl_;
  if (tl_pool == &impl) {
    run_inline(begin, end, grain, body, tl_worker);  // nested call from one of our chunks
    return;
  }
  const std::scoped_lock submit(impl.submit_mutex);
  const WorkerScope scope(&impl, 0);
  const std::int64_t chunks = chunk_count(begin, end, grain);
  if (chunks <= 1 || impl.threads.empty()) {
    run_inline(begin, end, grain, body, 0);
    return;
  }

  Impl::Job job;
  job.body = &body;
  job.begin = begin;
  job.end = end;
  job.grain = grain;
  job.chunks = chunks;
  {
    const std::scoped_lock lock(impl.mutex);
    impl.job = &job;
    ++impl.generation;
  }
  impl.wake.notify_all();
  Impl::run_chunks(job, 0);

  // Every chunk is claimed; wait for the pool threads still running one. A thread that wakes
  // after this sees no job. The mutex makes their writes visible to the caller.
  std::unique_lock lock(impl.mutex);
  impl.done.wait(lock, [&] { return impl.active == 0; });
  impl.job = nullptr;
}

}  // namespace wg::core
