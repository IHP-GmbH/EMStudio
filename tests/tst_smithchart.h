/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_SMITHCHART_H
#define TST_SMITHCHART_H

#include <QObject>

class SmithChartTest : public QObject
{
    Q_OBJECT

private slots:
    void paintsEmptyAndWithTraces();
    void zoomToggle_repaints();
    void multiPointTrace_isStrokedNotFilled(); // regression #24
};

#endif // TST_SMITHCHART_H
