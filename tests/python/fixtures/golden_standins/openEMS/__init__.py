# Stand-in for the openEMS Python package (from openEMS import openEMS).
from _standin_common import record


class openEMS:
    def __init__(self, **kwargs):
        record("openEMS", **kwargs)

    def SetGaussExcite(self, f0, fc):
        record("SetGaussExcite", f0=f0, fc=fc)

    def SetBoundaryCond(self, BC):
        record("SetBoundaryCond", BC=list(BC))
