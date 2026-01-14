import pya
import subprocess
import os
import shutil

# Try to locate EMStudio.exe in system PATH
emstudio_path = shutil.which("EMStudio.exe")

# Determine icon path (logo.png in same dir)
icon_path = ""
if emstudio_path:
    emstudio_dir = os.path.dirname(emstudio_path)
    icon_path = os.path.join(emstudio_dir, "logo.png").replace("\\", "/")

# Handler to launch EMStudio with arguments
def handler_func():
    mw = pya.Application.instance().main_window()
    view = mw.current_view()

    # Check if layout is loaded
    if not view or not view.active_cellview().layout():
        pya.MessageBox.critical("Error", "No layout is currently loaded in KLayout.", pya.MessageBox.Ok)
        return

    # Get GDS file path and top cell name
    cell_view = view.active_cellview()
    topcell_name = cell_view.cell.name
    gdsfile_path = cell_view.filename()

    gds_dir = os.path.dirname(gdsfile_path)    

    env = os.environ.copy()
    env.pop("PYTHONHOME", None) 

    # Build command line arguments
    args = [
        emstudio_path,
        "-gdsfile", gdsfile_path,
        "-topcell", topcell_name
    ]

    # For debugging – show constructed arguments
    #pya.MessageBox.info("Launch Info", f"GDS file: {gdsfile_path}\n\nArguments:\n{' '.join(args)}", pya.MessageBox.Ok)

    try:
        subprocess.Popen(args, env=env)
    except Exception as e:
        pya.MessageBox.critical("Error", f"Failed to launch EMStudio:\n{str(e)}", pya.MessageBox.Ok)

# Register menu action
menu_handler = pya.Action()
menu_handler.title = "EMStudio"
menu_handler.on_triggered = handler_func
if icon_path and os.path.exists(icon_path):
    menu_handler.icon = icon_path

menu = pya.Application.instance().main_window().menu()
menu.insert_item("@toolbar.end", "menu_item_emstudio", menu_handler)
menu.insert_item("tools_menu.end", "menu_item_emstudio", menu_handler)
