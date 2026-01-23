# MODEL FOR GMSH WITH PALACE

import os
import sys
import subprocess

# sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), 'gds2palace')))
from gds2palace import *

# Model comments
#
# This is a generic model running port excitation for all ports defined below,
# to get full [S] matrix data.
# Output is stored to Touchstone S-parameter file.
# No data plots are created by this script.

# ======================== workflow settings ================================

# start solver after creating the model?
start_simulation = False
run_command = ['./run_sim']

# ===================== input files and path settings =======================

gds_filename = "line_simple_viaport.gds"   # geometries
gds_cellname = ""       # optional name of cell, empty string to load always top cell

XML_filename = "SG13G2_nosub.xml"          # stackup

# get path for this simulation file
script_path = utilities.get_script_path(__file__)

# use script filename as model basename
model_basename = utilities.get_basename(__file__)

# set and create directory for simulation output
sim_path = utilities.create_sim_path (script_path,model_basename)
print('Simulation data directory: ', sim_path)

# change path to models script path
modelDir = os.path.dirname(os.path.abspath(__file__))
os.chdir(modelDir)

# ======================== simulation settings ================================

settings = {}

settings['purpose'] = [0] # @brief Which GDSII data type is evaluated? Values in [] can be separated by comma
settings['preprocess_gds'] = False  # @brief  Preprocess GDSII for safe handling of cutouts/holes?
settings['merge_polygon_size'] = 0 #  @brief  Merge via polygons with distance less than .. microns, set to 0 to disable via merging.

settings['unit']   = 1e-6  # @brief Geometry units, 1E-6 is in microns
settings['margin'] = 50    # @brief Distance from GDSII geometry boundary to stackup boundary, in project units

settings['fstart']  = 0e9  # @brief start frequency [Hz]
settings['fstop']   = 100e9 # @brief stop frequency [Hz]
settings['fstep']   = 2.5e9 # @brief frequency step [Hz], adaptive frequency sweep is used 

settings['fpoint']  = [] # @brief optional: list of discrete frequencies fpr S-param, in addition to sweep, default is []
settings['fdump']   = [] # @brief optional: list of discrete frequencies for field dump file (Paraview), default is [] 

# optional: boundary condition ABC, PEC or PMC at X-,X+,Y-mY+,Z-,Z+ Default is absorbing boundary.
settings['boundary']=['ABC','ABC','ABC','ABC','ABC','ABC']

settings['refined_cellsize'] = 2  # @brief mesh cell size in conductor region, in project units, default is 2
settings['cells_per_wavelength'] = 10   # @brief  how many mesh cells per wavelength, must be 10 or more

settings['meshsize_max'] = 70  # @brief maximum absolute mesh size, in addition to cells_per_wavelength
settings['adaptive_mesh_iterations'] = 0  # @brief adaptive mesh iterations, default is 2

# Ports from GDSII Data, polygon geometry from specified special layer
# Excitations can be switched off by voltage=0, those S-parameter will be incomplete then

simulation_ports = simulation_setup.all_simulation_ports()
# instead of in-plane port specified with target_layername, we here use via port specified with from_layername and to_layername
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=1, voltage=1, port_Z0=50, source_layernum=201, from_layername='Metal1', to_layername='TopMetal2', direction='z'))
simulation_ports.add_port(simulation_setup.simulation_port(portnumber=2, voltage=1, port_Z0=50, source_layernum=202, from_layername='Metal1', to_layername='TopMetal2', direction='z'))


# ======================== simulation ================================

# get technology stackup data
materials_list, dielectrics_list, metals_list = stackup_reader.read_substrate (XML_filename)

# get list of layers from technology
layernumbers = metals_list.getlayernumbers()
layernumbers.extend(simulation_ports.portlayers)

# read geometries from GDSII, only purpose 0
allpolygons = gds_reader.read_gds(gds_filename,
                                  layernumbers,
                                  purposelist=settings['purpose'],
                                  metals_list=metals_list,
                                  preprocess=settings['preprocess_gds'],
                                  merge_polygon_size=settings['merge_polygon_size'],
                                  cellname=gds_cellname)


########### create model ###########

settings['simulation_ports'] = simulation_ports
settings['materials_list'] = materials_list
settings['dielectrics_list'] = dielectrics_list
settings['metals_list'] = metals_list
settings['layernumbers'] = layernumbers
settings['allpolygons'] = allpolygons
settings['sim_path'] = sim_path
settings['model_basename'] = model_basename


# list of ports that are excited (set voltage to zero in port excitation to skip an excitation!)
excite_ports = simulation_ports.all_active_excitations()
config_name, data_dir = simulation_setup.create_palace (excite_ports, settings)


# for convenience, write run script to model directory
utilities.create_run_script(sim_path)


if start_simulation:
    try:
        os.chdir(sim_path)
        subprocess.run(run_command, shell=True)
    except:
        print(f"Unable to run Palace using command ",run_command)