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

# MODEL FOR openEMS - IHP SG13G2 CMIM (2.3 um) + TM1.a fix
#
# Same GDS/ports as palace_cmim_tm1fix.py, but openEMS FDTD workflow.
# Stackup: SG13G2_200um_openems.xml (MIM equiv. 0.1 um / er=16.87).
#
# Needs openems_ihp_sg13g2 "modules" next to this script (or on PYTHONPATH),
# and Python with openEMS (Windows wheels: 3.10/3.11).

import os
import sys

_script_dir = os.path.dirname(os.path.abspath(__file__))
# Parent of the "modules" package (EMStudio also adds this via PYTHONPATH).
sys.path.insert(0, _script_dir)
from modules import *

from openEMS import openEMS
import numpy as np


# ======================== workflow settings ================================
settings = {}

settings['preview_only'] = False
settings['postprocess_only'] = False
settings['no_gui'] = True  # skip AppCSXCAD when running from EMStudio

# ===================== input files and path settings =======================

gds_filename = "cmim_2u3_tm1fix.gds"
cellname = "TOP"
XML_filename = "SG13G2_200um_openems.xml"

settings['purpose'] = [0]
settings['preprocess_gds'] = True
settings['merge_polygon_size'] = 0.5

script_path = utilities.get_script_path(__file__)
model_basename = utilities.get_basename(__file__)
sim_path = utilities.create_sim_path(script_path, model_basename)
print('Simulation data directory: ', sim_path)

os.chdir(_script_dir)

# ======================== simulation settings ================================

settings['unit'] = 1e-06
settings['margin'] = 20
settings['air_around'] = 20

settings['fstart'] = 1000000000
settings['fstop'] = 100000000000
settings['numfreq'] = 401

settings['refined_cellsize'] = 0.5
settings['cells_per_wavelength'] = 20
settings['energy_limit'] = -40

# 'PEC' / 'PMC' / 'MUR' / 'PML_8'
settings['Boundaries'] = ['PEC', 'PEC', 'PEC', 'PEC', 'PEC', 'PEC']

# Same via ports as palace_cmim_tm1fix
simulation_ports = simulation_setup.all_simulation_ports()
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, voltage=1, port_Z0=50, source_layernum=201, from_layername='Metal1', to_layername='TopMetal1', direction='z'))
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, voltage=1, port_Z0=50, source_layernum=202, from_layername='Metal1', to_layername='Metal5', direction='z'))
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(XML_filename)
layernumbers = metals_list.getlayernumbers()
layernumbers.extend(simulation_ports.portlayers)

allpolygons = gds_reader.read_gds(
    gds_filename,
    layernumbers,
    purposelist=settings['purpose'],
    metals_list=metals_list,
    preprocess=settings['preprocess_gds'],
    merge_polygon_size=settings['merge_polygon_size'],
    cellname=cellname)

settings['simulation_ports'] = simulation_ports
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename

FDTD = openEMS(EndCriteria=np.exp(settings['energy_limit'] / 10 * np.log(10)))
FDTD.SetGaussExcite((settings['fstart'] + settings['fstop']) / 2,
                    (settings['fstop'] - settings['fstart']) / 2)
FDTD.SetBoundaryCond(settings['Boundaries'])

for port in simulation_ports.ports:
    settings['excite_portnumbers'] = [port.portnumber]
    simulation_setup.setupSimulation(FDTD=FDTD, settings=settings)
    simulation_setup.runSimulation(FDTD=FDTD, settings=settings)

if not settings['preview_only']:
    num_ports = simulation_ports.portcount
    s_params = np.empty((num_ports, num_ports, settings['numfreq']), dtype=object)
    f = np.linspace(settings['fstart'], settings['fstop'], settings['numfreq'])
    for i in range(1, num_ports + 1):
        for j in range(1, num_ports + 1):
            s_params[i - 1, j - 1] = utilities.calculate_Sij(
                i, j, f, sim_path, simulation_ports)
    snp_name = os.path.join(sim_path, model_basename + '.s' + str(num_ports) + 'p')
    utilities.write_snp(s_params, f, snp_name)
    print('Created S-parameter output file at ', snp_name)
