#include <QApplication>
#include <QtTest/QtTest>

#include "tst_palace_golden.h"
#include "tst_openems_golden.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    int status = 0;

    {
        PalaceGolden tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        OpenemsGolden tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}
