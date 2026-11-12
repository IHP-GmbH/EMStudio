/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 ************************************************************************/

#ifndef EMMARKUPPANEL_H
#define EMMARKUPPANEL_H

#include <QWidget>

#include "emmarkup.h"

class QLabel;
class QGroupBox;
class QPushButton;
class QTableWidget;
class QDoubleSpinBox;

/*!*******************************************************************************************************************
 * \class EmMarkupPanel
 * \brief "Markup" tab beside "Layers": the EM region and the ports of the layout.
 *
 * Shows and edits an EmMarkup; the layout itself is never changed. Every user edit emits
 * \c markupEdited with the whole markup; MainWindow redraws the overlay and saves the sidecar.
 **********************************************************************************************************************/
class EmMarkupPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EmMarkupPanel(QWidget *parent = nullptr);

    /*! Shows \a markup (no \c markupEdited). */
    void                setMarkup(const EmMarkup &markup);
    const EmMarkup     &markup() const { return m_markup; }
    /*! Stackup metal names offered for the port layers and used for the checks. */
    void                setMetals(const QStringList &metals);
    /*! Layout extent in GDS µm, for "Fit to layout". */
    void                setLayoutBounds(const QRectF &boundsUm);
    /*! The layout holds klayout_em_export markup: enables "Import from layout". */
    void                setLayoutHasMarkup(bool has);
    /*! Where the markup comes from / whether it is saved. */
    void                setStatus(const QString &text);
    /*! Problems shown under the ports (from EmMarkup::problems). */
    QStringList         problems() const;

    /*! Draw tool shown as armed: 0 none, 1 region, 2 port (no signals). */
    void                setDrawTool(int tool);
    /*! Ruler button shown as armed (no signals). */
    void                setRulerArmed(bool on);
    /*! Sets the EM region (as drawn in the layout) and emits \c markupEdited. */
    void                setRegion(const QRectF &regionUm);
    /*! Adds \a port (its number is kept if free, else the next free one) and emits \c markupEdited. */
    void                addPort(EmMarkupPort port);
    /*! Removes port \a number and emits \c markupEdited (nothing when there is no such port). */
    void                removePort(int number);
    /*! Gives port \a number the shapes \a shapesUm (resized or moved in the layout) and emits \c markupEdited. */
    void                setPortShapes(int number, const QVector<QPolygonF> &shapesUm);
    /*! Selects the row of port \a number (0 = none) without \c portSelected. */
    void                selectPort(int number);
    /*! Port of the selected row, 0 = none. */
    int                 selectedPort() const;

signals:
    /*! The user picked port \a number in the table (0 = none). */
    void                portSelected(int number);
    void                markupEdited(const EmMarkup &markup);
    void                importRequested();
    /*! Draw tool button toggled: 0 none, 1 region, 2 port. */
    void                drawToolRequested(int tool);
    /*! Ruler button toggled: measure in the layout. */
    void                rulerRequested(bool on);

private:
    void                rebuild();
    void                refreshProblems();
    void                fillPortRow(int row);
    void                onRegionEdited();
    void                onPortRowEdited(int row);
    void                emitEdited();

    EmMarkup            m_markup;
    QStringList         m_metals;
    QRectF              m_layoutBounds;
    bool                m_updating = false;

    QLabel             *m_status = nullptr;
    QPushButton        *m_importBtn = nullptr;
    QGroupBox          *m_regionBox = nullptr;
    QDoubleSpinBox     *m_x1 = nullptr;
    QDoubleSpinBox     *m_y1 = nullptr;
    QDoubleSpinBox     *m_x2 = nullptr;
    QDoubleSpinBox     *m_y2 = nullptr;
    QPushButton        *m_fitBtn = nullptr;
    QTableWidget       *m_ports = nullptr;
    QPushButton        *m_removePortBtn = nullptr;
    QPushButton        *m_drawRegionBtn = nullptr;
    QPushButton        *m_drawPortBtn = nullptr;
    QPushButton        *m_rulerBtn = nullptr;
    QLabel             *m_problems = nullptr;
};

#endif // EMMARKUPPANEL_H
