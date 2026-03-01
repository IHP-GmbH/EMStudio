# EMStudio HPC Docker

Docker image providing:

-   **EMStudio (Qt5 GUI)**
-   **Palace (MPI build)**
-   **openEMS**
-   Python venv with `gmsh`, `gdspy`, `scikit-rf`
-   Non-root MPI execution (required by Palace)

------------------------------------------------------------------------

# Prerequisites

You must have **Docker installed** on your system.

## Linux (Ubuntu/Debian)

``` bash
sudo apt update
sudo apt install docker.io
sudo usermod -aG docker $USER
```

Log out and log back in after adding yourself to the docker group.

Verify installation:

``` bash
docker --version
docker run hello-world
```

------------------------------------------------------------------------

## Windows (WSL2)

1.  Install **Docker Desktop**
2.  Enable **WSL2 integration**
3.  Verify inside WSL:

``` bash
docker --version
docker run hello-world
```

------------------------------------------------------------------------

## Disk Space Requirements

-   Recommended free space: **20--30 GB**
-   Build process downloads and compiles multiple large dependencies.

------------------------------------------------------------------------

# Build Image

Build the Docker image and save the full build log:

``` bash
docker build --progress=plain -t emstudio-hpc:dev . 2>&1 | tee build.log
```

Options explained:

-   `--progress=plain` → prints full build output\
-   `tee build.log` → stores the build log for debugging

After successful build the image name will be:

    emstudio-hpc:dev

------------------------------------------------------------------------

# Run Container (X11 GUI)

Run EMStudio with X11 forwarding:

``` bash
docker run -it --rm   -e DISPLAY=$DISPLAY   -e QT_QPA_PLATFORM=xcb   -e TMPDIR=/Docker/tmp   -e TEMP=/Docker/tmp   -e TMP=/Docker/tmp   -v /tmp/.X11-unix:/tmp/.X11-unix   -v "$HOME/Docker/workspace:/workspace"   -w /workspace   emstudio-hpc:dev
```

------------------------------------------------------------------------

## What this does

-   `DISPLAY` + `/tmp/.X11-unix` → enables Qt GUI via X11\
-   `QT_QPA_PLATFORM=xcb` → forces Qt to use X11 backend\
-   `TMPDIR`, `TEMP`, `TMP` → defines writable temp directory\
-   `-v "$HOME/Docker/workspace:/workspace"` → persistent project
    storage\
-   `-w /workspace` → sets working directory inside container\
-   `--rm` → removes container after exit

Container runs as **non-root user (UID 1000)**.\
Palace runs via `mpirun --oversubscribe`.

------------------------------------------------------------------------

# Persistent Data

All project files are stored in:

    $HOME/Docker/workspace

EMStudio configuration:

    /workspace/.config/EMStudio/

This ensures settings and models persist between container runs.

------------------------------------------------------------------------

# Environment Defaults

The container sets:

-   `HOME=/workspace`
-   `TMPDIR=/tmp`
-   `OMPI_MCA_tmpdir_base=/tmp`
-   `OMPI_MCA_orte_tmpdir_base=/tmp`
-   `XDG_RUNTIME_DIR=/tmp/runtime-UID`

This prevents common OpenMPI and Qt permission issues.

------------------------------------------------------------------------

# Troubleshooting

### Qt error: `XDG_RUNTIME_DIR not set`

Handled automatically in container startup.

------------------------------------------------------------------------

### OpenMPI error: `mkdir Permission denied`

Ensure `TMPDIR` is writable and not pointing to a root-owned path.

------------------------------------------------------------------------

### GUI not opening

Make sure X11 access is allowed:

``` bash
xhost +local:
```

------------------------------------------------------------------------

# Cleanup

Remove image:

``` bash
docker rmi emstudio-hpc:dev
```

Remove unused Docker data:

``` bash
docker system prune -af
```

------------------------------------------------------------------------

# Notes

-   Designed for Linux host with X11.
-   For WSL2/Windows additional X server configuration may be required.
-   Image size is large due to build toolchains and simulation
    libraries.
