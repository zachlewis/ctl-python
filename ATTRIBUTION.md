# Attribution

## CTL

This package vendors and links against the CTL (Color Transformation Language)
reference implementation maintained by AMPAS / aces-aswf
(https://github.com/ampas/CTL, https://github.com/aces-aswf/CTL), licensed
under Apache-2.0. Full license text included in `build/_deps/ctl-src/LICENSE`
after a fetch-mode build.

## matlab-ctl

The architecture (process-scoped interpreter cache with mtime invalidation,
parameter override priority, tile-parallel compute, error mapping) is ported
from the matlab-ctl MEX (https://github.com/aforsythe/matlab-ctl). The C++ MEX
implementation served as the design reference for this binding.
