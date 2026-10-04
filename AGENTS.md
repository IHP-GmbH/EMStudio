# EMStudio – guide for AI coding agents

This file is for AI agents (and humans) who change EMStudio's code. It describes
how the code is organized, how data flows, and the rules that keep the build, the
tests and the generated Python scripts working. The user documentation is in
[README.md](README.md).

**Keep this file current.** When you change the architecture, add or move a
module, change a data flow, a file format, a build step or a test convention,
update the matching section here in the same change.

---

## 1. What EMStudio is

EMStudio is a Qt 5 (C++17) desktop GUI for setting up, running and analysing
electromagnetic and thermal simulations of IC layouts made with IHP PDKs. The GUI
doesn't simulate anything itself. It edits and generates a **Python model script**
for one of four solver workflows, runs that script with an external Python, then
starts the solver and shows the results:

| Tool key (`currentSimToolKey()`) | Workflow / Python package | Solver | Results |
|---|---|---|---|
| `openems` | IHP openEMS flow (`modules/`, `gds2openEMS`) | openEMS (FDTD), started by the Python script | Touchstone `.sNp` |
| `palace` | `gds2palace` (gmsh mesh + `config.json`) | AWS Palace (FEM). On Windows it runs inside WSL. | Palace CSV → `.sNp` |
| `elmer_em` | `gds2palace` in Elmer mode | ElmerSolver (VectorHelmholtz) | CSV → `.sNp` |
| `elmer_thermal` | `gds2palace` thermal | ElmerSolver (HeatSolve) | `thermal_results*.vtu`, shown in Layout Field |

The legacy key `elmer` maps to `elmer_em` (`normalizeSimToolKey`). Use
`isElmerFamilyKey` / `isElmerEmKey` / `isElmerThermalKey` (src/elmerThermalUi.cpp)
rather than comparing strings.

Inputs: a GDSII layout (`.gds`) with a top cell, an XML **stackup** (substrate)
file, ports drawn on special GDS layers, and the model script. The script must sit
next to the solver workflow folder (`modules/` for openEMS, `gds2palace/` for
Palace/Elmer) unless that package is installed with pip.

---

## 2. Repository map

```
src/                 application sources (one qmake/CMake target)
extension/           custom QtPropertyBrowser editors (file path, scientific double)
QtPropertyBrowser/   vendored Qt Solutions property browser (don't restyle; see §8)
scripts/             runtime scripts copied next to the binary (templates, helpers, viewers)
keywords/            <tool>.csv: settings keys with description, topic, default, required; workflow_signatures.csv
tests/               Qt Test suite (one binary), golden files, solver stubs
examples/            reference models (openEMS, Palace, Elmer, stackup XML samples)
doc/                 screenshots (png/) and tutorials used by README.md
installer/           Inno Setup script (Windows)
docker/              HPC image with EMStudio + Palace + openEMS
tools/               build/CI helpers (version, coverage, asset copy, icon)
windows/             Windows manifest (DPI)
.github/workflows/   CI: build.yml (Linux, Ubuntu portable, tests, Windows)
EMStudio.pro         qmake app project  ─┐
emstudio_sources.pri shared source list  ├─ build files (see §7)
CMakeLists.txt       CMake build (own source list)
tests/tests.pro      test binary project
```

Untracked files in the repo root (`Makefile`, `*.o`, `moc_*`, `ui_*.h`,
`EMStudio`, `.qmake.stash`) come from in-source qmake builds. Never commit them.

---

## 3. Source modules (src/)

### 3.1 MainWindow: the hub, split across several files

`MainWindow` (src/mainwindow.h) owns almost all application state and the main UI
(`src/mainwindow.ui`, accessed as `m_ui->…`). Its implementation is spread across
several files by topic. Add a method to the file whose topic it belongs to:

| File | Topic |
|---|---|
| mainwindow.cpp | Construction, tabs, menus, settings grid, preferences, load/save/save-as, recent models (`addRecentPythonModel` on Open and on every successful Save), ports table, GDS/stackup inputs, layout preview + Layout Field, KLayout launch, assistant tool registration, sanity checks |
| pythonToStudio.cpp | **Script → GUI**: `applyPythonScriptFromEditor()` (write editor to file, re-parse, update GUI), settings grid rebuild, boundaries, property type inference |
| pythonToEditor.cpp | **GUI → script**: patch settings, paths, ports, stackup variable overrides and boundaries into the script text |
| runOpenEms.cpp | Run openEMS (one Python process) |
| runPalace.cpp | Run Palace / Elmer: Python stage, then solver stage; WSL, launcher scripts, MPI cores, simulation log, CSV→Touchstone |
| elmerThermalUi.cpp | Tool-key helpers, Ports↔Thermal tab switch, thermal objects table ↔ script, opening thermal results in Field view |
| convertloosemodel.cpp | File → Convert to Settings Dictionary: runs `scripts/convert_loose_to_settings.py`, confirmation dialog, verified backup, replace, reload |
| sidebysidediff.cpp | `SideBySideDiff`: original and changed Python side by side for that dialog. Rows come aligned from the converter's report (`rows`: line per side, 0 = filler); synced scrolling, syntax highlighting, line-number gutter, Previous/Next change. `changeShown` (original lines of the shown change) selects the variables assigned there in the dialog's table; a table row calls `goToLeftLine` |
| headless.cpp | `runHeadless()` for `-run -palace/-openems` |
| gdsreader.cpp / xmlreader.cpp | GDS cell/layer lists; stackup layer names for the port combos |
| tips.cpp | Load `keywords/<tool>.csv` (description, topic, default) and `workflow_signatures.csv`; merge with `# @brief` tips from the model |
| wslHelper.cpp | Path conversion and existence checks that work across Windows and WSL. The Simulation Tool combo (`refreshSimToolOptions` → `rebuildSimToolCombo`) checks WSL tool paths with one asynchronous `wsl.exe -- bash -s` call (`startWslToolProbe`, results in `m_wslExecChecks`): a cold WSL start can exceed any short timeout. Until it answers, those tools are listed provisionally; a check WSL doesn't answer (60 s) is reported as such, never as "not executable" |
| verification.cpp | `test*()` hooks, compiled only with `EMSTUDIO_TESTING` (see §6) |

Key members:

- `m_simSettings` (`QMap<QString,QVariant>`): the model's current settings.
  Generic keys mirror the script's `settings['key']` / top-level variables. Special
  keys: `Boundaries` (QVariantMap of X-/X+/…), `GdsFile`, `SubstrateFile`,
  `TopCell` / `gds_cellname` / `cellname`, `RunPythonScript`, `RunDir`,
  `StackupVariableOverrides`. The generic writer skips the structural ones
  (`keyIsExcludedForEm`, `shouldSkipPalaceSettingKey`).
- `m_preferences` (`QMap<QString,QVariant>`): app preferences (tool paths, Python
  interpreters, WSL distro, assistant config, viewer navigation style, recent models, …), persisted under
  QSettings group `Preferences`.
- `m_sysSettings`: misc. state (last dirs), QSettings group `SystemSettings`.
- `m_curPythonData` (`PythonParser::Result`): last parse of the model script,
  including where each key is written (`writeMode`: top-level vs dict) and which
  values were quoted strings (`quotedStrings`).
- `m_tabMap`: tab **title** → index. Code calls `showTab(m_tabMap["Substrate"])`
  etc., so renaming a tab title in `mainwindow.ui` breaks those lookups.
  Titles: Main, Substrate, Python, Ports (relabelled Thermal for Elmer Thermal),
  Simulate, Results, Fields.

QSettings: organization `EMStudio`, application `EMStudioApp` (on Linux:
`~/.config/EMStudio/EMStudioApp.conf`). Always open it with
`emstudioSettings()` (src/appsettings.h), never `QSettings("EMStudio", …)`
directly: test builds get a separate store there (§6). `saveSettings()` / `loadSettings()` in
mainwindow.cpp. `ASSISTANT_API_KEY` is stored encrypted (`securestore.cpp`, DPAPI
on Windows). On first launch `ToolAutoDetect::fillEmptyPreferences` fills empty
tool paths from `PATH`.

### 3.2 Model script parsing and synchronisation

This is the most fragile part of the app. The script is the source of truth:
the GUI parses it, shows its values, and patches values back into the text with
regular expressions. It never regenerates the whole script.

- **PythonParser** (pythonparser.{h,cpp}): regex-based, not a Python parser.
  It collects:
  - `settings['key'] = value` (any dict name),
  - simple top-level `name = literal` lines,
  - `# @brief` tooltip comments (model-specific; the templates have none, the keyword files carry the
    descriptions; the grid tooltip is the `@brief` text or else the keyword description, plus
    "Required." and "Default: …" from the keyword file),
  - GDS/XML file names and the top cell.

  Values become `bool`, `double`/`longlong`, or `QString`. A quoted literal is
  stored without its quotes; anything else (lists, expressions, `None`) is kept as
  raw text. `writeMode[key]` records whether the key is written as `TopLevel` or
  `DictAssign`. `quotedStrings` lists the keys whose value was a quoted literal.
- **Keyword files** `keywords/<tool>.csv` (`openems`, `palace`, `elmer_em`, `elmer_thermal`;
  Elmer falls back to `palace.csv` when its file is missing): tab-separated
  `keyword, description, topic, default, required`. File order is the grid order: topics in
  order of first appearance (Script control first), keys in file order. The default is
  the Python literal the workflow uses (`get_optional_setting` / `settings.get`), shown in
  the tooltip. `required` = `yes` marks keys the workflow needs: there is no "Required"
  topic, they stay in their topic and the grid draws their name in bold
  (`QtProperty::setModified`, which QtTreePropertyBrowser paints bold).
  `tools/sync_keywords.py` regenerates `workflow_signatures.csv` and checks the
  defaults against the gds2palace / gds2openEMS sources.
- **Settings grid** (Main tab, `m_propertyBrowser` + `m_variantManager`):
  `rebuildSimulationSettingsFromPalace()` creates one property per setting, inside a
  topic group (`m_settingTopicGroups`, unknown keys alphabetically under "Other").
  Settings are nested one level below "Simulation Settings": walk them with
  `forEachSimSettingProperty()`, never `m_simSettingsGroup->subProperties()`.
  Collapsed topics (`m_collapsedSettingTopics`) survive the rebuild on Save.
  Loose variables (older openEMS models) get the topic of the workflow parameter they
  are passed to: `PythonParser::bindWorkflowCalls` binds `setupSimulation`,
  `runSimulation`, `read_gds`, `read_substrate` arguments by position / keyword
  (`workflow_signatures.csv`), accepting only a variable with a single top-level
  literal assignment. This only affects topic, order and tooltip, never what is written.
- **Adding / removing settings** (grid strip "+ Add setting..." + filter; right-click on a topic or
  setting, `fillSettingsContextMenu`): `AddSettingDialog` (addsettingdialog.cpp) picks a keyword by
  topic or a custom one. `addSetting()` first syncs the grid into the editor, then
  `insertSettingIntoScript()` (pythonToEditor.cpp) inserts `dict['key'] = value` after the present
  key that comes last before it in keyword-file order (only column-0 statements are anchors,
  `PythonParser::statementEnd` spans multi-line values), and `reparseEditorIntoGrid()` re-reads the
  editor so the key gets a `writeMode`. Remove (`canRemoveSetting`: one top-level assignment, not
  required) deletes that statement; Reset writes the keyword-file default
  (`writeSettingValueToScript`). Models without a settings dict (loose variables) can't add.
  `inferPalacePropertyInfo()` picks the editor: numbers are always `Double` (for
  `SciDoubleSpinBox`), plus `Bool` and `String`. Elmer EM `fdump` is a checkbox; other tools show it as
  a list. A tool switch re-types that row (`retypeToolDependentSettings`: grid → editor → grid), the writer
  turns a checkbox value into a list for every tool (`[settings['fstop']]` / `[]`, nothing written when the
  script's list already agrees), never writes a bool over a non-bool script value, and the Run check
  (`collectSanityFindings`, code `fdump_bool`) flags `fdump = True/False` (gds2palace crashes on it).
  Text that looks like code (`foo.bar`, calls) and quoted strings containing `.`
  are hidden (`shouldSkipStringSelfReference`). Edits arrive in
  `onSimulationSettingChanged()` → `m_simSettings`.
- **GUI → script**: `syncGuiSettingsToPythonEditor()` / `loadPythonScriptToEditor()`
  call:
  - `applySimSettingsToScript` → `applyOneSettingToScript`, once per key. It
    replaces only the value, keeps trailing comments, and only touches keys
    already in `writeMode`. Text cells are written back only when they changed:
    quoted strings re-quoted, lists and expressions verbatim; for `fdump` a bare
    value is wrapped in `[...]`. Numbers and True/False are also written only when the value
    changed, so `1e9` keeps its spelling (golden files and templates rely on that).
  - `applyGdsAndXmlPaths`, `applyVariableOverridesToScript`, `applyBoundaries`.
    The Top Cell goes only into what the script's `read_gds(..., cellname=...)` uses
    (`PythonParser::readGdsCellRef`, `applyTopCellToScript`): a variable, a
    `settings['key']` or a literal. Without that argument nothing is written; the script then
    loads the GDS top cell (`top_level()[0]`, `m_gdsTopCell`), which is also the dropdown
    default. Loading reads the cell from the same place.
  - `replaceOrInsertPortSection(buildPortCodeFromGuiTable())`, or the thermal
    section for Elmer Thermal.
  - Tool-specific patches: `applyPalaceWorkflowToScript`,
    `applyElmerWorkflowToScript`, `applyElmerThermalWorkflowToScript`.
  - `forceStartSimulationOff` (EMStudio starts solvers, never the script).
- **Save** (`on_actionSave_triggered`): GUI → editor, then
  `applyPythonScriptFromEditor()`, which writes the file and **re-parses it**,
  replacing GUI state. A value that isn't written into the script is therefore
  lost on Save. Anything new in the grid must have a write path.
- **Load** (`loadPythonModel`): parse, then `detectPythonModelSimKey` (text
  markers such as `from openEMS import openEMS`, `create_elmer_thermal`) →
  `selectSimToolByKey`, rebuild the grid, set GDS/XML/top cell, ports or thermal
  table, then the layout preview.
  GDS/XML paths go through `resolveModelInputFile`: if the written path doesn't exist, a
  file with the same name next to the model is used (and written back on Save).
- **Ports**: GUI table `tblPorts` ↔ `simulation_ports.add_port(simulation_setup.simulation_port(...))`
  lines, which may span several lines (`findPortBlocks` balances parentheses).
  `PortInfo` holds one port. In-plane ports use only `to_layername`; via ports use
  from + to.
- **Thermal objects**: `add_heatsource` / `add_consttemp` blocks ↔ `m_tblThermalObjects`. The target column is a fixed dropdown of stackup layers
  (`fillThermalTargetCombo`, types from `m_subLayerTypes`, filled by `readSubstrateLayers`): heat
  sources offer conductors, constant temperatures sheets then conductors. A model target that isn't
  offered stays selected in red ("not in stackup" / "not a … layer"). Read the target with
  `thermalTargetOf()` (item data), never `currentText()`. `refreshThermalTargetCombos()` runs when
  the stackup changes.
- **Stackup variable overrides** (Substrate tab table, not in the grid): the dict that
  `stackup_reader.read_substrate(..., variable_overrides=X)` passes, X being a top-level
  variable, a `settings['key']` or an inline dict (`findOverridesDict`). Load and Save use
  that dict; a script without the argument gets a top-level `variable_overrides` and
  the argument only when the table has entries.

File → New (`setupNewModelMenu` / `newModel`, one entry per tool, greyed out when the tool isn't
in the tool list) selects the tool, clears the previous model's inputs (`clearModelInputs`: GDS, cells,
stackup and its view, overrides, Ports / Thermal tables, preview), inserts its template
(`generateDefaultModelScript`, also behind "Generate Default"), clears the model path so Save asks
for a new file, and opens Main.
The OpenEMS template imports an installed `gds2openEMS` and falls back to a local `modules`
folder; Save As asks for `modules` only when the script imports it and the OpenEMS Python lacks
the package (`validateRequiredFolderForSim`, `pythonHasModule`, cached per interpreter).
Templates for "Generate Default" are `scripts/openems_model.py`,
`palace_model.py`, `elmer_model.py`, `elmer_thermal_model.py`, found with
`resolveModelTemplatePath()` (preference `MODEL_TEMPLATES_DIR`, else
`<app>/scripts/`). Template edits change generated scripts and therefore the
golden tests (§6).

**Loose-variable → settings[] conversion** (File → Convert to Settings Dictionary, openEMS models
without a settings dict, `canConvertLooseModel`). The work is done by
`scripts/convert_loose_to_settings.py`, run asynchronously with preference `Python Path`
(`runLooseConverter`; stdlib only, Python ≥ 3.8):
- `--report model.py` prints a JSON plan (with `original_lines`, `converted_lines` and difflib-aligned
  `rows` for the side-by-side preview; character spans only where ≥ 70 % of the original line survives); `--write model.py --out tmp.py [--switch-imports]` writes
  only to `tmp.py`. EMStudio then copies the model to `<stem>_backup_<yyyyMMdd_HHmmss>.py`, compares
  the bytes, replaces the model with `QSaveFile` and reloads it.
- Nothing is guessed from names. A variable becomes `settings['key']` only when proven:
  - passed to `setupSimulation` / `runSimulation` (the key is the parameter name of the parsed
    signature) or to `read_gds` / `read_substrate` (keys from `workflow_signatures.csv`);
  - or used in the workflow's own expressions: `openEMS(EndCriteria=exp(X/10*log(10)))`,
    `SetGaussExcite((A+B)/2, (B-A)/2)`, `SetBoundaryCond(X)`, `linspace(fstart, fstop, X)`;
  - `cells_per_wavelength`: the `max_cellsize` formula must equal the one in the workflow's
    `setupSimulation`. The formula lines are dropped when only `setupSimulation` uses them.
  - Each converted variable must have exactly one binding: a top-level literal assignment.
- Workflow calls become `settings[param] = expr` lines plus `f(FDTD=FDTD, settings=settings)`.
  `name.method(call)` (e.g. `data_paths.append(runSimulation(...))`) is supported.
- The workflow the call actually uses is parsed (`modules/util_simulation_setup.py`, also via
  direct `import util_X` through `sys.path`, or the installed `gds2openEMS`). Its settings branch
  must read every written key, with equivalent defaults.
- Old `modules/` without `settings=` need `--switch-imports`, which rewrites the imports to the
  installed package. This is opt-in in the dialog, because the workflow version changes.
- Proof: the edited text must parse to exactly the AST of the intended transformation (and
  compile); otherwise the converter refuses and nothing is written. The converted model
  re-simulates once (runSimulation hashes the script).

### 3.3 Running simulations

`on_btnRun_clicked()` → sanity check dialog (`collectSanityFindings`,
sanitycheck.cpp) → `runOpenEMS()` or `runPalace()`. Both save first.

- **openEMS**: runs the model with preference `Python Path` in `RunDir` (default:
  the script's folder). Output streams into the Simulate log.
- **Palace / Elmer** (runPalace.cpp): `PalaceRunContext` is built by
  `buildPalaceRunContext`. Phases (`PalacePhase`):
  1. `PythonModel`: gds2palace writes the mesh and `config.json` (Palace) or `.sif`
     (Elmer).
  2. `PalaceSolver`: Palace runs as an executable (`PALACE_INSTALL_PATH`) or via a
     launcher script (`PALACE_RUN_MODE`, `PALACE_RUN_SCRIPT`). On Windows it runs
     inside WSL (`WSL_DISTRO`, wslHelper). Elmer runs natively (`ELMER_SOLVER_PATH`,
     `ELMER_PYTHON`).

  If the model has `preview_only = True`, the run ends after phase 1: gds2palace doesn't
  mesh then, so a solver would read a fresh config next to an old mesh.
  After the run, CSV results are converted with `scripts/combine_extend_snp.py`.
  Elmer Thermal opens the Fields page (`showRunControlPage("Fields")`).
- The simulation log is persisted as `emstudio_simulation.log` in the run's data
  folder, e.g. `palace_model/<model>_data/` (`simulationLogFilePath`,
  `persistSimulationLogSnapshot`, `loadSimulationLogFromDisk`). Loading a model restores
  its saved log; File → New clears the log (`clearSimulationLog(false)`).
- Headless: `EMStudio -run -palace|-openems model.py` (main.cpp → `runHeadless`)
  exits with a non-zero code on failure.

### 3.4 Other UI components

| Component | Files | Notes |
|---|---|---|
| Stackup model | substrate, layer, material, dielectric, stackupexpr | Parses and writes the XML stackup. Schema 3.x: Variables with expressions, derived layers, thermal tables. `resolve(overrides)` evaluates expressions. |
| Stackup cross-section | substrateview | 2.5D stack drawing; clicking a layer highlights it in the layout view |
| Stackup editor | stackupeditor | Dialog that edits the XML; emits saved → reload |
| Layout preview | gdslayout (flattening: own GDSII reader; STRANS reflection is about the X axis before the rotation, AREF corner points are origin + cols / rows pitches, element state including a TEXT's MAG is reset after every element; check changes against KLayout's flattening), layoutview, layoutlayerpanel | Like the workflows, the preview shows only the GDS datatypes of the model's `read_gds(purposelist=...)` (`PythonParser::readGdsPurposes`: literal, variable or `settings['key']`, keyword or 3rd positional argument; the grid value wins; unknown → datatype 0; `MainWindow::currentGdsPurposes`). `refreshLayoutPreviewIfPurposesChanged` rebuilds it after a model load and after Save's re-parse, since the GDS path is set before the editor holds the new script. One `LayoutView` + Layers panel (`m_layoutPaneSplit`) shared by two pages: `placeLayoutPane()` (called from `showTab`) moves it to **Substrate** (top view / Iso3D, Field off) or **Fields** (Field mode on, 2D; 3D opens the Field viewer), keeps Substrate's 2D/3D choice and each page's manual zoom (`LayoutView::viewState`). The view's Field button is hidden; Shift+F emits `fieldPageRequested`. Field mode isn't persisted. Opacity has two levels. Per layer (`layerOpacity` / `setLayerOpacity`, a selected row in the Layers panel) is the fill alpha of that layer. **Layout opacity** (`layoutOpacity` / `setLayoutOpacity`, the "All layers" slider) fades all fills as one image, so the slider looks the same however many layers overlap (thermal stacks: 20+ fills): in 2D every fill (layers and port / thermal marker areas) is a child of one `addFadeGroup` item (`QGraphicsOpacityEffect`), while a twin outline item on top (`kRoleOutline` = its pen, no brush) carries the name for clicks and the highlight; in Iso3D each run of faces between thermal surfaces is one faded group or pixmap (`kRoleFade`, `applyLayoutOpacity`). Lines, arrows and labels are never faded, so 0 % shows outlines. The layout view and Field mode keep their own layout opacity (`m_layoutOpacity` 1.0, `m_fieldLayoutOpacity` 0.0 = outlines in layer color over the heatmap); port / thermal marker lines and labels stay at full strength in Field mode (`markerOpacityFor`). The Layers panel re-reads both on a mode switch (`LayoutLayerPanel::refreshOpacities`). A right-click in the list doesn't change the selected row (it would retarget the slider); Show / Hide All and Hide Unmapped (`setListedVisible`) emit one `layersVisibilityChanged` → `LayoutView::setLayersVisible` (one Iso3D rebuild). The Layers panel lists stackup layers from top to bottom (mid-Z; `refreshLayoutPreview` builds the entries), layers without Z after them, port / thermal markers last. The Layers panel's "All layers" row is a check box over the listed layers (`setAllVisible`, state from `updateAllLayersCheck`: checked / partly / unchecked). The Layers panel lists port / thermal marker layers without stackup layers (no Ports row with From/To or target, no thermal object with a target) as "<name> (not mapped)" (`LayoutLayerPanel::Entry::unmapped`): the preview then draws them at a guessed position. Iso3D: dense scenes are one pre-rendered pixmap, so style changes rebuild the scene (`rebuildIso3dForStyleChange`); the sceneRect is a fixed square around the orbit center so orbiting doesn't move the scrollbars; pitch < 0 draws bottom caps and reverses the stack order. Port marker layers are GDS 201–299 unless the stackup defines that number (e.g. a `SUBGND` sheet on 250: `LayoutView::isPortLayerNumber`). Iso3D ports are drawn as the surface gds2palace builds (in-plane: bounding box at the target metal's bottom; via: vertical sheet on the xmin or ymin edge between the metals); via arrows point From → To, and `-z` reverses them. Elmer Thermal: the markers come from the Thermal table (`PortInfo::thermalKind`, named "Heat … W" / "T … K"), drawn without arrows; in Iso3D a heat source is the bounding box through the target layer and a constant temperature the polygon at the target's zmin and zmax, as gds2palace builds them; unlike EM port surfaces (always on top) these surfaces are sorted into the layer faces by height (heat source: target mid-Z; constant temperature: the sheet's z), and the dense-scene pixmap is split into one pixmap per run of faces between them; labels stay on top; clicking one selects its Thermal row. Via layers follow the model's `merge_polygon_size` (`MainWindow::currentViaMergeSize` → `LayoutView::setViaMergeSize`): > 0 merges them in 2D and 3D exactly like gds2palace's `merge_via_array` (grow spacing/2 + 0.01 µm, unite, shrink; `mergeViaArray`, QRegion on a 1 nm grid), so the preview shows the simulated geometry; 0 shows every via; not set: 2D every via, Iso3D its own display-only merge of dense via layers. A via layer with more drawn polygons than preference `LAYOUT_MAX_VIA_POLYGONS` (default 100, `setMaxViaPolygonsPerLayer`) replaces the 2D / 3D layout with a message label (`denseViaLayers`); a Field heatmap is still drawn. Mouse wheel / drag mapping depends on `NavStyle` (`LayoutView::setNavigationStyle`); keys in `LayoutView::handleViewKey`. |
| Navigation / key bindings | navigationstyle, keybindingsdialog | `NavStyle` EMStudio / setupEM, preference `VIEWER_NAV_STYLE` ("emstudio" / "setupem"). Setup → Key Bindings (`on_actionKeyBindings_triggered`) → `applyNavigationStyle()` sets the Layout preview and sends `{"nav_style": …}` to an open Field 3D viewer; new viewers get `--nav-style`. `NavigationStyle::bindingTable` is the one list of all bindings (dialog and tooltips): **change it together with LayoutView and `scripts/field_viewer.py`**. Window-wide shortcuts: menu actions in mainwindow.ui, F5 / Ctrl+1…7 in `setupGlobalShortcuts()`. |
| Layout Field | layoutview + mainwindow.cpp `*Field*` methods | All Python, run with host Python `FIELD_VIEWER_PYTHON`. `scripts/field_io.py` is the shared reader: discovery per source (`palace`, `elmer_em`, `elmer_thermal`, `openems` FD `_abs`/`_arg.vtr`), cycle-aware `.pvd` reading, frequency labels, units → µm, derived \|E\| magnitudes, grouped field list. EMStudio lists the run's files with `field_io.py --list` (`refreshFieldChoices` → the Field panel's file/cycle combo). The 2D slice is `field_slice_export.py --source --cycle` (Z-slice PNG plus meta JSON, no arrows; color limits in `_slice_clim`: `--log` spans the slice max down to the slice min, at most 40 dB). Field→3D starts `field_viewer.py --run-path --source --select-file --cycle --stdin-control`, a PySide6/pyvistaqt window ported from setupEM. A second 3D click sends JSON on the viewer's stdin, so the open window reloads / comes to the front. The viewer remaps mouse presses and swallows VTK's own letter keys in `FieldViewerWindow.eventFilter` (`--nav-style`, stdin `nav_style`). Status and error messages of the Fields page and viewers go to the Log window (`fieldLog`), not the simulation log, which holds solver output and is saved with the run. Spec: FIELD_VIEWER_SPEC.md (setupEM). |
| Results | resultsviewer, touchstone, smithchartwidget, resultscalculator, exprparser | Scans the run folder for `.sNp`; dB/phase/Smith (`SmithChartWidget`: own QPainter chart with grid labels as setupEM's result_viewer, click marker (readout left of the chart when wide; no own legend, the viewer's legend covers all charts) with f / Γ / Z readout from `addTrace(..., freqHz, z0)`); Compare; RF calculator (`cser($1)`, `ydiff_cser($1,$2)`, …); Model Fit via `snp2le` |
| Python editor | pythoneditor, pythonsyntaxhighlighter, finddialog | `editRunPythonScript` on the Python tab |
| Preferences | preferences (+ preferences.ui) | Property-browser dialog over `m_preferences` |
| About | about (+ about.ui) | Async version probes for tools; native on Linux, WSL on Windows |
| Add setting | addsettingdialog | Settings grid → Add setting: keyword list by topic (required bold, present greyed), default prefilled, custom keyword; `toPythonLiteral` |
| Keywords | keywordseditor, tips | Edit `keywords/*.csv`: keyword, description, topic (combo of the file's topics), default, required (yes/no combo). Description is shown last (header `moveSection`, view only); the model and file keep the file column order. Always saves tab-separated, keeping row order |
| Assistant | assistantchatpanel, assistantagent, assistantmcp, assistantpromptblob | Chat dock. `AssistantMcp` is an in-process tool registry; `MainWindow::registerAssistantMcpTools()` defines the tools (`get_app_state`, `load_model`, `run_simulation`, `set_preference`, …). `AssistantAgent` calls an OpenAI-compatible `/chat/completions` endpoint (`ASSISTANT_BASE_URL/MODEL/API_KEY`). `assistantpromptblob.cpp` holds an encoded policy fragment ("do not edit by hand"). |
| KLayout | mainwindow.cpp `*Klayout*`, scripts/klEmsDriver.py, klayout_*.{py,rb}, KLayout.sh/.bat | Opens the GDS in KLayout; the KLayout macro starts EMStudio with `-gdsfile/-topcell`; the assistant can modify GDS via KLayout batch |

Custom property editors (`extension/`): `VariantManager` / `VariantFactory` add a
file-path type (`FileEdit`) and use `SciDoubleSpinBox` for doubles. Settings grids
must go through these managers, not the stock `QtVariantEditorFactory`.

---

## 4. Runtime layout and external dependencies

- `scripts/` and `keywords/` must sit next to the executable. qmake copies them
  after linking; CI copies them into `dist/` and into the test folder. If you add a
  runtime script, it lives in `scripts/` and is found with
  `resolveModelTemplatePath()` or `applicationDirPath()/scripts`.
- Python interpreters come from preferences: `Python Path` (openEMS),
  `PALACE_PYTHON` (inside WSL on Windows), `ELMER_PYTHON`, `FIELD_VIEWER_PYTHON`
  (PyVista + Pillow; the Windows installer bundles one in `field_viewer_python/`,
  built by `scripts/stage_field_viewer_python.ps1` from `requirements-field-viewer.txt`).
- Python packages for the workflows are listed in `requirements-python.txt`.
- On Linux Wayland desktops with XWayland, main.cpp sets
  `QT_QPA_PLATFORM=xcb;wayland` before creating the QApplication
  (`preferXcbOnWayland`): Qt 5's Wayland backend prints warnings and misplaces
  nested popups. An explicit `QT_QPA_PLATFORM` wins (tests run `-platform offscreen`).
  Child processes launched for the Field viewers get `QT_QPA_PLATFORM` removed.
- Windows specifics are wrapped in `#ifdef Q_OS_WIN` (mostly runPalace, wslHelper,
  toolautodetect, about). Any code that handles paths must work with Windows paths,
  WSL `/mnt/c/...` paths and Linux paths.

---

## 5. Coding conventions

- C++17, Qt 5.15 (CI uses 5.15.2). Some code has `QT_VERSION >= 6` branches; keep
  them compiling. `DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000`.
- Every source file starts with the GPL-3.0-or-later header block (copy it from
  an existing file).
- Doxygen comments in the existing style: a `/*!*****…***/` banner with `\brief`,
  `\param`, `\return` above each function, in the .cpp. A `Doxyfile` is in the root.
- Headers declare members in aligned columns (type, then name at a fixed column),
  as in mainwindow.h.
- 4-space indent, `QStringLiteral` / `QLatin1String` for literals, `tr()` for
  user-visible text.
- Commit messages: one imperative sentence ending with a period, e.g.
  "Fix multiline port save duplicating Source Layer 202."
- Python scripts in `scripts/` target the user's solver Pythons (often 3.10–3.12,
  sometimes a Windows embeddable Python). Avoid new third-party dependencies.

---

## 6. Tests

- One Qt Test binary `emstudio_golden_tests` (tests/tests.pro), compiled with
  `EMSTUDIO_TESTING` and coverage flags. tests/main.cpp runs every suite listed in
  `ADD_TEST(...)` and writes per-suite logs merged into `test_results.txt`.
  **A new suite needs the .cpp/.h in tests.pro and tests/CMakeLists.txt, and an
  `ADD_TEST` line.**
- `MainWindow` test hooks are the `test*()` methods declared under
  `#ifdef EMSTUDIO_TESTING` in mainwindow.h and implemented in verification.cpp
  (some in runPalace.cpp). Add a hook there instead of making production methods
  public. Also under `EMSTUDIO_TESTING`, some interactive steps are skipped
  (sanity-check dialog, Elmer warnings, auto-opening Field 3D, the
  `field_io.py --list` call in `refreshFieldChoices`); grep for
  `EMSTUDIO_TESTING` before relying on a dialog in a test.
- **Golden tests**: `tests/golden/tst_palace_golden.py` and `tst_openems_golden.py`
  are the exact scripts expected from "default template + a few GUI edits".
  Template or codegen changes require updating them on purpose
  (`updateGoldenOnce()` in test_utils.cpp; the call is commented out in the test).
  `tst_about_linux_probe.txt` pins tool versions (it fails locally when your
  gds2palace version differs from CI's).
- **Settings isolation:** tests run real `MainWindow`s that save preferences.
  With `EMSTUDIO_TESTING`, `emstudioSettings()` uses its own INI store, and
  tests/main.cpp points it at a temporary folder, so local test runs never touch
  the user's preferences. Tests that read or write settings must use
  `emstudioSettings()` too.
- Python tests for the model converter: `python -m pytest tests/python/test_convert_loose.py`
  (numpy only). `tests/python/fixtures/convert_loose` has a loose model, stand-in workflow
  `modules/` (same signatures and settings branch as gds2openEMS, recording what they receive)
  and a fake `openEMS`. Each test runs the original and the converted model and compares the
  recordings. The Qt tests `MainWindowPortsTest::convertLooseModel_*` use the same fixture. In the
  test binary, wait with `QTRY_*`, not `QSignalSpy::wait()`: `HeadlessDispatchTest` calls
  `QCoreApplication::exit`, after which nested event loops return at once.
- Python tests for the field scripts: `python -m pytest tests/python` (needs
  pyvista; the viewer tests also need PySide6 + pyvistaqt, run with
  `QT_QPA_PLATFORM=offscreen`). They build small synthetic dumps; they are not
  part of the Qt test binary or CI yet. Creating several `QtInteractor` windows
  in one offscreen process aborts VTK; share one window per module.
- Solver stubs in `tests/tools/` stand in for openEMS, Palace and Elmer. Tests set
  them through preferences, e.g. `testSetPreference("PALACE_RUN_SCRIPT", stub)`. Tests that run a
  Palace model must also set `PALACE_PYTHON` to `tools/palace_python_stub` (it writes the
  `config.json` gds2palace would): otherwise a Python found on PATH runs real gds2palace and opens gmsh.
- Tests that rewrite `keywords/*.csv` next to the test binary must use
  `KeywordFileBackup` (test_utils.h), which restores the file. Copy a fresh
  `keywords/` into the build folder when a run was interrupted.
- Run locally (Linux):
  ```bash
  mkdir -p build-tests && cd build-tests && qmake ../tests/tests.pro && make -j$(nproc)
  cp -R ../scripts ../keywords ../icons .
  EMSTUDIO_TEST_PYTHON=$(command -v python3) ./emstudio_golden_tests -platform offscreen
  ```
  or `scripts/run_tests_local.sh`. Some distro Qt builds (e.g. 5.15.13) compile
  the QtPropertyBrowser `moc_*.cpp` files separately even though the sources
  `#include` them, which fails. CI's Qt 5.15.2 is fine. Locally, remove those
  `moc_*.o` from the generated Makefile's OBJECTS.
- The test runs chmod the `tests/tools/*.sh` stubs. Don't commit those mode changes.
- If you change the data members of a class with inline accessors (e.g. `LayoutView::isFieldMode`),
  rebuild the test objects (`rm *.o` in the test build folder): the locally patched test Makefile
  may not recompile every test file, and a stale object then reads the wrong member.

---

## 7. Build and CI

- **Two build systems, three source lists.** qmake: `emstudio_sources.pri` (app
  and tests), plus `EMStudio.pro` (main.cpp, .ui, resources). CMake:
  `CMakeLists.txt` (app) and `tests/CMakeLists.txt` have their own lists. **When
  you add or remove a source or header (anything with `Q_OBJECT` needs the header
  in HEADERS for moc), update emstudio_sources.pri and both CMake lists.** CI has
  failed several times over this.
- Version: `1.<commits since tag v1.0>` computed by qmake/CMake from git
  (`EMSTUDIO_VERSION_STR`, `EMSTUDIO_GIT_DATE_STR`).
- Linux dev build: `qmake EMStudio.pro && make -j$(nproc) && ./EMStudio`.
- CI (.github/workflows/build.yml):
  - `build-linux`, plus `build-ubuntu-portable` for older glibc;
  - `tests-linux` (Qt 5.15.2, xvfb, `gds2palace==0.5.2` for the About golden);
  - `build-windows` (MinGW, stages `field_viewer_python`, Inno Setup installer
    `installer/EMStudio.iss` packing `build/dist/*`).

---

## 8. Pitfalls and rules

1. **Script round-trip.** Any GUI value must be written into the script by the
   GUI → script path, or Save's re-parse silently reverts it (§3.2). Test new
   settings with a Save round-trip (see
   `MainWindowPortsTest::saveAction_keepsEditedTextSettings`).
2. **Regex patching.** Replacements must keep indentation and trailing `# comments`,
   and must only touch lines that exist (`writeMode`). Don't rewrite unchanged
   values; users diff their scripts. Models may set things inside loops (parameter
   sweeps, e.g. openEMS `L6n2_sweep`): the port and thermal blocks, `Boundaries`,
   GDS/XML paths and `gds_cellname` are only rewritten when they changed, and then
   with the original indentation; paths that still name the same file (relative
   ones too) stay as written (`MainWindowPortsTest::saveAction_keepsIndentedSweepModel`).
3. **Tool-specific behaviour** (e.g. Elmer EM `fdump` is a checkbox, Palace `fdump`
   is a frequency list) is keyed on `currentSimToolKey()`. Check all four tools
   when changing shared code. The tool can change while a model is open, so a grid
   editor chosen for one tool must not reach the script of another (see `fdump`, §3.2).
   A user's choice in the tool list (`QComboBox::activated`, not File > New / load) with a
   model open calls `warnAboutToolSwitch` (Log + dialog): gds2palace tools ↔ each other get
   their workflow calls patched on Save (`apply*WorkflowToScript`), openEMS is never converted.
4. **Never start solvers from the script.** `forceStartSimulationOff` sets
   `start_simulation = False`; EMStudio launches solvers itself.
5. **Tab titles are identifiers** (`m_tabMap`), and so are widget object names in
   `mainwindow.ui` (`m_ui->txtGdsFile` etc., and auto-connected `on_<name>_<signal>`
   slots). Renaming one breaks code without a compile error for auto-connections.
6. **WSL paths.** Use `toLinuxPathPortable` / `wslToWinPath` / `pathExistsPortable`
   for anything Palace touches on Windows.
7. **Vendored QtPropertyBrowser.** Fix bugs if needed, but keep its API; the app
   and Preferences rely on `QtVariantPropertyManager` signals.
8. **No blocking UI.** Long work (solver runs, field export, version probes) runs
   in `QProcess` with signals. Follow that pattern.
9. **Assistant tools** run with the user's privileges (load models, run
   simulations, set preferences, edit GDS). Keep new tools narrowly scoped and
   describe them accurately in `registerTool`.
