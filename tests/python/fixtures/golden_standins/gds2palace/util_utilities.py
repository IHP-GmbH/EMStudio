import os

from _standin_common import get_basename, get_script_path, record  # noqa: F401


def create_sim_path(script_path, model_basename, dirname='palace_model'):
    path = os.path.join(script_path, dirname, model_basename + '_data')
    record("create_sim_path", path=path)
    return path


def create_run_script(destination_path):
    record("create_run_script", destination_path=destination_path)
