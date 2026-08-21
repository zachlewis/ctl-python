// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#pragma once

namespace ctlpython {

// Acquire the GIL briefly and check for pending Python signals (e.g. SIGINT).
// If a signal is pending, throws nanobind::python_error() — nanobind will
// then propagate the underlying Python exception (e.g. KeyboardInterrupt)
// back across the C++ → Python boundary.
//
// Caller MUST be the C++ thread that owns the Python interpreter (typically
// the thread that called into the extension). Spawned worker threads must
// NOT call this function — only worker 0 (which runs on the calling thread)
// is safe.
void check_interrupt();

}
