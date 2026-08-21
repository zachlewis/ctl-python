import os

import pytest


def test_native_module_imports():
    from ctl import _ctl_native  # noqa: F401

def test_version_attribute():
    import ctl
    assert isinstance(ctl.__version__, str)

def test_sdk_paths():
    import ctl
    inc, lib = ctl.get_include(), ctl.get_lib()
    if not os.path.isdir(inc):
        pytest.skip("vendored CTL SDK is only present in wheel installs")
    assert os.path.isfile(os.path.join(inc, "CtlSimdInterpreter.h"))
    assert any("IlmCtl" in name for name in os.listdir(lib))
