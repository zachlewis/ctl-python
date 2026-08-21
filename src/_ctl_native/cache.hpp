// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#pragma once
#include <nanobind/nanobind.h>
#include <CtlSimdInterpreter.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace ctlpython {

struct CachedInterp {
    // shared_ptr: an in-flight apply() keeps its interpreter alive even if a
    // concurrent reload/cache_clear() evicts the entry (apply releases the
    // GIL, and free-threaded builds have no GIL to serialize on at all).
    std::shared_ptr<Ctl::SimdInterpreter> interp;
    std::chrono::nanoseconds main_mtime;
    // List of (absolute_path, mtime) for all transitively imported .ctl files.
    // Any mtime change invalidates this cache entry. The import list is internal;
    // only main_mtime is exposed via info().
    std::vector<std::pair<std::string, std::chrono::nanoseconds>> imports;
    std::string path;
};

class InterpCache {
public:
    static InterpCache& instance();
    std::shared_ptr<Ctl::SimdInterpreter> get_or_load(const std::string& path);
    nanobind::list info() const;
    void clear();
    void set_module_paths(std::vector<std::string> paths);
private:
    // Guards map_ and user_paths_ (and the setModulePaths+loadFile pair,
    // which flows through a CTL-global). Required for free-threaded CPython.
    mutable std::mutex mu_;
    std::unordered_map<std::string, CachedInterp> map_;
    std::vector<std::string> user_paths_;
    static std::vector<std::string> env_paths();
};

}

void register_cache(nanobind::module_& m);
