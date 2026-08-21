// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include <nanobind/nanobind.h>
#include "apply.hpp"
#include "exceptions.hpp"
#include "signature.hpp"
#include "cache.hpp"

namespace nb = nanobind;

// Free-threaded support (Py_mod_gil slot) comes from nanobind_add_module's
// FREE_THREADED flag. Shared state is the InterpCache (internally locked);
// CTL FunctionCalls are per-thread and tiling.cpp guards its own error channel.
NB_MODULE(_ctl_native, m) {
    m.doc() = "ctl-python native bindings";

    nb::exception<ctlpython::ParseError>(m, "_NativeParseError", PyExc_RuntimeError);
    nb::exception<ctlpython::RuntimeErr>(m, "_NativeRuntimeErr", PyExc_RuntimeError);
    nb::exception<ctlpython::ParameterError>(m, "_NativeParameterError", PyExc_ValueError);

    register_apply(m);
    register_signature(m);
    register_cache(m);
}
