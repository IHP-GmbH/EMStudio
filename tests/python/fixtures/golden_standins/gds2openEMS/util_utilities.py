import os

from _standin_common import get_basename, get_script_path, record  # noqa: F401


def create_sim_path(script_path, model_basename):
    path = os.path.join(script_path, 'output', model_basename)
    record("create_sim_path", path=path)
    return path


def calculate_Sij(i, j, f, sim_path, simulation_ports):
    record("calculate_Sij", i=i, j=j)
    return None


def write_snp(Smatrix, f, filename, z0=50):
    record("write_snp", filename=filename, numfreq=len(f), z0=z0)
