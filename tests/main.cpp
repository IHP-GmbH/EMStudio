#include <QApplication>
#include <QtTest/QtTest>

#include "tst_find_dialog.h"
#include "tst_about_dialog.h"
#include "tst_palace_golden.h"
#include "tst_python_editor.h"
#include "tst_openems_golden.h"
#include "tst_headless_dispatch.h"
#include "tst_preferences_dialog.h"
#include "tst_keywords_editor_dialog.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    int status = 0;

    {
        HeadlessDispatchTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        PythonEditorTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        PalaceGolden tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        OpenemsGolden tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        AboutDialogTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        PreferencesDialogTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        FindDialogTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        KeywordsEditorDialogTest tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}
