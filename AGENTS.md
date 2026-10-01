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
keywords/            openems.csv / palace.csv: tooltips for settings keys
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
| mainwindow.cpp | Construction, tabs, menus, settings grid, preferences, load/save/save-as, recent models, ports table, GDS/stackup inputs, layout preview + Layout Field, KLayout launch, assistant tool registration, sanity checks |
| pythonToStudio.cpp | **Script → GUI**: `applyPythonScriptFromEditor()` (write editor to file, re-parse, update GUI), settings grid rebuild, boundaries, property type inference |
| pythonToEditor.cpp | **GUI → script**: patch settings, paths, ports, stackup variable overrides and boundaries into the script text |
| runOpenEms.cpp | Run openEMS (one Python process) |
| runPalace.cpp | Run Palace / Elmer: Python stage, then solver stage; WSL, launcher scripts, MPI cores, simulation log, CSV→Touchstone |
| elmerThermalUi.cpp | Tool-key helpers, Ports↔Thermal tab switch, thermal objects table ↔ script, opening thermal results in Field view |
| headless.cpp | `runHeadless()` for `-run -palace/-openems` |
| gdsreader.cpp / xmlreader.cpp | GDS cell/layer lists; stackup layer names for the port combos |
| tips.cpp | Load `keywords/<tool>.csv` and merge with `# @brief` tips from the model |
| wslHelper.cpp | Path conversion and existence checks that work across Windows and WSL |
| verification.cpp | `test*()` hooks, compiled only with `EMSTUDIO_TESTING` (see §6) |

Key members:

- `m_simSettings` (`QMap<QString,QVariant>`): the model's current settings.
  Generic keys mirror the script's `settings['key']` / top-level variables. Special
  keys: `Boundaries` (QVariantMap of X-/X+/…), `GdsFile`, `SubstrateFile`,
  `TopCell` / `gds_cellname` / `cellname`, `RunPythonScript`, `RunDir`,
  `StackupVariableOverrides`. The generic writer skips the structural ones
  (`keyIsExcludedForEm`, `shouldSkipPalaceSettingKey`).
- `m_preferences` (`QMap<QString,QVariant>`): app preferences (tool paths, Python
  interpreters, WSL distro, assistant config, recent models, …), persisted under
  QSettings group `Preferences`.
- `m_sysSettings`: misc. state (last dirs), QSettings group `SystemSettings`.
- `m_curPythonData` (`PythonParser::Result`): last parse of the model script,
  including where each key is written (`writeMode`: top-level vs dict) and which
  values were quoted strings (`quotedStrings`).
- `m_tabMap`: tab **title** → index. Code calls `showTab(m_tabMap["Substrate"])`
  etc., so renaming a tab title in `mainwindow.ui` breaks those lookups.
  Titles: Main, Substrate, Python, Ports (relabelled Thermal for Elmer Thermal),
  Simulate, Results.

QSettings: organization `EMStudio`, application `EMStudioApp` (on Linux:
`~/.config/EMStudio/EMStudioApp.conf`). `saveSettings()` / `loadSettings()` in
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
  - `# @brief` tooltip comments,
  - GDS/XML file names and the top cell.

  Values become `bool`, `double`/`longlong`, or `QString`. A quoted literal is
  stored without its quotes; anything else (lists, expressions, `None`) is kept as
  raw text. `writeMode[key]` records whether the key is written as `TopLevel` or
  `DictAssign`. `quotedStrings` lists the keys whose value was a quoted literal.
- **Settings grid** (Main tab, `m_propertyBrowser` + `m_variantManager`):
  `rebuildSimulationSettingsFromPalace()` creates one property per setting.
  `inferPalacePropertyInfo()` picks the editor: numbers are always `Double` (for
  `SciDoubleSpinBox`), plus `Bool` and `String`. Elmer EM `fdump` is a checkbox.
  Text that looks like code (`foo.bar`, calls) and quoted strings containing `.`
  are hidden (`shouldSkipStringSelfReference`). Edits arrive in
  `onSimulationSettingChanged()` → `m_simSettings`.
- **GUI → script**: `syncGuiSettingsToPythonEditor()` / `loadPythonScriptToEditor()`
  call:
  - `applySimSettingsToScript` → `applyOneSettingToScript`, once per key. It
    replaces only the value, keeps trailing comments, and only touches keys
    already in `writeMode`. Text cells are written back only when they changed:
    quoted strings re-quoted, lists and expressions verbatim; for `fdump` a bare
    value is wrapped in `[...]`.
  - `applyGdsAndXmlPaths`, `applyVariableOverridesToScript`, `applyBoundaries`.
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
- **Ports**: GUI table `tblPorts` ↔ `simulation_ports.add_port(simulation_setup.simulation_port(...))`
  lines, which may span several lines (`findPortBlocks` balances parentheses).
  `PortInfo` holds one port. In-plane ports use only `to_layername`; via ports use
  from + to.
- **Thermal objects**: `add_heatsource` / `add_consttemp` blocks ↔ `m_tblThermalObjects`.
- **Stackup variable overrides**: the `variable_overrides = {...}` line plus the
  `stackup_reader.read_substrate(...)` call.

Templates for "Generate Default" are `scripts/openems_model.py`,
`palace_model.py`, `elmer_model.py`, `elmer_thermal_model.py`, found with
`resolveModelTemplatePath()` (preference `MODEL_TEMPLATES_DIR`, else
`<app>/scripts/`). Template edits change generated scripts and therefore the
golden tests (§6).

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

  After the run, CSV results are converted with `scripts/combine_extend_snp.py`.
  Elmer Thermal opens the Substrate tab with Layout Field.
- The simulation log is persisted as `emstudio_simulation.log` in the run's data
  folder, e.g. `palace_model/<model>_data/` (`simulationLogFilePath`,
  `persistSimulationLogSnapshot`, `loadSimulationLogFromDisk`).
- Headless: `EMStudio -run -palace|-openems model.py` (main.cpp → `runHeadless`)
  exits with a non-zero code on failure.

### 3.4 Other UI components

| Component | Files | Notes |
|---|---|---|
| Stackup model | substrate, layer, material, dielectric, stackupexpr | Parses and writes the XML stackup. Schema 3.x: Variables with expressions, derived layers, thermal tables. `resolve(overrides)` evaluates expressions. |
| Stackup cross-section | substrateview | 2.5D stack drawing; clicking a layer highlights it in the layout view |
| Stackup editor | stackupeditor | Dialog that edits the XML; emits saved → reload |
| Layout preview | gdslayout (flattening), layoutview, layoutlayerpanel | Substrate tab: top view, Iso3D extrusion, ports, layer panel, **Layout Field** overlay |
| Layout Field | layoutview + mainwindow.cpp `*Field*` methods | Runs `scripts/field_slice_export.py` (QProcess, host Python `FIELD_VIEWER_PYTHON`) for a Z-slice PNG plus meta JSON. Field→3D starts `scripts/field_volume_viewer.py`. |
| Results | resultsviewer, touchstone, smithchartwidget, resultscalculator, exprparser | Scans the run folder for `.sNp`; dB/phase/Smith; Compare; RF calculator (`cser($1)`, `ydiff_cser($1,$2)`, …); Model Fit via `snp2le` |
| Python editor | pythoneditor, pythonsyntaxhighlighter, finddialog | `editRunPythonScript` on the Python tab |
| Preferences | preferences (+ preferences.ui) | Property-browser dialog over `m_preferences` |
| About | about (+ about.ui) | Async version probes for tools; native on Linux, WSL on Windows |
| Keywords | keywordseditor, tips | Edit `keywords/*.csv` tooltips |
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
  (sanity-check dialog, Elmer warnings, auto-opening Field 3D); grep for
  `EMSTUDIO_TESTING` before relying on a dialog in a test.
- **Golden tests**: `tests/golden/tst_palace_golden.py` and `tst_openems_golden.py`
  are the exact scripts expected from "default template + a few GUI edits".
  Template or codegen changes require updating them on purpose
  (`updateGoldenOnce()` in test_utils.cpp; the call is commented out in the test).
  `tst_about_linux_probe.txt` pins tool versions (it fails locally when your
  gds2palace version differs from CI's).
- Solver stubs in `tests/tools/` stand in for openEMS, Palace and Elmer. Tests set
  them through preferences, e.g. `testSetPreference("PALACE_RUN_SCRIPT", stub)`.
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
   values; users diff their scripts.
3. **Tool-specific behaviour** (e.g. Elmer EM `fdump` is a checkbox, Palace `fdump`
   is a frequency list) is keyed on `currentSimToolKey()`. Check all four tools
   when changing shared code.
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
