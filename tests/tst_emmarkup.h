/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_EMMARKUP_H
#define TST_EMMARKUP_H

#include <QObject>

class EmMarkupTest : public QObject
{
    Q_OBJECT

private slots:
    void portSettings_matchKLayoutEmExport();
    void gdsReader_keepsPropertiesAndImportsMarkup();
    void sidecar_roundTrip();
    void problems_reportBadPorts();
    void layoutView_drawsMarkupAsOverlay();
    void mainWindow_markupTab_importsSavesAndReloads();
    void clipAndMerge_mergesClipsAndOpensHoles();
    void export_writesGdsSimcfgAndPortmap();
    void layoutView_drawTool_emitsSnappedRect();
    void mainWindow_exportUsesEmLayoutInModel();
    void mainWindow_markupPortsFollowPortsTable();
    void layoutView_portHandlesResizeAndMove();
    void layoutView_rulerSnapsToLayout();
    void mainWindow_reshapeAndExportRenumbersMarkup();
};

#endif // TST_EMMARKUP_H
