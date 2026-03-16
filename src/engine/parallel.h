#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstddef>

namespace bp {

// ---------------------------------------------------------------------------
// parallel_for — splits a range [0, count) across N threads.
//
// body(begin, end) is called once per chunk.  Chunks are contiguous and
// non-overlapping.  Thread count defaults to hardware_concurrency() (clamped
// to [1, 16]).  The calling thread participates as one of the workers to
// avoid an idle core.
//
// cancel: checked before each chunk starts; if true, remaining chunks are
// skipped but already-running chunks complete normally.
//
// Thread-safety requirements: body must be safe to call from multiple threads.
// Each invocation operates on a disjoint [begin, end) range, so no locking
// is needed as long as body only writes to elements within that range.
// ---------------------------------------------------------------------------
inline void parallel_for(
    size_t count,
    const std::atomic<bool>& cancel,
    const std::function<void(size_t begin, size_t end)>& body,
    int num_threads = 0)
{
    if (count == 0) return;

    if (num_threads <= 0)
        num_threads = std::max(1, (int)std::thread::hardware_concurrency());
    num_threads = std::min(num_threads, 16);
    num_threads = std::min(num_threads, (int)count);

    size_t chunk = (count + num_threads - 1) / num_threads;

    // Spawn n-1 threads; the calling thread takes the first chunk.
    std::vector<std::thread> threads;
    threads.reserve(num_threads - 1);

    for (int t = 1; t < num_threads; ++t) {
        size_t begin = t * chunk;
        size_t end   = std::min(begin + chunk, count);
        if (begin >= count) break;
        threads.emplace_back([&cancel, &body, begin, end]() {
            if (!cancel.load())
                body(begin, end);
        });
    }

    // Calling thread takes chunk 0
    if (!cancel.load())
        body(0, std::min(chunk, count));

    for (auto& t : threads)
        t.join();
}

} // namespace bp
