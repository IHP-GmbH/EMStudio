#!/usr/bin/env bash
# Install Qt 5.15 (+ Charts) for Rocky/RHEL 8 CI via aqt (system python is too old
# for jurplel/install-qt-action).
set -euo pipefail

QT_VERSION="${QT_VERSION:-5.15.2}"
QT_INSTALL_ROOT="${QT_INSTALL_ROOT:-/opt/Qt}"
QT_PATH="${QT_INSTALL_ROOT}/${QT_VERSION}/gcc_64"

if [[ -x "${QT_PATH}/bin/qmake" ]]; then
  echo "Qt already installed at ${QT_PATH}"
else
  dnf install -y python39 python39-pip
  python3.9 -m pip install --upgrade 'pip<25' 'setuptools>=70.1.0' wheel
  python3.9 -m pip install 'aqtinstall==3.1.*' 'py7zr==1.0.*'

  mkdir -p "$QT_INSTALL_ROOT"
  python3.9 -m aqt install-qt linux desktop "$QT_VERSION" gcc_64 \
    -m qtcharts \
    -O "$QT_INSTALL_ROOT"

  if [[ ! -x "${QT_PATH}/bin/qmake" ]]; then
    echo "ERROR: qmake not found after aqt install (expected ${QT_PATH}/bin/qmake)" >&2
    find "$QT_INSTALL_ROOT" -name qmake 2>/dev/null || true
    exit 1
  fi
fi

echo "Qt installed at ${QT_PATH}"
"${QT_PATH}/bin/qmake" -version

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "QT_ROOT_DIR=${QT_PATH}"
    echo "LD_LIBRARY_PATH=${QT_PATH}/lib:${LD_LIBRARY_PATH:-}"
  } >>"$GITHUB_ENV"
fi

if [[ -n "${GITHUB_PATH:-}" ]]; then
  echo "${QT_PATH}/bin" >>"$GITHUB_PATH"
fi
