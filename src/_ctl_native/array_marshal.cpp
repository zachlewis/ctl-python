// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#include "array_marshal.hpp"
#include <stdexcept>

namespace nb = nanobind;

namespace ctlpython {

Planes split_n3_to_planes(InputArray a) {
    auto v = a.view();
    Planes p;
    p.numSamples = static_cast<size_t>(v.shape(0));
    p.r.resize(p.numSamples); p.g.resize(p.numSamples); p.b.resize(p.numSamples);
    for (size_t i = 0; i < p.numSamples; ++i) {
        p.r[i] = v(i, 0);
        p.g[i] = v(i, 1);
        p.b[i] = v(i, 2);
    }
    return p;
}

nb::ndarray<nb::numpy, float, nb::ndim<2>> planes_to_n3(const Planes& p) {
    float* data = new float[p.numSamples * 3];
    for (size_t i = 0; i < p.numSamples; ++i) {
        data[3 * i + 0] = p.r[i];
        data[3 * i + 1] = p.g[i];
        data[3 * i + 2] = p.b[i];
    }
    // The capsule owns the buffer; numpy holds it until the array dies.
    nb::capsule owner(data, [](void* ptr) noexcept {
        delete[] static_cast<float*>(ptr);
    });
    return nb::ndarray<nb::numpy, float, nb::ndim<2>>(
        data, {p.numSamples, 3}, owner);
}

}
