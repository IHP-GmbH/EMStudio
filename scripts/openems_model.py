import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), 'modules')))
from modules import *

from openEMS import openEMS
import numpy as np

# Model comments
#
# This is a generic model running port excitation for all ports defined below,
# to get full [S] matrix data.
# Output is stored to Touchstone S-parameter file.
# No data plots are created by this script.
#
# This model uses an alternative syntax where are settings are stored in settings dictionary,
# which makes it easier to implement future extensions without touching again the
# simulation model code


# ======================== workflow settings ================================
settings = {}


settings['preview_only'] = False  # @brief Enable this to preview model/mesh only, without starting simulation
settings['postprocess_only'] = False # @brief Enable this to show existing results only, without starting simulation

# ===================== input files and path settings =======================

gds_filename = ""
cellname = ""  # optional, set empty string "" to use top cell

XML_filename = ""

settings['purpose'] = [0] # @brief Which GDSII data type is evaluated? Values in [] can be separated by comma
settings['preprocess_gds'] = True  # @brief  Preprocess GDSII for safe handling of cutouts/holes?
settings['merge_polygon_size'] = 0.5 #  @brief  Merge via polygons with distance less than .. microns, set to 0 to disable via merging.


# get path for this simulation file
script_path = utilities.get_script_path(__file__)
# use script filename as model basename
model_basename = utilities.get_basename(__file__)
# set and create directory for simulation output
sim_path = utilities.create_sim_path(script_path, model_basename)
print('Simulation data directory: ', sim_path)

# change current path to model script path
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# ======================== simulation settings ================================

settings['unit']   = 1e-06  # @brief Geometry units, 1E-6 is in microns
settings['margin'] = 50    # @brief Distance from GDSII geometry boundary to simulation boundary, in project units 

settings['fstart']   = 0e9  # @brief start frequency [Hz]
settings['fstop']    = 110000000000 # @brief stop frequency [Hz]
settings['numfreq']  = 401  # @brief number of frequency steps [Hz]

settings['refined_cellsize'] = 2  # @brief mesh cell size in conductor region, in project units

# choices for boundary:
# 'PEC' : perfect electric conductor (default)
# 'PMC' : perfect magnetic conductor, useful for symmetries
# 'MUR' : simple MUR absorbing boundary conditions
# 'PML_8' : PML absorbing boundary conditions
settings['Boundaries'] = ['PEC', 'PEC', 'PEC', 'PEC', 'PEC', 'PEC']

settings['cells_per_wavelength'] = 20   # @brief how many mesh cells per wavelength, must be 10 or more
settings['energy_limit'] = -40          # @brief end criteria for residual energy (dB), default is -40

# port configuration, port geometry is read from GDSII file on the specified layer
simulation_ports = simulation_setup.all_simulation_ports()

# ======================== simulation ================================

# get technology stackup data
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate(XML_filename)
# get list of layers from technology
layernumbers = metals_list.getlayernumbers()
# we must also read the layers where we added ports, these are not included in technology layers
layernumbers.extend(simulation_ports.portlayers)

# read geometries from GDSII, only purpose 0
allpolygons = gds_reader.read_gds(gds_filename,
                                  layernumbers,
                                  purposelist=settings['purpose'],
                                  metals_list=metals_list,
                                  preprocess=settings['preprocess_gds'],
                                  merge_polygon_size=settings['merge_polygon_size'],
                                  cellname=cellname)


########### create model, run and post-process ###########

settings['simulation_ports'] = simulation_ports
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename

# define excitation and stop criteria and boundaries
FDTD = openEMS(EndCriteria=np.exp(settings['energy_limit']/10 * np.log(10)))
FDTD.SetGaussExcite((settings['fstart'] + settings['fstop'])/2,
                    (settings['fstop'] - settings['fstart'])/2)
FDTD.SetBoundaryCond(settings['Boundaries'])


########### create model, run and post-process ###########

# run all port excitations, one after another
for port in simulation_ports.ports:
    settings['excite_portnumbers'] = [port.portnumber]

    # prepare model from GDSII data
    simulation_setup.setupSimulation(FDTD=FDTD, settings=settings)  # must use named parameters when using settings dict!

    # preview model and start simulation
    simulation_setup.runSimulation(FDTD=FDTD, settings=settings)    # must use named parameters when using settings dict!


# Initialize an empty matrix for S-parameters
num_ports = simulation_ports.portcount
s_params = np.empty((num_ports, num_ports, settings['numfreq']), dtype=object)

# Define frequency resolution (postprocessing)
f = np.linspace(settings['fstart'], settings['fstop'], settings['numfreq'])

# Populate the S-parameter matrix with simulation results
for i in range(1, num_ports + 1):
    for j in range(1, num_ports + 1):
        s_params[i-1, j-1] = utilities.calculate_Sij(i, j, f, sim_path, simulation_ports)

# Write to Touchstone *.snp file
snp_name = os.path.join(sim_path, model_basename + '.s' + str(num_ports) + 'p')
utilities.write_snp(s_params, f, snp_name)

print('Created S-parameter output file at ', snp_name)
