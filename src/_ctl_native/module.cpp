// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include <pybind11/pybind11.h>
#include "apply.hpp"
#include "exceptions.hpp"
#include "signature.hpp"
#include "cache.hpp"

namespace py = pybind11;

// mod_gil_not_used: safe to run without the GIL on free-threaded CPython.
// Shared state is the InterpCache (internally locked); CTL FunctionCalls are
// per-thread and tiling.cpp guards its own error channel.
PYBIND11_MODULE(_ctl_native, m, py::mod_gil_not_used()) {
    m.doc() = "ctl-python native bindings";

    py::register_exception<ctlpython::ParseError>(m, "_NativeParseError", PyExc_RuntimeError);
    py::register_exception<ctlpython::RuntimeErr>(m, "_NativeRuntimeErr", PyExc_RuntimeError);
    py::register_exception<ctlpython::ParameterError>(m, "_NativeParameterError", PyExc_ValueError);

    register_apply(m);
    register_signature(m);
    register_cache(m);
}
