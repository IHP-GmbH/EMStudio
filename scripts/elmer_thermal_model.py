########################################################################
#
# Copyright 2025–2026 IHP Authors / Volker Muehlhaus (workflow patterns)
#
# Licensed under the GNU General Public License, Version 3.0
#
########################################################################

# Default EMStudio template: Elmer thermal (steady-state heat)

import os
import sys
import subprocess

from gds2palace import *

# Model comments
# Steady-state thermal simulation for Elmer FEM.
# Define heat sources / constant-temperature boundaries below (Thermal tab).

# ======================== workflow settings ================================

start_simulation = False
run_command = ['./run_elmer']

# ===================== input files and path settings =======================

gds_filename = ""
gds_cellname = ""
XML_filename = ""

preprocess_gds = True
merge_polygon_size = 0.5

script_path = utilities.get_script_path(__file__)
model_basename = utilities.get_basename(__file__)
sim_path = utilities.create_sim_path(script_path, model_basename, dirname='elmer_model')
print('Simulation data directory: ', sim_path)

modelDir = os.path.dirname(os.path.abspath(__file__))
os.chdir(modelDir)

# ======================== simulation settings ================================

settings = {}

settings['unit'] = 1e-6
settings['margin'] = 100
settings['refined_cellsize'] = 5
settings['meshsize_max'] = 100
settings['elmer_thermal'] = True
settings['no_gui'] = True

# Thermal objects from GDSII marker layers (filled by EMStudio Thermal tab)
thermal_objects = simulation_setup.all_thermal_objects()

# ======================== simulation ================================

variable_overrides = {}

materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(
    XML_filename, variable_overrides=variable_overrides)

layernumbers = metals_list.getlayernumbers()
layernumbers.extend(thermal_objects.layers)

allpolygons = gds_reader.read_gds(
    gds_filename,
    layernumbers,
    purposelist=[0],
    metals_list=metals_list,
    preprocess=preprocess_gds,
    merge_polygon_size=merge_polygon_size,
    cellname=gds_cellname)

########### create model ###########

settings['thermal_objects'] = thermal_objects
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename

config_name, data_dir = simulation_setup.create_elmer_thermal(settings)

if start_simulation:
    try:
        os.chdir(sim_path)
        subprocess.run(run_command, shell=True)
    except Exception:
        print('Unable to run Elmer using command ', run_command)
