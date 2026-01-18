import pya
import subprocess
import os
import shutil
import sys


def _this_script_dir():
    # In KLayout, __file__ usually works. Keep safe fallback.
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except Exception:
        return os.getcwd()


def _is_windows():
    return os.name == "nt"


def find_emstudio_executable():
    """
    Find EMStudio executable in a robust cross-platform way:
      1) Use env var EMSTUDIO_EXE if set
      2) Try PATH: EMStudio (linux/mac), EMStudio.exe (win)
      3) Try common install locations
    Returns absolute path or None.
    """
    # 1) environment override
    env_exe = os.environ.get("EMSTUDIO_EXE", "").strip().strip('"')
    if env_exe and os.path.exists(env_exe):
        return env_exe

    # 2) PATH
    candidates = ["EMStudio.exe", "EMStudio"] if _is_windows() else ["EMStudio", "EMStudio.exe"]
    for name in candidates:
        p = shutil.which(name)
        if p:
            return p

    # 3) common locations
    if _is_windows():
        common = [
            os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "EMStudio", "EMStudio.exe"),
            os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "EMStudio", "EMStudio.exe"),
        ]
    else:
        common = [
            "/usr/local/bin/EMStudio",
            "/usr/bin/EMStudio",
            os.path.expanduser("~/bin/EMStudio"),
        ]

    for p in common:
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p

    return None


def find_icon_path(emstudio_path=None):
    """
    Search order:
      1) Next to this driver (same folder as klEmsDriver.py)
      2) ../icons next to the driver (typical repo/install layout: EMStudio/icons)
      3) Next to EMStudio executable (if provided)
    Returns path (with /) or empty string.
    """
    driver_dir = _this_script_dir()

    # 1) same dir as driver
    for fname in ("logo.png", "KLayout.png", "emstudio.png"):
        p = os.path.join(driver_dir, fname)
        if os.path.exists(p):
            return p.replace("\\", "/")

    # 2) ../icons relative to driver
    icons_dir = os.path.normpath(os.path.join(driver_dir, "..", "icons"))
    for fname in ("logo.png", "KLayout.png", "emstudio.png", "doxy.png"):
        p = os.path.join(icons_dir, fname)
        if os.path.exists(p):
            return p.replace("\\", "/")

    # 3) next to EMStudio executable
    if emstudio_path:
        em_dir = os.path.dirname(emstudio_path)
        for fname in ("logo.png", "KLayout.png", "emstudio.png"):
            p = os.path.join(em_dir, fname)
            if os.path.exists(p):
                return p.replace("\\", "/")

    return ""


def handler_func():
    mw = pya.Application.instance().main_window()
    view = mw.current_view()

    # Check if layout is loaded
    if not view or not view.active_cellview().layout():
        pya.MessageBox.critical("Error", "No layout is currently loaded in KLayout.", pya.MessageBox.Ok)
        return

    cell_view = view.active_cellview()
    topcell_name = cell_view.cell.name
    gdsfile_path = cell_view.filename()

    if not gdsfile_path:
        pya.MessageBox.critical(
            "Error",
            "Current layout has no filename.\nPlease save/export the layout to GDS first.",
            pya.MessageBox.Ok
        )
        return

    emstudio_path = find_emstudio_executable()
    if not emstudio_path:
        msg = (
            "Could not find EMStudio executable.\n\n"
            "Tried:\n"
            "- env var EMSTUDIO_EXE\n"
            "- PATH (EMStudio / EMStudio.exe)\n"
            "- common locations\n\n"
            "Fix options:\n"
            "1) Add EMStudio to PATH\n"
            "2) Or set EMSTUDIO_EXE to full path, e.g.:\n"
        )
        if _is_windows():
            msg += '   setx EMSTUDIO_EXE "C:\\Program Files (x86)\\EMStudio\\EMStudio.exe"\n'
        else:
            msg += '   export EMSTUDIO_EXE="/path/to/EMStudio"\n'
        pya.MessageBox.critical("Error", msg, pya.MessageBox.Ok)
        return

    # Clean environment (avoid python embedding conflicts)
    env = os.environ.copy()
    env.pop("PYTHONHOME", None)
    env.pop("PYTHONPATH", None)

    args = [
        emstudio_path,
        "-gdsfile", gdsfile_path,
        "-topcell", topcell_name
    ]

    try:
        subprocess.Popen(args, env=env)
    except Exception as e:
        pya.MessageBox.critical("Error", f"Failed to launch EMStudio:\n{str(e)}", pya.MessageBox.Ok)


# Register menu action
menu_handler = pya.Action()
menu_handler.title = "EMStudio"
menu_handler.on_triggered = handler_func

# Resolve icon path at load time (so toolbar icon is visible)
emstudio_path_for_icon = find_emstudio_executable()
icon_path = find_icon_path(emstudio_path_for_icon)
if icon_path:
    menu_handler.icon = icon_path

menu = pya.Application.instance().main_window().menu()
menu.insert_item("@toolbar.end", "menu_item_emstudio", menu_handler)
menu.insert_item("tools_menu.end", "menu_item_emstudio", menu_handler)

