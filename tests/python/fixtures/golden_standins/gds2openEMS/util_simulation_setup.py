from _standin_common import all_simulation_ports, record, simulation_port  # noqa: F401

from . import util_meshlines


def setupSimulation(excite_portnumbers=None, simulation_ports=None, FDTD=None, materials_list=None,
                    dielectrics_list=None, metals_list=None, allpolygons=None, max_cellsize=None,
                    refined_cellsize=None, margin=None, unit=None,
                    z_mesh_function=util_meshlines.create_z_mesh,
                    xy_mesh_function=util_meshlines.create_xy_mesh_from_polygons, air_around=0,
                    field_dumps=False, settings=None, fill_factor_correction=False):
    record("setupSimulation", excite_portnumbers=(settings or {}).get('excite_portnumbers'),
           settings_keys=sorted((settings or {}).keys()))


def runSimulation(excite_portnumbers=None, FDTD=None, sim_path=None, model_basename=None,
                  preview_only=None, postprocess_only=None, force_simulation=False, no_gui=False,
                  numThreads=None, settings=None):
    record("runSimulation", excite_portnumbers=(settings or {}).get('excite_portnumbers'))
