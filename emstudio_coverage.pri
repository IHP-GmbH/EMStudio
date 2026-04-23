# tools/coverage.pri
# Enable GCC/MinGW coverage instrumentation

coverage {
    win32-g++|unix:!macx {
        QMAKE_CXXFLAGS += -O0 -g --coverage -fprofile-abs-path
        QMAKE_CFLAGS   += -O0 -g --coverage -fprofile-abs-path
        QMAKE_LFLAGS   += --coverage

        DEFINES += COVERAGE_BUILD

        QMAKE_DISTCLEAN += *.gcda *.gcno *.gcov coverage.html

        report.commands = gcovr -r $$TOP --html-details -o coverage.html --print-summary --exclude ".*moc_.*" --exclude ".*qrc_.*"
        QMAKE_EXTRA_TARGETS += report
    }
}
