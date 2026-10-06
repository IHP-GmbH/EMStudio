# Stand-in for the openEMS Python package (from openEMS import openEMS).
import os
import sys

_FIXTURES = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _FIXTURES not in sys.path:
    sys.path.insert(0, _FIXTURES)

from workflow_standins.api import openEMS as _OpenEMSBase  # noqa: E402
from _standin_common import record  # noqa: E402


class openEMS(_OpenEMSBase):
    def __init__(self, EndCriteria=None):
        super().__init__(EndCriteria=EndCriteria)
        record("openEMS", EndCriteria=EndCriteria)

    def SetGaussExcite(self, f0, fc):
        record("SetGaussExcite", f0=f0, fc=fc)

    def SetBoundaryCond(self, BC):
        record("SetBoundaryCond", BC=list(BC))
