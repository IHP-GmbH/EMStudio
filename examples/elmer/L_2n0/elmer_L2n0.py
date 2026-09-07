########################################################################
#
# Copyright 2025 Volker Muehlhaus and IHP PDK Authors
#
# Licensed under the GNU General Public License, Version 3.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.gnu.org/licenses/gpl-3.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
########################################################################

# MODEL FOR GMSH WITH ELMER (EM / S-parameters)
# Converted from workflow/palace_L2n0.py per Volker's userguide:
#   create_palace → create_elmer, create_run_script → create_elmer_run_script

import os
import sys
import subprocess

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), 'gds2palace')))
from gds2palace import *

# Model comments
#
# Inductor model with rather coarse 5 micron refined_cellsize, for fast simulation.
# Elmer solves every frequency point (no adaptive sweep) — fstep is coarser than Palace.


# ======================== workflow settings ================================

start_simulation = False
run_command = ['./run_elmer']

# ===================== input files and path settings =======================

gds_filename = "L_2n0_twoport.gds"   # geometries
XML_filename = "SG13G2_200um.xml"    # stackup

preprocess_gds = True
merge_polygon_size = 2

script_path = utilities.get_script_path(__file__)
model_basename = utilities.get_basename(__file__)
sim_path = utilities.create_sim_path(script_path, model_basename)
print('Simulation data directory: ', sim_path)

modelDir = os.path.dirname(os.path.abspath(__file__))
os.chdir(modelDir)

# ======================== simulation settings ================================

settings = {}

settings['unit']   = 1e-6  # geometry is in microns
settings['margin'] = 150    # distance in microns from GDSII geometry boundary to simulation boundary
settings['air_around'] = 50

settings['fstart']  = 0e9
settings['fstop']   = 50e9
settings['fstep']   = 2.5e9

settings['refined_cellsize'] = 5  # mesh cell size in conductor region
settings['cells_per_wavelength'] = 10

settings['meshsize_max'] = 70  # microns, override cells_per_wavelength
settings['adaptive_mesh_iterations'] = 0

settings['order'] = 2
settings['iterative'] = False

settings['no_gui'] = False

simulation_ports = simulation_setup.all_simulation_ports()
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, voltage=1, port_Z0=50, source_layernum=201, from_layername='SUBGND', to_layername='TopMetal1', direction='z'))
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, voltage=1, port_Z0=50, source_layernum=202, from_layername='SUBGND', to_layername='TopMetal1', direction='z'))


# ======================== simulation ================================

materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(XML_filename)
layernumbers = metals_list.getlayernumbers()
layernumbers.extend(simulation_ports.portlayers)

allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], metals_list=metals_list, preprocess=preprocess_gds, merge_polygon_size=merge_polygon_size)

########### create model ###########

settings['simulation_ports'] = simulation_ports
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename

excite_ports = simulation_ports.all_active_excitations()
config_name, data_dir = simulation_setup.create_elmer(excite_ports, settings)

utilities.create_elmer_run_script(sim_path, settings)

if start_simulation:
    try:
        os.chdir(sim_path)
        subprocess.run(run_command, shell=True)
    except Exception:
        print(f"Unable to run Elmer using command ", run_command)
