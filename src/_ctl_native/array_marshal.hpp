// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences
#pragma once
#include <nanobind/ndarray.h>
#include <vector>

namespace ctlpython {

struct Planes {
    std::vector<float> r, g, b;   // length = numSamples
    size_t numSamples;
};

// Strict (N, 3) float32 C-contiguous input; the Python wrapper (_api.py)
// normalizes dtype/contiguity, and nanobind implicitly converts anything
// else that reaches us directly.
using InputArray = nanobind::ndarray<const float, nanobind::shape<-1, 3>,
                                     nanobind::c_contig, nanobind::device::cpu>;

Planes split_n3_to_planes(InputArray a);
nanobind::ndarray<nanobind::numpy, float, nanobind::ndim<2>> planes_to_n3(const Planes& p);

}
