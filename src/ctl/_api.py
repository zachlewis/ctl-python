"""Pythonic apply() entry point."""
from __future__ import annotations

from collections.abc import Sequence
from os import PathLike
from pathlib import Path
from typing import Any

import numpy as np

from ctl import _ctl_native
from ctl._errors import CtlParameterError, CtlParseError, CtlRuntimeError

PathLikeStr = str | PathLike[str]


def _coerce_transforms(transforms: PathLikeStr | Sequence[PathLikeStr]) -> list[str]:
    if isinstance(transforms, (str, PathLike)):
        transforms = [transforms]
    paths = [str(Path(t).resolve()) for t in transforms]
    if not paths:
        raise ValueError("transforms list is empty")
    return paths


def _coerce_input(a: np.ndarray) -> tuple[np.ndarray, str, tuple[int, ...], np.dtype]:
    """Return (n3_float32, kind, original_shape, original_dtype)."""
    orig_shape = a.shape
    orig_dtype = a.dtype

    if a.ndim == 1 and a.shape[-1] == 3:
        n3 = a.reshape(1, 3).astype(np.float32, copy=False)
        kind = "triplet"
    elif a.ndim == 1:
        n3 = np.repeat(a.reshape(-1, 1), 3, axis=1).astype(np.float32, copy=False)
        kind = "ramp"
    elif a.ndim == 2 and a.shape[-1] == 3:
        n3 = a.astype(np.float32, copy=False)
        kind = "n3"
    elif a.ndim == 3 and a.shape[-1] == 3:
        n3 = a.reshape(-1, 3).astype(np.float32, copy=False)
        kind = "image"
    else:
        raise ValueError(
            f"input must be (N,), (3,), (N,3), or (H,W,3); got shape {orig_shape}"
        )
    return n3, kind, orig_shape, orig_dtype


def _restore_shape(out_n3: np.ndarray, kind: str, orig_shape, orig_dtype) -> np.ndarray:
    if kind == "triplet":
        out = out_n3.reshape(3)
    elif kind == "ramp" or kind == "n3":
        out = out_n3
    elif kind == "image":
        out = out_n3.reshape(orig_shape)
    else:
        raise AssertionError(kind)
    return out.astype(orig_dtype, copy=False) if orig_dtype != np.float32 else out


def apply(
    input: np.ndarray,
    transforms: PathLikeStr | Sequence[PathLikeStr],
    *,
    default_alpha: float | None = 1.0,
    **params: Any,
) -> np.ndarray:
    """Apply one or more CTL transforms to an array.

    Parameters
    ----------
    input
        Image data. Accepted shapes: ``(3,)`` single triplet, ``(N,)`` neutral
        ramp (broadcast to R=G=B, output is ``(N, 3)``), ``(N, 3)`` row
        triplets, ``(H, W, 3)`` image.
    transforms
        One CTL path, or a sequence of paths to apply in order.
    default_alpha
        Value to broadcast into a varying float input named ``aIn`` when the
        CTL has no default and the user has not provided an override. The ACES
        convention is 1.0 (opaque alpha passthrough). Set to ``None`` to
        disable; a missing ``aIn`` then raises :class:`CtlParameterError`.
    **params
        Optional scalar parameter overrides matched by name.

    Returns
    -------
    np.ndarray
        Transformed array. Shape matches input where possible (``(N,)`` always
        promotes to ``(N, 3)``); dtype matches input.
    """
    a = np.asarray(input)
    n3, kind, shape, dtype = _coerce_input(a)
    # The native layer requires C-contiguous float32; no-op when already so.
    n3 = np.ascontiguousarray(n3)
    paths = _coerce_transforms(transforms)
    try:
        raw = _ctl_native.apply(n3, paths, params, default_alpha)
    except _ctl_native._NativeParseError as exc:
        raise CtlParseError(str(exc)) from exc
    except _ctl_native._NativeParameterError as exc:
        raise CtlParameterError(str(exc)) from exc
    except _ctl_native._NativeRuntimeErr as exc:
        raise CtlRuntimeError(str(exc)) from exc
    return _restore_shape(raw, kind, shape, dtype)
