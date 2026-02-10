# tools/coverage.pri
# Enable GCC/MinGW coverage instrumentation

win32-g++|unix:!macx {
    QMAKE_CXXFLAGS += -O0 -g --coverage
    QMAKE_CFLAGS   += -O0 -g --coverage
    QMAKE_LFLAGS   += --coverage
}

