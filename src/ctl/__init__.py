"""ctl-python — native Python bindings for CTL (Color Transformation Language)."""
from __future__ import annotations

import os

__version__ = "0.1.0"

from ctl import _ctl_native as _native  # noqa: F401
from ctl._api import apply
from ctl._cache import CacheEntry, cache_clear, cache_info, set_module_paths
from ctl._errors import CtlError, CtlParameterError, CtlParseError, CtlRuntimeError
from ctl._signature import CtlParam, CtlSignature, signature

__all__ = [
    "CacheEntry",
    "CtlError",
    "CtlParam",
    "CtlParameterError",
    "CtlParseError",
    "CtlRuntimeError",
    "CtlSignature",
    "apply",
    "cache_clear",
    "cache_info",
    "get_include",
    "get_lib",
    "set_module_paths",
    "signature",
]


def get_include() -> str:
    """Directory of the vendored CTL headers (``#include <CtlSimdInterpreter.h>``).

    Present in wheel installs only; editable builds serve headers from the
    CTL source tree instead. Linking also needs Imath/OpenEXR dev packages.
    """
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "include", "CTL")


def get_lib() -> str:
    """Directory of the vendored CTL static libraries (IlmCtl, IlmCtlSimd, IlmCtlMath)."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib")
