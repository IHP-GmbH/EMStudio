<p align="center">
  <img src="icons/logo.png" alt="EMStudio Logo" width="160"/>
</p>

<br/>

<h1 align="center">EMStudio</h1>

<br/>
<br/>

<p align="center">
  Qt-based GUI for electromagnetic (EM) simulations  
  Developed at IHP Microelectronics
</p>

---

## Overview

**EMStudio** is a Qt-based desktop application for preparing, visualizing, and managing electromagnetic simulations.

It provides an integrated workflow for:

- Loading **GDS** layout data  
- Defining and editing **substrate stacks** (dielectrics, metals, interfaces)  
- Visualizing a **2.5D cross-section** of the substrate  
- Configuring simulation parameters  
- Generating configuration files for external EM solvers (e.g. **Palace**)  
- Editing and validating **Python driver scripts** with syntax highlighting  

The application uses the Qt Property Browser for convenient editing of model parameters and includes custom editors and widgets.

---

## Features

- Qt GUI (C++17, Qt Widgets)
- GDS reader (`gdsreader.cpp`)
- Substrate and material model (`substrate`, `dielectric`, `material`)
- 2.5D stack visualization (`substrateview`)
- Python parser + integrated code editor with syntax highlighter
- Preferences dialog for application paths and solver settings
- Generation of Palace/solver configuration (JSON + Python)
- Custom QtPropertyBrowser-based parameter editor

---

## Requirements

- **Qt 5 (or 6)** with Widgets  
- **C++17** compiler  
  - Linux: GCC / Clang  
  - Windows: MSVC 2019/2022 or MinGW  
- qmake (from your Qt installation)  
- (Optional) Palace EM solver & Python (for simulation execution)

---

## Building EMStudio

### Build with Qt Creator (recommended)

1. Open **Qt Creator**
2. `File → Open File or Project…` → select `EMStudio.pro`
3. Select a Kit (e.g. *Desktop Qt 5.15.2 MSVC 64-bit*)
4. Configure project
5. Click **Build**
6. Click **Run**

---

### Build from command line (Linux)

```bash
cd /path/to/EMStudio
qmake EMStudio.pro
make -j$(nproc)
./EMStudio

