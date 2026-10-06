"""Stand-ins for the gds2palace / gds2openEMS / openEMS calls the EMStudio templates make.

Signatures come from ``workflow_standins.api`` (shared with convert_loose). Every call is
recorded as one JSON line in the file named by STANDIN_RECORD. Nothing is meshed or simulated.
"""

import json
import os
import sys

# fixtures/ is on PYTHONPATH next to this package (see test_golden_scripts.py).
_FIXTURES = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
if _FIXTURES not in sys.path:
    sys.path.insert(0, _FIXTURES)

from workflow_standins import api as _api  # noqa: E402


def record(call, **args):
    path = os.environ.get("STANDIN_RECORD")
    if not path:
        return
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps({"call": call, "args": args}, default=repr) + "\n")


# utilities
def get_script_path(filename):
    return os.path.dirname(os.path.abspath(filename))


def get_basename(filename):
    return os.path.splitext(os.path.basename(filename))[0]


# stackup_reader
StackupLayers = _api.StackupLayers


def read_substrate(XML_filename, variable_overrides=None):
    record("read_substrate", XML_filename=XML_filename, variable_overrides=variable_overrides)
    return _api.read_substrate(XML_filename, variable_overrides=variable_overrides)


# gds_reader (same signature in gds2palace and gds2openEMS)
def read_gds(filename, layerlist, purposelist, metals_list, preprocess=False, merge_polygon_size=0,
             mirror=False, offset_x=0, offset_y=0, gds_boundary_layers=None, layernumber_offset=0,
             cellname="", derived_layers=None):
    if gds_boundary_layers is None:
        gds_boundary_layers = []
    record("read_gds", filename=filename, layerlist=list(layerlist), purposelist=purposelist,
           preprocess=preprocess, merge_polygon_size=merge_polygon_size, cellname=cellname)
    return _api.read_gds(filename, layerlist, purposelist, metals_list, preprocess=preprocess,
                         merge_polygon_size=merge_polygon_size, mirror=mirror, offset_x=offset_x,
                         offset_y=offset_y, gds_boundary_layers=gds_boundary_layers,
                         layernumber_offset=layernumber_offset, cellname=cellname,
                         derived_layers=derived_layers)


# simulation_setup ports
simulation_port = _api.simulation_port
all_simulation_ports = _api.all_simulation_ports
