/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_RESULTS_VIEWER_H
#define TST_RESULTS_VIEWER_H

#include <QObject>

class ResultsViewerTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyDirectory_showsPlaceholder();
    void loadsTouchstone_andSwitchesModes();
    void filterCheckboxes_affectListing();
    void checkedFile_togglesParamButtonsAndSmith();
    void nestedDirs_groupItemsAndConvertWithoutCsv();
};

#endif // TST_RESULTS_VIEWER_H
