import os
import sys

_FIXTURES = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _FIXTURES not in sys.path:
    sys.path.insert(0, _FIXTURES)

from _standin_common import all_simulation_ports, record, simulation_port  # noqa: E402,F401
from workflow_standins import api as _api  # noqa: E402


def create_palace(excite_ports, settings):
    record("create_palace", excite_ports=[p.portnumber for p in excite_ports],
           settings_keys=sorted(settings.keys()))
    return _api.create_palace(excite_ports, settings)
