# Test stand-in for the openEMS Python package: records what the model configures.
import json
import os


def _record(kind, **values):
    path = os.environ.get("CONVERT_RECORD")
    if path:
        with open(path, "a") as f:
            f.write(json.dumps({"kind": kind, **values}, sort_keys=True) + "\n")


class openEMS:
    def __init__(self, EndCriteria=None, **kwargs):
        _record("openEMS", end=EndCriteria)

    def SetGaussExcite(self, f0, fc):
        _record("gauss", f0=f0, fc=fc)

    def SetBoundaryCond(self, bc):
        _record("boundary", bc=list(bc))
