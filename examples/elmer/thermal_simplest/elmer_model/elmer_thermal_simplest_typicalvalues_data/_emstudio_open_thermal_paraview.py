from paraview.simple import *

vtu = r'C:/users/anton/documents/emstudio/examples/elmer/thermal_simplest/elmer_model/elmer_thermal_simplest_typicalvalues_data/thermal_results_t0001.vtu'
reader = OpenDataFile(vtu)
view = GetActiveViewOrCreate('RenderView')
display = Show(reader, view)
ColorBy(display, ('POINTS', 'temperature'))
display.RescaleTransferFunctionToDataRange(True, False)
display.SetScalarBarVisibility(view, True)
Render()
ResetCamera()
