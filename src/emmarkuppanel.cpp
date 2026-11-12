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

#include "emmarkuppanel.h"

#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QSignalBlocker>

#include <algorithm>

namespace {

enum PortColumn { ColPort, ColDir, ColFrom, ColTo, ColZ0, ColV, ColCount };

QDoubleSpinBox *coordinateBox(QWidget *parent)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(-1e6, 1e6);
    box->setDecimals(3);
    box->setSuffix(QStringLiteral(" \u00b5m"));
    box->setKeyboardTracking(false);
    return box;
}

QComboBox *layerCombo(const QStringList &metals, const QString &current, QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->addItems(metals);
    if (!current.isEmpty() && combo->findText(current) < 0)
        combo->addItem(current);
    combo->setCurrentText(current);
    return combo;
}

} // namespace

EmMarkupPanel::EmMarkupPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("lblMarkupStatus"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    m_importBtn = new QPushButton(tr("Import from layout"), this);
    m_importBtn->setObjectName(QStringLiteral("btnMarkupImport"));
    m_importBtn->setToolTip(tr("Take the ports (layers 201..299 with port settings) and the EM region box (800/0) "
                               "drawn with the klayout_em_export KLayout plugin."));
    m_importBtn->setEnabled(false);
    buttons->addWidget(m_importBtn);
    buttons->addStretch(1);
    lay->addLayout(buttons);

    m_regionBox = new QGroupBox(tr("EM region"), this);
    m_regionBox->setObjectName(QStringLiteral("grpMarkupRegion"));
    m_regionBox->setCheckable(true);
    m_regionBox->setChecked(false);
    m_regionBox->setToolTip(tr("Only the layout inside the region is simulated."));
    auto *grid = new QGridLayout(m_regionBox);
    m_x1 = coordinateBox(m_regionBox);
    m_y1 = coordinateBox(m_regionBox);
    m_x2 = coordinateBox(m_regionBox);
    m_y2 = coordinateBox(m_regionBox);
    grid->addWidget(new QLabel(tr("x1"), m_regionBox), 0, 0);
    grid->addWidget(m_x1, 0, 1);
    grid->addWidget(new QLabel(tr("y1"), m_regionBox), 0, 2);
    grid->addWidget(m_y1, 0, 3);
    grid->addWidget(new QLabel(tr("x2"), m_regionBox), 1, 0);
    grid->addWidget(m_x2, 1, 1);
    grid->addWidget(new QLabel(tr("y2"), m_regionBox), 1, 2);
    grid->addWidget(m_y2, 1, 3);
    m_drawRegionBtn = new QPushButton(tr("Draw region"), m_regionBox);
    m_drawRegionBtn->setObjectName(QStringLiteral("btnMarkupDrawRegion"));
    m_drawRegionBtn->setCheckable(true);
    m_drawRegionBtn->setToolTip(tr("Drag a rectangle in the layout (Esc cancels)."));
    m_fitBtn = new QPushButton(tr("Fit to layout"), m_regionBox);
    m_fitBtn->setObjectName(QStringLiteral("btnMarkupFitRegion"));
    grid->addWidget(m_drawRegionBtn, 2, 0, 1, 2);
    grid->addWidget(m_fitBtn, 2, 2, 1, 2);
    lay->addWidget(m_regionBox);

    auto *portsBox = new QGroupBox(tr("Ports"), this);
    auto *portsLay = new QVBoxLayout(portsBox);
    m_ports = new QTableWidget(0, ColCount, portsBox);
    m_ports->setObjectName(QStringLiteral("tblMarkupPorts"));
    m_ports->setHorizontalHeaderLabels({tr("Port"), tr("Dir"), tr("From / on"), tr("To"), tr("Z0 [\u03a9]"), tr("V")});
    m_ports->verticalHeader()->hide();
    m_ports->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ports->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ports->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ports->horizontalHeader()->setStretchLastSection(true);
    portsLay->addWidget(m_ports, 1);
    auto *portButtons = new QHBoxLayout;
    m_drawPortBtn = new QPushButton(tr("Draw port"), portsBox);
    m_drawPortBtn->setObjectName(QStringLiteral("btnMarkupDrawPort"));
    m_drawPortBtn->setCheckable(true);
    m_drawPortBtn->setToolTip(tr("Drag over the port area in the layout: a rectangle gives a via port, a thin "
                                 "line an edge (in-plane) port. Corners snap to the layout. Esc cancels."));
    m_removePortBtn = new QPushButton(tr("Remove port"), portsBox);
    m_removePortBtn->setObjectName(QStringLiteral("btnMarkupRemovePort"));
    m_removePortBtn->setEnabled(false);
    m_rulerBtn = new QPushButton(tr("Ruler"), portsBox);
    m_rulerBtn->setObjectName(QStringLiteral("btnMarkupRuler"));
    m_rulerBtn->setCheckable(true);
    m_rulerBtn->setToolTip(tr("Measure in the layout (M): click the start and the end point; points snap to "
                              "layout vertices. Esc clears."));
    portButtons->addWidget(m_drawPortBtn);
    portButtons->addWidget(m_removePortBtn);
    portButtons->addWidget(m_rulerBtn);
    portButtons->addStretch(1);
    portsLay->addLayout(portButtons);
    lay->addWidget(portsBox, 1);

    m_problems = new QLabel(this);
    m_problems->setObjectName(QStringLiteral("lblMarkupProblems"));
    m_problems->setWordWrap(true);
    m_problems->setStyleSheet(QStringLiteral("color: #c00000;"));
    lay->addWidget(m_problems);

    connect(m_drawRegionBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        if (on)
            setDrawTool(1);
        emit drawToolRequested(on ? 1 : 0);
    });
    connect(m_drawPortBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        if (on)
            setDrawTool(2);
        emit drawToolRequested(on ? 2 : 0);
    });
    connect(m_rulerBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updating)
            emit rulerRequested(on);
    });
    connect(m_importBtn, &QPushButton::clicked, this, &EmMarkupPanel::importRequested);
    connect(m_regionBox, &QGroupBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        if (on && !m_markup.hasRegion && m_x1->value() == m_x2->value() && !m_layoutBounds.isNull()) {
            QSignalBlocker b1(m_x1), b2(m_y1), b3(m_x2), b4(m_y2);
            m_x1->setValue(m_layoutBounds.left());
            m_y1->setValue(m_layoutBounds.top());
            m_x2->setValue(m_layoutBounds.right());
            m_y2->setValue(m_layoutBounds.bottom());
        }
        onRegionEdited();
    });
    for (QDoubleSpinBox *box : {m_x1, m_y1, m_x2, m_y2})
        connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
            if (!m_updating)
                onRegionEdited();
        });
    connect(m_fitBtn, &QPushButton::clicked, this, [this]() {
        if (m_layoutBounds.isNull())
            return;
        m_updating = true;
        m_regionBox->setChecked(true);
        m_x1->setValue(m_layoutBounds.left());
        m_y1->setValue(m_layoutBounds.top());
        m_x2->setValue(m_layoutBounds.right());
        m_y2->setValue(m_layoutBounds.bottom());
        m_updating = false;
        onRegionEdited();
    });
    connect(m_ports, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removePortBtn->setEnabled(!m_ports->selectedItems().isEmpty() || m_ports->currentRow() >= 0);
        if (!m_updating)
            emit portSelected(selectedPort());
    });
    connect(m_removePortBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_ports->currentRow();
        if (row < 0 || row >= m_markup.ports.size())
            return;
        m_markup.ports.removeAt(row);
        rebuild();
        emitEdited();
    });

    rebuild();
}

void EmMarkupPanel::setMarkup(const EmMarkup &markup)
{
    m_markup = markup;
    rebuild();
}

void EmMarkupPanel::setMetals(const QStringList &metals)
{
    if (metals == m_metals)
        return;
    m_metals = metals;
    rebuild();
}

void EmMarkupPanel::setLayoutBounds(const QRectF &boundsUm)
{
    m_layoutBounds = boundsUm.normalized();
    m_fitBtn->setEnabled(!m_layoutBounds.isNull());
}

void EmMarkupPanel::setLayoutHasMarkup(bool has)
{
    m_importBtn->setEnabled(has);
}

void EmMarkupPanel::setStatus(const QString &text)
{
    m_status->setText(text);
}

QStringList EmMarkupPanel::problems() const
{
    return m_markup.problems(m_metals);
}

void EmMarkupPanel::setDrawTool(int tool)
{
    const bool old = m_updating;
    m_updating = true;
    m_drawRegionBtn->setChecked(tool == 1);
    m_drawPortBtn->setChecked(tool == 2);
    m_updating = old;
}

void EmMarkupPanel::setRulerArmed(bool on)
{
    const bool old = m_updating;
    m_updating = true;
    m_rulerBtn->setChecked(on);
    m_updating = old;
}

void EmMarkupPanel::setRegion(const QRectF &regionUm)
{
    m_markup.hasRegion = !regionUm.isEmpty();
    m_markup.regionUm = m_markup.hasRegion ? regionUm.normalized() : QRectF();
    rebuild();
    emitEdited();
}

void EmMarkupPanel::addPort(EmMarkupPort port)
{
    if (port.number < 1 || m_markup.port(port.number))
        port.number = m_markup.nextFreePortNumber();
    if (port.number < 1)
        return;
    m_markup.ports << port;
    std::sort(m_markup.ports.begin(), m_markup.ports.end(),
              [](const EmMarkupPort &a, const EmMarkupPort &b) { return a.number < b.number; });
    rebuild();
    for (int row = 0; row < m_markup.ports.size(); ++row)
        if (m_markup.ports.at(row).number == port.number)
            m_ports->selectRow(row);
    emitEdited();
}

void EmMarkupPanel::removePort(int number)
{
    for (int i = 0; i < m_markup.ports.size(); ++i) {
        if (m_markup.ports.at(i).number == number) {
            m_markup.ports.removeAt(i);
            rebuild();
            emitEdited();
            return;
        }
    }
}

void EmMarkupPanel::setPortShapes(int number, const QVector<QPolygonF> &shapesUm)
{
    for (EmMarkupPort &p : m_markup.ports) {
        if (p.number == number) {
            p.shapesUm = shapesUm;
            refreshProblems();
            emitEdited();
            return;
        }
    }
}

void EmMarkupPanel::selectPort(int number)
{
    const bool old = m_updating;
    m_updating = true;
    m_ports->clearSelection();
    for (int row = 0; row < m_markup.ports.size(); ++row) {
        if (m_markup.ports.at(row).number == number) {
            m_ports->selectRow(row);
            m_ports->scrollToItem(m_ports->item(row, ColPort));
        }
    }
    m_removePortBtn->setEnabled(number > 0 && m_markup.port(number));
    m_updating = old;
}

int EmMarkupPanel::selectedPort() const
{
    const QModelIndexList rows = m_ports->selectionModel() ? m_ports->selectionModel()->selectedRows()
                                                           : QModelIndexList();
    const int row = !rows.isEmpty() ? rows.first().row() : -1;
    return row >= 0 && row < m_markup.ports.size() ? m_markup.ports.at(row).number : 0;
}

void EmMarkupPanel::rebuild()
{
    m_updating = true;
    m_regionBox->setChecked(m_markup.hasRegion);
    if (m_markup.hasRegion) {
        m_x1->setValue(m_markup.regionUm.left());
        m_y1->setValue(m_markup.regionUm.top());
        m_x2->setValue(m_markup.regionUm.right());
        m_y2->setValue(m_markup.regionUm.bottom());
    }
    m_ports->clearContents();
    m_ports->setRowCount(m_markup.ports.size());
    for (int row = 0; row < m_markup.ports.size(); ++row)
        fillPortRow(row);
    m_removePortBtn->setEnabled(false);
    m_updating = false;
    refreshProblems();
}

void EmMarkupPanel::fillPortRow(int row)
{
    const EmMarkupPort &p = m_markup.ports.at(row);
    auto *num = new QTableWidgetItem(QStringLiteral("P%1").arg(p.number));
    num->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    num->setToolTip(p.summary());
    m_ports->setItem(row, ColPort, num);

    auto *dir = new QComboBox(m_ports);
    dir->addItems(EmMarkupPort::directions());
    dir->setCurrentText(p.direction);
    m_ports->setCellWidget(row, ColDir, dir);

    auto *from = layerCombo(m_metals, p.isVia() ? p.fromLayer : p.target, m_ports);
    m_ports->setCellWidget(row, ColFrom, from);
    auto *to = layerCombo(m_metals, p.toLayer, m_ports);
    to->setEnabled(p.isVia());
    m_ports->setCellWidget(row, ColTo, to);

    auto *z0 = new QDoubleSpinBox(m_ports);
    z0->setRange(0.001, 1e6);
    z0->setDecimals(3);
    z0->setValue(p.z0);
    z0->setKeyboardTracking(false);
    m_ports->setCellWidget(row, ColZ0, z0);
    auto *v = new QDoubleSpinBox(m_ports);
    v->setRange(0.0, 1e3);
    v->setDecimals(3);
    v->setValue(p.voltage);
    v->setKeyboardTracking(false);
    v->setToolTip(tr("Excitation voltage; 0 = port not excited"));
    m_ports->setCellWidget(row, ColV, v);

    auto edited = [this, row]() {
        if (!m_updating)
            onPortRowEdited(row);
    };
    connect(dir, &QComboBox::currentTextChanged, this, edited);
    connect(from, &QComboBox::currentTextChanged, this, edited);
    connect(to, &QComboBox::currentTextChanged, this, edited);
    connect(z0, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, edited);
    connect(v, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, edited);
}

void EmMarkupPanel::refreshProblems()
{
    const QStringList list = problems();
    m_problems->setText(list.isEmpty() ? QString() : QStringLiteral("\u2717 ") + list.join(QStringLiteral("\n\u2717 ")));
    m_problems->setVisible(!list.isEmpty());
    for (int row = 0; row < m_markup.ports.size(); ++row)
        if (QTableWidgetItem *num = m_ports->item(row, ColPort))
            num->setToolTip(m_markup.ports.at(row).summary());
}

void EmMarkupPanel::onRegionEdited()
{
    m_markup.hasRegion = m_regionBox->isChecked();
    m_markup.regionUm = m_markup.hasRegion
            ? QRectF(QPointF(m_x1->value(), m_y1->value()), QPointF(m_x2->value(), m_y2->value())).normalized()
            : QRectF();
    emitEdited();
}

void EmMarkupPanel::onPortRowEdited(int row)
{
    if (row < 0 || row >= m_markup.ports.size())
        return;
    EmMarkupPort &p = m_markup.ports[row];
    auto *dir = qobject_cast<QComboBox *>(m_ports->cellWidget(row, ColDir));
    auto *from = qobject_cast<QComboBox *>(m_ports->cellWidget(row, ColFrom));
    auto *to = qobject_cast<QComboBox *>(m_ports->cellWidget(row, ColTo));
    auto *z0 = qobject_cast<QDoubleSpinBox *>(m_ports->cellWidget(row, ColZ0));
    auto *v = qobject_cast<QDoubleSpinBox *>(m_ports->cellWidget(row, ColV));
    if (!dir || !from || !to || !z0 || !v)
        return;
    const bool wasVia = p.isVia();
    p.direction = dir->currentText();
    const QString first = from->currentText().trimmed();
    if (p.isVia()) {
        p.fromLayer = first;
        p.toLayer = to->currentText().trimmed();
        if (!wasVia)
            p.target.clear();
    } else {
        p.target = first;
        if (wasVia) {
            p.fromLayer.clear();
            p.toLayer.clear();
        }
    }
    to->setEnabled(p.isVia());
    p.z0 = z0->value();
    p.voltage = v->value();
    emitEdited();
}

void EmMarkupPanel::emitEdited()
{
    refreshProblems();
    emit markupEdited(m_markup);
}
