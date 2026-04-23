#include <QApplication>
#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QStringList>

#include <functional>
#include <cstdio>

#include "tst_wsl_helper.h"
#include "tst_find_dialog.h"
#include "tst_about_dialog.h"
#include "tst_palace_golden.h"
#include "tst_python_editor.h"
#include "tst_openems_golden.h"
#include "tst_mainwindow_ports.h"
#include "tst_headless_dispatch.h"
#include "tst_preferences_dialog.h"
#include "tst_keywords_editor_dialog.h"

namespace
{

/*!********************************************************************************************************
 * \brief Runs one test class and stores its output in a dedicated text file.
 *********************************************************************************************************/
template <typename T>
static int runTestToFile(const QString &fileName, int argc, char **argv)
{
    Q_UNUSED(argc)

    QByteArray fileArg = "-o";
    QByteArray outArg  = QFile::encodeName(fileName) + ",txt";

    char *testArgv[] = {
        argv[0],
        fileArg.data(),
        outArg.data()
    };

    T tc;
    return QTest::qExec(&tc, 3, testArgv);
}

/*!********************************************************************************************************
 * \brief Appends the contents of one file to another file.
 *********************************************************************************************************/
static bool appendFile(QFile &dst, const QString &srcPath)
{
    QFile src(srcPath);
    if(!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    dst.write(src.readAll());
    src.close();
    return true;
}

/*!********************************************************************************************************
 * \brief Joins per-suite logs into a single test_results.txt file.
 *********************************************************************************************************/
static void mergeLogs(const QStringList &files, const QString &mergedFile)
{
    QFile out(mergedFile);
    if(!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qDebug() << "Failed to open merged log:" << mergedFile;
        return;
    }

    for(const QString &file : files) {
        out.write("==================================================\n");
        out.write(QFileInfo(file).fileName().toUtf8());
        out.write("\n==================================================\n");

        if(!appendFile(out, file)) {
            out.write("Failed to read file.\n");
        }

        out.write("\n\n");
    }

    out.close();
}

/*!********************************************************************************************************
 * \brief Stores one test entry with a name and a launcher function.
 *********************************************************************************************************/
struct TestEntry
{
    QString                             name;
    std::function<int(const QString &)> run;
};

template <typename T>
static TestEntry makeTestEntry(const QString &name, int argc, char **argv)
{
    TestEntry entry;
    entry.name = name;
    entry.run = [argc, argv](const QString &fileName) -> int {
        return runTestToFile<T>(fileName, argc, argv);
    };
    return entry;
}

} // namespace

#define ADD_TEST(TESTCLASS) makeTestEntry<TESTCLASS>(#TESTCLASS, argc, argv)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    int status = 0;

    const QString logDir    = QDir::currentPath();
    const QString mergedLog = logDir + "/test_results.txt";

    QFile::remove(mergedLog);

    const QList<TestEntry> tests = {
        ADD_TEST(HeadlessDispatchTest),
        ADD_TEST(WslHelperTest),
        ADD_TEST(MainWindowPortsTest),
        ADD_TEST(PythonEditorTest),
        ADD_TEST(PalaceGolden),
        ADD_TEST(OpenemsGolden),
        ADD_TEST(AboutDialogTest),
        ADD_TEST(PreferencesDialogTest),
        ADD_TEST(FindDialogTest),
        ADD_TEST(KeywordsEditorDialogTest)
    };

    QStringList logFiles;

    for(const TestEntry &test : tests) {
        const QString logFile = logDir + "/test_results_" + test.name + ".txt";

        QFile::remove(logFile);

        const int testStatus = test.run(logFile);
        status |= testStatus;
        logFiles << logFile;

        fprintf(stdout,
                "[%s] %s\n",
                testStatus == 0 ? "PASS" : "FAIL",
                test.name.toUtf8().constData());
        fflush(stdout);
    }

    mergeLogs(logFiles, mergedLog);

    if(status == 0) {
        fprintf(stdout, "\n===================================\n");
        fprintf(stdout, "ALL TESTS PASSED\n");
        fprintf(stdout, "===================================\n\n");
        fflush(stdout);
    }
    else {
        fprintf(stdout, "\n===================================\n");
        fprintf(stdout, "TESTS FAILED (status = %d)\n", status);
        fprintf(stdout, "===================================\n\n");
        fflush(stdout);
    }

    fprintf(stdout, "Merged test log: %s\n", mergedLog.toUtf8().constData());
    fflush(stdout);

    return status;
}

#undef ADD_TEST
