// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include "interrupt.hpp"
#include <nanobind/nanobind.h>

namespace ctlpython {

void check_interrupt() {
    nanobind::gil_scoped_acquire g;
    if (PyErr_CheckSignals() != 0) {
        throw nanobind::python_error();
    }
}

}
