import os
import sys

_FIXTURES = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _FIXTURES not in sys.path:
    sys.path.insert(0, _FIXTURES)

from _standin_common import all_simulation_ports, record, simulation_port  # noqa: E402,F401
from workflow_standins import api as _api  # noqa: E402

from . import util_meshlines


def setupSimulation(excite_portnumbers=None, simulation_ports=None, FDTD=None, materials_list=None,
                    dielectrics_list=None, metals_list=None, allpolygons=None, max_cellsize=None,
                    refined_cellsize=None, margin=None, unit=None,
                    z_mesh_function=util_meshlines.create_z_mesh,
                    xy_mesh_function=util_meshlines.create_xy_mesh_from_polygons, air_around=0,
                    field_dumps=False, settings=None, fill_factor_correction=False):
    record("setupSimulation", excite_portnumbers=(settings or {}).get('excite_portnumbers'),
           settings_keys=sorted((settings or {}).keys()))
    return _api.setupSimulation(
        excite_portnumbers=excite_portnumbers, simulation_ports=simulation_ports, FDTD=FDTD,
        materials_list=materials_list, dielectrics_list=dielectrics_list, metals_list=metals_list,
        allpolygons=allpolygons, max_cellsize=max_cellsize, refined_cellsize=refined_cellsize,
        margin=margin, unit=unit, z_mesh_function=z_mesh_function,
        xy_mesh_function=xy_mesh_function, air_around=air_around, field_dumps=field_dumps,
        settings=settings, fill_factor_correction=fill_factor_correction)


def runSimulation(excite_portnumbers=None, FDTD=None, sim_path=None, model_basename=None,
                  preview_only=None, postprocess_only=None, force_simulation=False, no_gui=False,
                  numThreads=None, settings=None):
    record("runSimulation", excite_portnumbers=(settings or {}).get('excite_portnumbers'))
    return _api.runSimulation(
        excite_portnumbers=excite_portnumbers, FDTD=FDTD, sim_path=sim_path,
        model_basename=model_basename, preview_only=preview_only,
        postprocess_only=postprocess_only, force_simulation=force_simulation, no_gui=no_gui,
        numThreads=numThreads, settings=settings)
