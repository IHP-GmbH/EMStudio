# Test stand-in for gds2openEMS util_gds_reader.py.
import json
import os


def read_gds(filename, layerlist, purposelist=[0], metals_list=None, preprocess=False,
             merge_polygon_size=0, mirror=False, offset_x=0, offset_y=0, gds_boundary_layers=[],
             layernumber_offset=0, cellname=None, derived_layers=None):
    path = os.environ.get("CONVERT_RECORD")
    if path:
        with open(path, "a") as f:
            f.write(json.dumps({"kind": "read_gds", "file": filename, "layers": layerlist,
                                "purpose": purposelist, "preprocess": preprocess,
                                "merge": merge_polygon_size, "cellname": cellname}) + "\n")
    return object()
