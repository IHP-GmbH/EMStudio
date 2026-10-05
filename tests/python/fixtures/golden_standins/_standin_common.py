"""Stand-ins for the gds2palace / gds2openEMS / openEMS calls the EMStudio templates make.

They keep the real function signatures (a wrong argument fails like in the real workflow) and
record every call as one JSON line in the file named by STANDIN_RECORD. Nothing is meshed or
simulated.
"""

import json
import os


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
class StackupLayers:
    def getlayernumbers(self):
        return [8, 10, 30]


def read_substrate(XML_filename, variable_overrides=None):
    record("read_substrate", XML_filename=XML_filename, variable_overrides=variable_overrides)
    return [], [], StackupLayers()


# gds_reader (same signature in gds2palace and gds2openEMS)
def read_gds(filename, layerlist, purposelist, metals_list, preprocess=False, merge_polygon_size=0,
             mirror=False, offset_x=0, offset_y=0, gds_boundary_layers=[], layernumber_offset=0,
             cellname="", derived_layers=None):
    record("read_gds", filename=filename, layerlist=list(layerlist), purposelist=purposelist,
           preprocess=preprocess, merge_polygon_size=merge_polygon_size, cellname=cellname)
    return []


# simulation_setup ports (same in both workflows)
class simulation_port:
    def __init__(self, portnumber, voltage, port_Z0, source_layernum, target_layername=None,
                 from_layername=None, to_layername=None, direction='x'):
        self.portnumber = portnumber
        self.voltage = voltage
        self.port_Z0 = port_Z0
        self.source_layernum = source_layernum
        self.target_layername = target_layername
        self.from_layername = from_layername
        self.to_layername = to_layername
        self.direction = direction


class all_simulation_ports:
    def __init__(self):
        self.ports = []
        self.portcount = 0
        self.portlayers = []

    def add_port(self, port):
        self.ports.append(port)
        self.portcount = len(self.ports)
        self.portlayers.append(port.source_layernum)

    def all_active_excitations(self):
        return [p for p in self.ports if p.voltage != 0]
