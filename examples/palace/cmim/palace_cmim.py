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

# MODEL FOR GMSH WITH PALACE - IHP SG13G2 CMIM (2.3 um x 2.3 um)
#
# DUT: small cmim from IHP-Open-PDK#493.
# Porting like Volker palace_rfcmim: long Metal1-referenced leads, ports at ends.
# De-embed: scripts/combine_extend_snp.py (negative port L from port_information.json).

import os
import sys
import subprocess

from gds2palace import *


# ======================== workflow settings ================================

start_simulation = False
run_command = ['./run_sim']

# ===================== input files and path settings =======================

gds_cellname = "TOP"
gds_filename = "cmim_2u3_flat.gds"
XML_filename = "SG13G2_200um.xml"
variable_overrides = {}

preprocess_gds = True
merge_polygon_size = 0

script_path = utilities.get_script_path(__file__)
model_basename = utilities.get_basename(__file__)
sim_path = utilities.create_sim_path(script_path, model_basename)
print('Simulation data directory: ', sim_path)

modelDir = os.path.dirname(os.path.abspath(__file__))
os.chdir(modelDir)

# ======================== simulation settings ================================

settings = {}

settings['unit'] = 1e-06
settings['margin'] = 20
settings['air_around'] = 20

settings['fstart'] = 1000000000
settings['fstop'] = 100000000000
settings['fstep'] = 2500000000
settings['fpoint'] = [1e9, 5e9, 10e9, 20e9, 50e9, 100e9]
settings['fdump'] = [10e9]

settings['refined_cellsize'] = 0.15
settings['cells_per_wavelength'] = 10
settings['meshsize_max'] = 70
settings['adaptive_mesh_iterations'] = 0

settings['no_gui'] = True

# ======================== ports (palace_rfcmim) ================================

simulation_ports = simulation_setup.all_simulation_ports()
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, voltage=1, port_Z0=50, source_layernum=201, from_layername='Metal1', to_layername='TopMetal1', direction='z'))
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, voltage=1, port_Z0=50, source_layernum=202, from_layername='Metal1', to_layername='Metal5', direction='z'))
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(XML_filename)
layernumbers = metals_list.getlayernumbers()
layernumbers.extend(simulation_ports.portlayers)

allpolygons = gds_reader.read_gds(gds_filename, layernumbers, purposelist=[0], metals_list=metals_list, preprocess=preprocess_gds, merge_polygon_size=merge_polygon_size)

settings['simulation_ports'] = simulation_ports
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename

excite_ports = simulation_ports.all_active_excitations()
config_name, data_dir = simulation_setup.create_palace(excite_ports, settings)

utilities.create_run_script(sim_path)

if start_simulation:
    try:
        os.chdir(sim_path)
        subprocess.run(run_command, shell=True)
    except Exception:
        print('Unable to run Palace using command ', run_command)
