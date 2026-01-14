#pragma once

//-----------------------------------------------------------------------------
// Cross-Platform Threading Abstractions for X-Ray Monolith Engine
//-----------------------------------------------------------------------------
// Provides unified API for parallel algorithms using platform-native libraries:
// - Windows: Microsoft PPL (Parallel Patterns Library) - part of MSVC runtime
// - Linux: Intel TBB (Threading Building Blocks) - requires libtbb-dev
//
// This abstraction enables data-parallel algorithms without platform-specific
// code in the engine. Use these primitives for CPU-bound parallel work like:
// - Texture loading during level load
// - AI cover point computation
// - Visibility culling
// - Any embarrassingly parallel computation
//
// For GPU command parallelism, use D3D11 deferred contexts instead (see MT Sun).
//
// Based on IX-Ray implementation, adapted for X-Ray Monolith engine.
//-----------------------------------------------------------------------------

#ifdef _MSC_VER
    // Windows - Use Microsoft PPL (Parallel Patterns Library)
    // PPL is part of Visual Studio runtime, no additional dependencies
    #include <ppl.h>
    #include <concurrent_vector.h>
    #include <concurrent_unordered_map.h>
#else
    // Linux/BSD - Use Intel TBB
    // Install: apt install libtbb-dev (Ubuntu/Debian)
    // CMake: find_package(TBB REQUIRED)
    #include <tbb/task_group.h>
    #include <tbb/parallel_for.h>
    #include <tbb/parallel_for_each.h>
    #include <tbb/blocked_range.h>
    #include <tbb/concurrent_vector.h>
    #include <tbb/concurrent_unordered_map.h>
#endif

#include <atomic>
#include <thread>

//-----------------------------------------------------------------------------
// Atomic Types
//-----------------------------------------------------------------------------
// Standard atomic type aliases for consistent naming across the codebase.
// Use these for lock-free counters, flags, and simple shared state.

using xr_atomic_u8    = std::atomic_uint8_t;
using xr_atomic_u16   = std::atomic_uint16_t;
using xr_atomic_u32   = std::atomic_uint32_t;
using xr_atomic_u64   = std::atomic_uint64_t;
using xr_atomic_s32   = std::atomic_int32_t;
using xr_atomic_s64   = std::atomic_int64_t;
using xr_atomic_bool  = std::atomic_bool;
using xr_atomic_float = std::atomic<float>;

//-----------------------------------------------------------------------------
// Task Group Abstraction
//-----------------------------------------------------------------------------
// Fire-and-forget task parallelism. Use for independent async work units.
//
// Usage:
//   xr_task_group tasks;
//   tasks.run([&]() { ProcessChunkA(); });
//   tasks.run([&]() { ProcessChunkB(); });
//   tasks.wait();  // Block until all complete

#ifdef _MSC_VER
    using xr_task_group = concurrency::task_group;
#else
    using xr_task_group = tbb::task_group;
#endif

//-----------------------------------------------------------------------------
// Concurrent Containers
//-----------------------------------------------------------------------------
// Thread-safe containers for accumulating results from parallel work.
// These support concurrent insertion without external locking.

#ifdef _MSC_VER
    template<typename T>
    using xr_concurrent_vector = concurrency::concurrent_vector<T>;

    template<typename K, typename V>
    using xr_concurrent_unordered_map = concurrency::concurrent_unordered_map<K, V>;
#else
    template<typename T>
    using xr_concurrent_vector = tbb::concurrent_vector<T>;

    template<typename K, typename V>
    using xr_concurrent_unordered_map = tbb::concurrent_unordered_map<K, V>;
#endif

//-----------------------------------------------------------------------------
// Parallel For (Index Range)
//-----------------------------------------------------------------------------
// Execute a function for each index in [Begin, End) in parallel.
// The functor receives the current index as its argument.
//
// Usage:
//   xr_parallel_for(0u, vertex_count, [&](u32 i) {
//       ProcessVertex(i);
//   });
//
// Notes:
//   - Iteration order is undefined
//   - Functor must be thread-safe (no shared mutable state without sync)
//   - Best for large ranges (1000+ iterations) to amortize overhead

template<typename IndexType, typename Functor>
inline void xr_parallel_for(IndexType Begin, IndexType End, Functor func)
{
#ifdef _MSC_VER
    concurrency::parallel_for(Begin, End, func);
#else
    using RangeType = tbb::blocked_range<IndexType>;
    tbb::parallel_for(RangeType(Begin, End), [&func](const RangeType& range) {
        for (IndexType i = range.begin(); i != range.end(); ++i) {
            func(i);
        }
    });
#endif
}

//-----------------------------------------------------------------------------
// Parallel For with Grain Size
//-----------------------------------------------------------------------------
// Same as above, but with explicit grain size for tuning.
// Grain size controls minimum iterations per task - larger values reduce
// task scheduling overhead but may leave cores idle at the end.
//
// Usage:
//   xr_parallel_for(0u, vertex_count, 1000u, [&](u32 i) {
//       ProcessVertex(i);  // At least 1000 vertices per task
//   });

template<typename IndexType, typename Functor>
inline void xr_parallel_for(IndexType Begin, IndexType End, IndexType Grain, Functor func)
{
#ifdef _MSC_VER
    // PPL parallel_for doesn't have direct grain size, but we can approximate
    // by using parallel_for with a step that processes Grain items at a time
    concurrency::parallel_for(Begin, End, func);
    // Note: PPL auto-partitions, grain size hint is ignored on Windows
#else
    using RangeType = tbb::blocked_range<IndexType>;
    tbb::parallel_for(RangeType(Begin, End, Grain), [&func](const RangeType& range) {
        for (IndexType i = range.begin(); i != range.end(); ++i) {
            func(i);
        }
    });
#endif
}

//-----------------------------------------------------------------------------
// Parallel For-Each (Iterator Range)
//-----------------------------------------------------------------------------
// Execute a function for each element in [Begin, End) in parallel.
// The functor receives a reference to the current element.
//
// Usage:
//   xr_parallel_foreach(textures.begin(), textures.end(), [](auto& tex) {
//       tex.Load();
//   });
//
// Notes:
//   - Iteration order is undefined
//   - Functor must be thread-safe
//   - Works with any forward iterator

template<typename Iterator, typename Functor>
inline void xr_parallel_foreach(Iterator Begin, Iterator End, Functor func)
{
#ifdef _MSC_VER
    concurrency::parallel_for_each(Begin, End, func);
#else
    tbb::parallel_for_each(Begin, End, func);
#endif
}

//-----------------------------------------------------------------------------
// Hardware Concurrency Query
//-----------------------------------------------------------------------------
// Returns the number of hardware threads available for parallel work.
// Use this to size thread pools or partition work appropriately.
//
// Usage:
//   size_t num_workers = xr_max_concurrency();
//   size_t chunk_size = total_work / num_workers;

inline size_t xr_max_concurrency()
{
    // Use standard C++ hardware_concurrency - works on all platforms
    unsigned int count = std::thread::hardware_concurrency();
    return (count > 0) ? static_cast<size_t>(count) : 1;
}

//-----------------------------------------------------------------------------
// Parallel Invoke (2-4 tasks)
//-----------------------------------------------------------------------------
// Execute 2-4 independent tasks in parallel and wait for all to complete.
// More efficient than task_group for small fixed numbers of tasks.
//
// Usage:
//   xr_parallel_invoke(
//       [&]() { ProcessA(); },
//       [&]() { ProcessB(); },
//       [&]() { ProcessC(); }
//   );

template<typename F1, typename F2>
inline void xr_parallel_invoke(F1&& f1, F2&& f2)
{
#ifdef _MSC_VER
    concurrency::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2));
#else
    tbb::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2));
#endif
}

template<typename F1, typename F2, typename F3>
inline void xr_parallel_invoke(F1&& f1, F2&& f2, F3&& f3)
{
#ifdef _MSC_VER
    concurrency::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3));
#else
    tbb::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2), std::forward<F3>(f3));
#endif
}

template<typename F1, typename F2, typename F3, typename F4>
inline void xr_parallel_invoke(F1&& f1, F2&& f2, F3&& f3, F4&& f4)
{
#ifdef _MSC_VER
    concurrency::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2),
                                  std::forward<F3>(f3), std::forward<F4>(f4));
#else
    tbb::parallel_invoke(std::forward<F1>(f1), std::forward<F2>(f2),
                          std::forward<F3>(f3), std::forward<F4>(f4));
#endif
}
