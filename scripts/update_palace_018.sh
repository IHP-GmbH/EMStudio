#!/bin/bash
set -euo pipefail
LOG=/home/adatsuk/update_palace_018.log
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) start palace v0.18.1 ==="
cd /home/adatsuk/palace
rm -f .git/refs/remotes/origin/main
git fetch --tags origin
git checkout -f v0.18.1
git submodule update --init --recursive
echo "=== checked out ==="
git describe --tags --always
if [ -d /home/adatsuk/palace-install ] && [ ! -d /home/adatsuk/palace-install-0.17 ]; then
  echo "=== backing up palace-install -> palace-install-0.17 ==="
  cp -a /home/adatsuk/palace-install /home/adatsuk/palace-install-0.17
fi
echo "=== cmake configure ==="
mkdir -p build
cd build
cmake .. \
  -DCMAKE_INSTALL_PREFIX=/home/adatsuk/palace-install \
  -DCMAKE_BUILD_TYPE=Release \
  -DPALACE_BUILD_EXTERNAL_DEPS=ON \
  -DPALACE_WITH_GSLIB=ON \
  -DPALACE_WITH_LIBXSMM=ON \
  -DPALACE_WITH_SLEPC=ON \
  -DPALACE_WITH_SUPERLU=ON \
  -DPALACE_WITH_SUNDIALS=ON \
  -DPALACE_WITH_CUDA=OFF \
  -DPALACE_WITH_OPENMP=OFF
echo "=== build palace-install ==="
NPROC=$(nproc)
cmake --build . --target palace-install -j"$NPROC"
echo "=== done ==="
/home/adatsuk/palace-install/bin/palace --version || true
git -C /home/adatsuk/palace describe --tags --always
echo "=== $(date -Is) finished ==="
