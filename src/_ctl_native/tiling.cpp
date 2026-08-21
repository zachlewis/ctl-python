// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include "tiling.hpp"
#include "interrupt.hpp"
#include "exceptions.hpp"
#include <nanobind/nanobind.h>
#include <Iex.h>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <exception>

namespace ctlpython {

void run_parallel_transform(
    Ctl::SimdInterpreter& interp,
    size_t numSamples,
    const float* in_r, const float* in_g, const float* in_b,
    float* out_r, float* out_g, float* out_b,
    const std::unordered_map<std::string, ScalarValue>& scalar_overrides,
    const std::unordered_map<std::string, double>& varying_overrides,
    std::optional<float> default_alpha
) {
    if (numSamples == 0) return;

    const size_t maxChunk = interp.maxSamples();
    if (maxChunk == 0) {
        throw RuntimeErr("CTL interpreter reported maxSamples() == 0");
    }
    const size_t numChunks = (numSamples + maxChunk - 1) / maxChunk;

    // Decide worker count: cap at available hardware threads and chunk count.
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    const size_t numWorkers = std::min<size_t>(hw, numChunks);

    // Construct one FunctionCall per worker on the calling (main) thread.
    // CTL's newFunctionCall holds its interpreter mutex but we avoid concurrent
    // calls to it entirely — all setup happens before any thread is spawned.
    struct Worker {
        Ctl::FunctionCallPtr fn;
    };
    std::vector<Worker> workers(numWorkers);
    for (auto& w : workers) {
        w.fn = interp.newFunctionCall("main");
        // Per-worker setup: setVarying on the RGB channels.
        w.fn->inputArg(0)->setVarying(true);
        w.fn->inputArg(1)->setVarying(true);
        w.fn->inputArg(2)->setVarying(true);
        w.fn->outputArg(0)->setVarying(true);
        w.fn->outputArg(1)->setVarying(true);
        w.fn->outputArg(2)->setVarying(true);
        // Bind scalar overrides on each worker's FunctionCall (independent objects).
        bind_scalar_overrides(*w.fn, scalar_overrides);
    }

    // Atomic chunk counter; workers grab indices with relaxed ordering
    // (chunks are independent — no inter-chunk data dependency).
    std::atomic<size_t> nextChunk{0};
    std::atomic<bool> failed{false};
    std::exception_ptr first_exception;
    std::mutex errorMutex;

    auto worker_loop = [&](size_t widx) {
        Ctl::FunctionCall& fn = *workers[widx].fn;
        try {
            while (true) {
                size_t cidx = nextChunk.fetch_add(1, std::memory_order_relaxed);
                if (cidx >= numChunks) break;
                if (failed.load(std::memory_order_relaxed)) break;

                size_t off = cidx * maxChunk;
                size_t n = std::min(maxChunk, numSamples - off);

                std::memcpy(fn.inputArg(0)->data(), in_r + off, n * sizeof(float));
                std::memcpy(fn.inputArg(1)->data(), in_g + off, n * sizeof(float));
                std::memcpy(fn.inputArg(2)->data(), in_b + off, n * sizeof(float));
                apply_varying_broadcasts(fn, n, varying_overrides, default_alpha);
                fn.callFunction(n);
                std::memcpy(out_r + off, fn.outputArg(0)->data(), n * sizeof(float));
                std::memcpy(out_g + off, fn.outputArg(1)->data(), n * sizeof(float));
                std::memcpy(out_b + off, fn.outputArg(2)->data(), n * sizeof(float));

                // Worker 0 runs on the calling (Python main) thread — it may
                // briefly re-acquire the GIL to check for pending signals.
                // Spawned workers (widx > 0) never touch Python.
                if (widx == 0) {
                    ctlpython::check_interrupt();  // throws nanobind::python_error on Ctrl+C
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (!failed.exchange(true)) {
                first_exception = std::current_exception();
            }
        }
    };

    if (numWorkers <= 1) {
        worker_loop(0);
    } else {
        // Worker 0 runs inline on the calling thread; spawn the rest.
        std::vector<std::thread> threads;
        threads.reserve(numWorkers - 1);
        for (size_t w = 1; w < numWorkers; ++w) {
            threads.emplace_back(worker_loop, w);
        }
        worker_loop(0);
        for (auto& t : threads) t.join();
    }

    if (first_exception) {
        try {
            std::rethrow_exception(first_exception);
        } catch (nanobind::python_error&) {
            throw;  // preserve KeyboardInterrupt / any Python exception
        } catch (const Iex::BaseExc& e) {
            throw RuntimeErr(std::string("CTL: ") + e.what());
        } catch (const std::exception& e) {
            throw RuntimeErr(e.what());
        } catch (...) {
            throw RuntimeErr("unknown exception in CTL worker thread");
        }
    }
}

} // namespace ctlpython
