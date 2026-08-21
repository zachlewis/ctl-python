// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include "cache.hpp"
#include "exceptions.hpp"
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <CtlInterpreter.h>
#include <Iex.h>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>

namespace ctlpython {

InterpCache& InterpCache::instance() {
    static InterpCache c;
    return c;
}

static std::chrono::nanoseconds file_mtime(const std::string& path) {
    auto t = std::filesystem::last_write_time(path);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch());
}

// Parse top-level `import "Name";` directives from a CTL source file.
// Limitation: only matches one-import-per-line form (no multi-line imports,
// no comment-suppressed imports). This is sufficient for all real-world CTL
// files (ACES, etc.) which use the canonical single-line form.
static std::vector<std::string> parse_imports(const std::string& path) {
    std::vector<std::string> imports;
    std::ifstream f(path);
    if (!f) return imports;
    std::string line;
    // Match: optional whitespace, "import", whitespace, quoted name, optional ;
    std::regex pat(R"(^\s*import\s+\"([^\"]+)\"\s*;?\s*$)");
    while (std::getline(f, line)) {
        std::smatch m;
        if (std::regex_match(line, m, pat)) {
            imports.push_back(m[1]);
        }
    }
    return imports;
}

// Resolve an import name like "Lib.helper" to an absolute path
// "<dir>/Lib.helper.ctl" by searching the provided directories in order.
// Returns empty string if not found.
static std::string resolve_import(
    const std::string& name,
    const std::vector<std::string>& search_paths
) {
    for (const auto& dir : search_paths) {
        auto candidate = std::filesystem::path(dir) / (name + ".ctl");
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate).string();
        }
    }
    return "";
}

std::vector<std::string> InterpCache::env_paths() {
    std::vector<std::string> out;
    const char* env = std::getenv("CTL_MODULE_PATH");
    if (!env || !*env) return out;
    std::string s(env);
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        if (end > start) out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

void InterpCache::set_module_paths(std::vector<std::string> paths) {
    std::lock_guard<std::mutex> lock(mu_);
    user_paths_ = std::move(paths);
}

std::shared_ptr<Ctl::SimdInterpreter> InterpCache::get_or_load(const std::string& path) {
    std::string abs;
    try {
        abs = std::filesystem::absolute(path).string();
    } catch (const std::filesystem::filesystem_error& e) {
        throw ParseError(std::string("path resolution failed: ") + e.what());
    }

    std::chrono::nanoseconds main_mt;
    try {
        main_mt = file_mtime(abs);
    } catch (const std::filesystem::filesystem_error& e) {
        throw ParseError(std::string("CTL file not found: ") + e.what());
    }

    // ponytail: one lock over lookup+load serializes cold-cache compiles of
    // distinct files too; per-entry locks if that ever measurably matters.
    std::lock_guard<std::mutex> lock(mu_);

    auto it = map_.find(abs);
    if (it != map_.end() && it->second.main_mtime == main_mt) {
        // Also verify all transitively imported files are unchanged.
        bool stale = false;
        for (const auto& [imp_path, imp_mt] : it->second.imports) {
            try {
                if (file_mtime(imp_path) != imp_mt) { stale = true; break; }
            } catch (const std::filesystem::filesystem_error&) {
                stale = true;  // import file deleted or inaccessible
                break;
            }
        }
        if (!stale) return it->second.interp;
    }
    map_.erase(abs);

    // Combine user paths (first) with env paths, then set globally before loadFile.
    // NOTE: module-path changes do NOT invalidate existing cache entries; call
    // cache_clear() after changing paths to force reload of already-cached files.
    auto combined = user_paths_;
    auto env = env_paths();
    combined.insert(combined.end(), env.begin(), env.end());
    Ctl::Interpreter::setModulePaths(combined);

    auto entry = CachedInterp{
        std::make_shared<Ctl::SimdInterpreter>(),
        main_mt,
        {},   // imports — populated below
        abs
    };

    try {
        entry.interp->loadFile(abs);
    } catch (const Iex::BaseExc& e) {
        throw ParseError(std::string("CTL parse error: ") + e.what());
    }

    // Walk imports transitively using a BFS queue.
    // Cycle protection: skip already-visited paths; bail after 100 unique files.
    std::set<std::string> visited;
    std::vector<std::string> queue = {abs};
    while (!queue.empty()) {
        std::string cur = queue.back();
        queue.pop_back();
        if (visited.count(cur)) continue;
        visited.insert(cur);
        if (visited.size() > 100) break;  // sanity cap against runaway graphs

        auto imp_names = parse_imports(cur);
        for (const auto& name : imp_names) {
            std::string resolved = resolve_import(name, combined);
            if (resolved.empty() || visited.count(resolved)) continue;
            try {
                auto mt = file_mtime(resolved);
                entry.imports.emplace_back(resolved, mt);
                queue.push_back(resolved);
            } catch (const std::filesystem::filesystem_error&) {
                // Import unresolvable by us — CTL's loadFile would already
                // have thrown if the import was actually required.
            }
        }
    }

    auto [iter, _] = map_.emplace(abs, std::move(entry));
    return iter->second.interp;
}

nanobind::list InterpCache::info() const {
    std::lock_guard<std::mutex> lock(mu_);
    nanobind::list out;
    for (const auto& [_, e] : map_) {
        nanobind::dict d;
        d["path"] = e.path;
        // mtime_ns is the file_time_type epoch on the host platform (Unix
        // epoch on macOS/Linux, FILETIME epoch on Windows). Treat as opaque;
        // do not interpret cross-platform.
        d["mtime_ns"] = static_cast<int64_t>(e.main_mtime.count());
        out.append(d);
    }
    return out;
}

void InterpCache::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear();
}

}

void register_cache(nanobind::module_& m) {
    m.def("cache_info", []{ return ctlpython::InterpCache::instance().info(); });
    m.def("cache_clear", []{ ctlpython::InterpCache::instance().clear(); });
    m.def("set_module_paths", [](const std::vector<std::string>& paths) {
        ctlpython::InterpCache::instance().set_module_paths(paths);
    });
}
