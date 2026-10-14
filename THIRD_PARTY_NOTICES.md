# Third-party notices

EMStudio itself is **GPL-3.0-or-later** (see [LICENSE](LICENSE)).
This file lists other components in the source tree and, where relevant,
components shipped with the CI / installer binaries.

This is an inventory for transparency, not legal advice.

## Included in the source tree

### QtPropertyBrowser (`QtPropertyBrowser/`)

Nokia / Qt Solutions property browser (settings grid and Preferences).

- Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies)
- Dual choice as stated in the file headers: **LGPL-2.1** with the
  [Nokia Qt LGPL Exception 1.1](QtPropertyBrowser/LGPL_EXCEPTION.txt),
  **or GPL-3.0**
- Licence texts: [LICENSE.LGPL](QtPropertyBrowser/LICENSE.LGPL),
  [LICENSE.GPL](QtPropertyBrowser/LICENSE.GPL),
  [LGPL_EXCEPTION.txt](QtPropertyBrowser/LGPL_EXCEPTION.txt)

EMStudio keeps local bug fixes in this copy (see `AGENTS.md` §8).

### Property editors (`extension/`)

File-path / colour / keyword editors and `VariantManager` /
`VariantFactory` used by the settings grid and Preferences.

These modules are distributed as part of EMStudio under **GPL-3.0-or-later**
(IHP Authors). They started from the Qt Quarterly “file path” property
editor sample (Trolltech 2006), which was **GPL-2.0-only** (or Qt
Commercial). The present sources have been extensively modified for
EMStudio (extra editor kinds, SciDoubleSpinBox, WSL-aware paths, HiDPI,
etc.) and no longer carry the GPL-2.0-only banner.

If a counsel review requires a strict clean-room rewrite of any remaining
shared structure, open a follow-up issue.

### Artwork

| File | Origin |
|---|---|
| `icons/klayout.ico`, `installer/KLayout.ico` | KLayout project artwork |
| `icons/doxy.png` | Doxygen artwork (if redistributed) |

## Shipped with binaries (Windows installer / Ubuntu portable)

Not all of these appear in every artifact.

| Component | Typical licence | Notes |
|---|---|---|
| Qt 5.15 (Core, Gui, Widgets, Network, Svg, plugins) | LGPL-3.0 | From aqt / system Qt |
| Qt Charts | GPL-3.0 (open-source edition) | Linked by EMStudio |
| ICU | ICU License | Ubuntu portable `libicu*` |
| ANGLE (`libEGL`, `libGLESv2`) | BSD | Windows deploy |
| Mesa `opengl32sw.dll` | MIT | Windows software GL |
| `D3Dcompiler_47.dll` | Microsoft redistributable | Windows |
| MinGW runtime (`libgcc`, `libstdc++`, `libwinpthread`) | GPL + GCC runtime exception / MIT | Windows |
| Bundled Field viewer Python (`field_viewer_python/`) | see below | Windows installer only |

### Field viewer Python bundle (Windows)

Built by `scripts/stage_field_viewer_python.ps1` from
`requirements-field-viewer.txt` (**PySide6-Essentials**, not full PySide6):

| Package | Typical licence |
|---|---|
| CPython embeddable | PSF |
| PySide6-Essentials / Shiboken6 | LGPL-3.0 |
| VTK | BSD-3-Clause |
| PyVista, pyvistaqt | MIT |
| NumPy, SciPy | BSD |
| Matplotlib | PSF-style |
| Pillow | HPND / MIT-CMU |
| qtpy, pooch, scooby, rich, … | various (BSD/MIT) |

## Used but not shipped

openEMS, Palace, Elmer, gds2palace, gds2openEMS, gmsh, KLayout and similar
solver / EDA tools are started from Preferences paths; they are not part of
the EMStudio binary artifacts (except when the user builds the optional
Docker image themselves).
