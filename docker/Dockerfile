FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-lc"]

# ----------------------------
# Base build + runtime deps
# ----------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git wget curl \
    build-essential pkg-config \
    cmake ninja-build gfortran \
    python3 python3-pip python3-venv \
    openmpi-bin libopenmpi-dev \
    libblas-dev liblapack-dev libopenblas-dev \
    libtinyxml-dev \
    libcgal-dev libgmp-dev libmpfr-dev \
    qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools \
    qttools5-dev qttools5-dev-tools \
    libqt5svg5-dev \
    libqt5opengl5-dev \
    libx11-6 libx11-xcb1 libxcb1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-render-util0 libxcb-render0 libxcb-shape0 libxcb-shm0 libxcb-sync1 \
    libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 libxcb-glx0 \
    libgl1 mesa-utils \
    xterm \
    libfftw3-dev libhdf5-dev libboost-all-dev \
    libvtk9-dev libvtk9-qt-dev \
    gengetopt help2man groff pod2pdf bison flex libhpdf-dev libtool \
 && rm -rf /var/lib/apt/lists/*

# ----------------------------
# Python venv + gmsh + deps
# ----------------------------
RUN python3 -m venv /opt/venv \
 && source /opt/venv/bin/activate \
 && pip install --upgrade pip \
 && pip install gmsh gdspy scikit-rf

# ----------------------------
# gds2palace (not a pip package) -> clone + add to PYTHONPATH
# ----------------------------
RUN git clone https://github.com/VolkerMuehlhaus/gds2palace_ihp_sg13g2.git /opt/gds2palace_ihp_sg13g2
ENV PYTHONPATH="/opt/gds2palace_ihp_sg13g2:${PYTHONPATH:-}"

ENV PATH="/opt/venv/bin:/opt/emstudio/bin:/opt/openems/bin:/opt/palace/bin:${PATH}"
ENV VIRTUAL_ENV="/opt/venv"

# ----------------------------
# Build Palace (source; downloads deps during CMake)
# ----------------------------
RUN git clone --recursive https://github.com/awslabs/palace.git \
 && cd palace \
 && cmake -S . -B build -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/palace \
 && cmake --build build --parallel 1 --verbose 2>&1 | tee /tmp/palace-build.log \
 && cmake --install build

# ----------------------------
# Build openEMS (C++ install) + manual Python install
# ----------------------------
WORKDIR /opt/src
RUN git clone --recursive https://github.com/thliebig/openEMS-Project.git \
 && cd openEMS-Project \
 && source /opt/venv/bin/activate \
 && ./update_openEMS.sh /opt/openems --python --disable-GUI \
 || (echo "===== openEMS build log =====" && tail -n 200 /opt/src/openEMS-Project/build_*.log && false)
 
# ----------------------------
# Build EMStudio (qmake)
# ----------------------------
RUN git clone --recursive https://github.com/IHP-GmbH/EMStudio.git \
 && cd EMStudio \
 && qmake EMStudio.pro \
 && make -j"$(nproc)" \
 && mkdir -p /opt/emstudio/bin \
 && cp -a EMStudio /opt/emstudio/bin/EMStudio
 
RUN chown -R 1000:1000 /opt/gds2palace_ihp_sg13g2/workflow
RUN chown -R 1000:1000 /tmp
RUN mkdir -p /run/user/1000 && chown 1000:1000 /run/user/1000 && chmod 700 /run/user/1000
 
# ----------------------------
# Non-root user for OpenMPI
# ----------------------------
RUN set -e; \
    if ! getent passwd 1000 >/dev/null; then \
        useradd -m -u 1000 -s /bin/bash user; \
    fi; \
    mkdir -p /workspace; \
    chown -R 1000:1000 /workspace

USER 1000
ENV HOME=/workspace
WORKDIR /workspace

COPY EMStudioApp.conf.docker /etc/emstudio/EMStudioApp.conf

# ----------------------------
# Entry point
# ----------------------------
COPY entrypoint.sh /entrypoint.sh
USER root
RUN chmod +x /entrypoint.sh
USER 1000

ENV QT_X11_NO_MITSHM=1
ENTRYPOINT ["/entrypoint.sh"]
CMD ["/opt/emstudio/bin/EMStudio"]
