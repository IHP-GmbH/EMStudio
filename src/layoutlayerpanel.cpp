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

#include "layoutlayerpanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QAbstractItemView>
#include <QFont>
#include <QMenu>
#include <QAction>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

namespace {
constexpr int kRoleGds = Qt::UserRole;
constexpr int kRoleName = Qt::UserRole + 1;
constexpr int kRoleKind = Qt::UserRole + 2;
}

LayoutLayerPanel::LayoutLayerPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(160);
    // No max width — panel stretches with the Layout|Layers splitter.

    m_title = new QLabel(tr("Layers"), this);
    m_title->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_title->setVisible(false); // title lives next to "Layout" in MainWindow header row

    m_usedOnly = new QCheckBox(tr("Used layers only"), this);
    m_usedOnly->setChecked(true);
    m_usedOnly->setToolTip(tr("Hide stackup XML layers that do not appear in the current GDS layout."));
    connect(m_usedOnly, &QCheckBox::toggled, this, &LayoutLayerPanel::onUsedOnlyToggled);

    m_showCoords = new QCheckBox(tr("Show coordinates"), this);
    m_showCoords->setChecked(true);
    m_showCoords->setToolTip(tr("Show X/Y (µm) next to the cursor in the Layout preview."));
    connect(m_showCoords, &QCheckBox::toggled, this, &LayoutLayerPanel::showCoordinatesToggled);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemChanged, this, &LayoutLayerPanel::onItemChanged);
    connect(m_list, &QListWidget::currentItemChanged, this, &LayoutLayerPanel::onCurrentItemChanged);
    connect(m_list, &QWidget::customContextMenuRequested, this, &LayoutLayerPanel::onListContextMenu);

    m_opacityLabel = new QLabel(tr("Opacity"), this);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(10, 100);
    m_opacity->setValue(100);
    m_opacity->setEnabled(false);
    m_opacity->setToolTip(tr("Fill opacity for the selected layer (layout preview)."));
    connect(m_opacity, &QSlider::valueChanged, this, &LayoutLayerPanel::onOpacitySlider);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(m_title);

    auto *opts = new QHBoxLayout();
    opts->setContentsMargins(0, 0, 0, 0);
    opts->setSpacing(8);
    opts->addWidget(m_usedOnly);
    opts->addWidget(m_showCoords);
    opts->addStretch(1);
    lay->addLayout(opts);

    lay->addWidget(m_list, 1);
    lay->addWidget(m_opacityLabel);
    lay->addWidget(m_opacity);
}

void LayoutLayerPanel::setTitleVisible(bool visible)
{
    if (m_title)
        m_title->setVisible(visible);
}

bool LayoutLayerPanel::showCoordinates() const
{
    return m_showCoords && m_showCoords->isChecked();
}

void LayoutLayerPanel::setShowCoordinates(bool on)
{
    if (m_showCoords)
        m_showCoords->setChecked(on);
}

bool LayoutLayerPanel::usedLayersOnly() const
{
    return m_usedOnly && m_usedOnly->isChecked();
}

void LayoutLayerPanel::setUsedLayersOnly(bool on)
{
    if (m_usedOnly)
        m_usedOnly->setChecked(on);
}

void LayoutLayerPanel::clear()
{
    m_all.clear();
    m_block = true;
    m_list->clear();
    m_opacity->setEnabled(false);
    m_opacity->setValue(100);
    m_block = false;
}

void LayoutLayerPanel::setLayers(const QVector<Entry> &layers)
{
    m_all = layers;
    rebuildList();
}

void LayoutLayerPanel::setHighlightedName(const QString &name)
{
    m_block = true;
    QListWidgetItem *match = nullptr;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        if (it->data(kRoleName).toString() == name) {
            match = it;
            break;
        }
    }
    m_list->setCurrentItem(match);
    m_block = false;
    if (match)
        onCurrentItemChanged(match, nullptr);
}

void LayoutLayerPanel::clearHighlight()
{
    m_block = true;
    m_list->setCurrentItem(nullptr);
    m_opacity->setEnabled(false);
    m_block = false;
}

void LayoutLayerPanel::onUsedOnlyToggled(bool on)
{
    m_usedOnlyOn = on;
    rebuildList();
    emit usedLayersOnlyToggled(on);
}

void LayoutLayerPanel::rebuildList()
{
    const QString keepName = m_list->currentItem()
            ? m_list->currentItem()->data(kRoleName).toString()
            : QString();

    m_block = true;
    m_list->clear();

    for (const Entry &e : m_all) {
        if (m_usedOnlyOn && !e.used)
            continue;
        auto *it = new QListWidgetItem(swatchIcon(e.color), e.name, m_list);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        it->setCheckState(e.visible ? Qt::Checked : Qt::Unchecked);
        it->setData(kRoleGds, e.gdsLayer);
        it->setData(kRoleName, e.name);
        it->setData(kRoleKind, e.kind);
        QString tip = tr("GDS layer %1").arg(e.gdsLayer);
        if (!e.used)
            tip += tr(" (not in layout)");
        it->setToolTip(tip);
        if (!e.used) {
            QFont f = it->font();
            f.setItalic(true);
            it->setFont(f);
        }
    }

    m_block = false;

    if (!keepName.isEmpty())
        setHighlightedName(keepName);
    else
        m_opacity->setEnabled(false);
}

QListWidgetItem *LayoutLayerPanel::itemForGds(int gdsLayer) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        if (it->data(kRoleGds).toInt() == gdsLayer)
            return it;
    }
    return nullptr;
}

QIcon LayoutLayerPanel::swatchIcon(const QColor &c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    QColor fill = c;
    fill.setAlpha(200);
    p.fillRect(1, 1, 14, 14, fill);
    p.setPen(QColor(40, 40, 40));
    p.drawRect(1, 1, 13, 13);
    p.end();
    return QIcon(pm);
}

void LayoutLayerPanel::onItemChanged(QListWidgetItem *item)
{
    if (m_block || !item)
        return;
    const int gds = item->data(kRoleGds).toInt();
    const bool vis = item->checkState() == Qt::Checked;
    for (Entry &e : m_all) {
        if (e.gdsLayer == gds) {
            e.visible = vis;
            break;
        }
    }
    emit visibilityChanged(gds, vis);
}

void LayoutLayerPanel::onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *)
{
    if (m_block)
        return;
    if (!current) {
        m_opacity->setEnabled(false);
        return;
    }
    const int gds = current->data(kRoleGds).toInt();
    qreal op = 1.0;
    QString name, kind;
    for (const Entry &e : m_all) {
        if (e.gdsLayer == gds) {
            op = e.opacity;
            name = e.name;
            kind = e.kind;
            break;
        }
    }
    m_block = true;
    m_opacity->setEnabled(true);
    m_opacity->setValue(qBound(10, int(op * 100.0 + 0.5), 100));
    m_opacityLabel->setText(tr("Opacity %1%").arg(m_opacity->value()));
    m_block = false;
    if (!name.isEmpty())
        emit layerActivated(name, kind);
}

void LayoutLayerPanel::onOpacitySlider(int value)
{
    if (m_block)
        return;
    auto *cur = m_list->currentItem();
    if (!cur)
        return;
    const int gds = cur->data(kRoleGds).toInt();
    const qreal op = value / 100.0;
    m_opacityLabel->setText(tr("Opacity %1%").arg(value));
    for (Entry &e : m_all) {
        if (e.gdsLayer == gds) {
            e.opacity = op;
            break;
        }
    }
    emit opacityChanged(gds, op);
}

void LayoutLayerPanel::onListContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *showAll = menu.addAction(tr("Show All"));
    QAction *hideAll = menu.addAction(tr("Hide All"));
    QAction *chosen = menu.exec(m_list->mapToGlobal(pos));
    if (chosen == showAll)
        setAllVisible(true);
    else if (chosen == hideAll)
        setAllVisible(false);
}

void LayoutLayerPanel::setAllVisible(bool visible)
{
    // Apply to layers currently shown in the list (respects "Used layers only").
    m_block = true;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        const int gds = it->data(kRoleGds).toInt();
        it->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
        for (Entry &e : m_all) {
            if (e.gdsLayer == gds) {
                e.visible = visible;
                break;
            }
        }
        emit visibilityChanged(gds, visible);
    }
    m_block = false;
}

#endif // QT_VERSION
