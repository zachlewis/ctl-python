// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include "apply.hpp"
#include "array_marshal.hpp"
#include "cache.hpp"
#include "exceptions.hpp"
#include "overrides.hpp"
#include "tiling.hpp"
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <CtlSimdInterpreter.h>
#include <CtlFunctionCall.h>
#include <Iex.h>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <optional>

namespace nb = nanobind;
using namespace ctlpython;

static nb::ndarray<nb::numpy, float, nb::ndim<2>> apply_impl(
    InputArray input,
    const std::vector<std::string>& transforms,
    const nb::dict& params,
    nb::object default_alpha
) {
    std::optional<float> alpha_default;
    if (!default_alpha.is_none()) {
        alpha_default = nb::cast<float>(default_alpha);
    }

    if (transforms.empty()) throw ParameterError("transforms list is empty");

    // Convert py::dict -> ScalarValue map once (path-independent).
    std::unordered_map<std::string, ScalarValue> scalar_overrides;
    for (auto item : params) {
        std::string name = nb::cast<std::string>(item.first);
        nb::handle v = item.second;
        ScalarValue sv;
        if (nb::isinstance<nb::bool_>(v)) {
            sv.tag = ScalarValue::B;
            sv.b   = nb::cast<bool>(v);
        } else if (nb::isinstance<nb::int_>(v)) {
            sv.tag = ScalarValue::I;
            sv.i   = nb::cast<long long>(v);
        } else if (nb::isinstance<nb::float_>(v)) {
            sv.tag = ScalarValue::F;
            sv.f   = nb::cast<double>(v);
        } else {
            throw ParameterError("override '" + name + "' must be int, float, or bool");
        }
        scalar_overrides.emplace(std::move(name), sv);
    }

    Planes planes = split_n3_to_planes(input);

    for (const auto& path : transforms) {
        try {
            // shared_ptr keeps the interpreter alive across the GIL-released
            // region below even if another thread evicts the cache entry.
            auto interp = ctlpython::InterpCache::instance().get_or_load(path);

            // Validation pass on the main thread, surfacing structural errors
            // and uniform/varying requirement failures before any worker is
            // spawned. The same FunctionCall feeds extract_varying_overrides;
            // workers will allocate their own.
            auto setup_fn = interp->newFunctionCall("main");
            if (setup_fn->numInputArgs() < 3 || setup_fn->numOutputArgs() < 3) {
                throw RuntimeErr(
                    "CTL main() must have at least 3 input and 3 output "
                    "varying float params (rIn/gIn/bIn -> rOut/gOut/bOut)");
            }
            ctlpython::bind_scalar_overrides(*setup_fn, scalar_overrides);
            ctlpython::validate_varying_required(*setup_fn, scalar_overrides, alpha_default);
            auto varying_overrides =
                ctlpython::extract_varying_overrides(*setup_fn, scalar_overrides);

            std::vector<float> rOut(planes.numSamples), gOut(planes.numSamples), bOut(planes.numSamples);

            {
                nb::gil_scoped_release release;
                ctlpython::run_parallel_transform(
                    *interp, planes.numSamples,
                    planes.r.data(), planes.g.data(), planes.b.data(),
                    rOut.data(), gOut.data(), bOut.data(),
                    scalar_overrides, varying_overrides, alpha_default
                );
            }  // GIL reacquired here

            planes.r = std::move(rOut);
            planes.g = std::move(gOut);
            planes.b = std::move(bOut);

        } catch (const Iex::BaseExc& e) {
            throw RuntimeErr(std::string("CTL: ") + e.what());
        }
    }
    return planes_to_n3(planes);
}

void register_apply(nb::module_& m) {
    m.def("apply", &apply_impl,
          nb::arg("input"), nb::arg("transforms"), nb::arg("params"),
          nb::arg("default_alpha").none() = 1.0);
}
