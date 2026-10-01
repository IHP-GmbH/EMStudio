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
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
#include <QEvent>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <climits>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

namespace {
constexpr int kRoleGds = Qt::UserRole;
constexpr int kRoleName = Qt::UserRole + 1;
constexpr int kRoleKind = Qt::UserRole + 2;
constexpr int kGdsAllLayers = INT_MIN; // kRoleGds of the "All layers" row
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
    // Esc / click below the last row → back to "All layers".
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);

    // True 2D fill opacity in percent (Field image is not affected).
    m_opacityLabel = new QLabel(tr("Opacity"), this);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    m_opacity->setEnabled(false);
    connect(m_opacity, &QSlider::valueChanged, this, &LayoutLayerPanel::onOpacitySlider);
    connect(m_opacity, &QSlider::sliderReleased, this, &LayoutLayerPanel::flushPendingOpacity);
    m_opacitySettle = new QTimer(this);
    m_opacitySettle->setSingleShot(true);
    m_opacitySettle->setInterval(300);
    connect(m_opacitySettle, &QTimer::timeout, this, &LayoutLayerPanel::flushPendingOpacity);

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
    m_block = false;
    updateOpacityControls();
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
    if (match)
        m_list->setCurrentItem(match);
    m_block = false;
    if (match)
        onCurrentItemChanged(match, nullptr);
    else
        selectAllLayersMode(false);
}

void LayoutLayerPanel::clearHighlight()
{
    selectAllLayersMode(false);
}

/*!*******************************************************************************************************************
 * \brief Selects the "All layers" row, so the opacity slider acts on every layer.
 *
 * \param notify Emit \c layerDeactivated (user action) so other views drop their highlight.
 **********************************************************************************************************************/
void LayoutLayerPanel::selectAllLayersMode(bool notify)
{
    m_block = true;
    QListWidgetItem *all = m_list->count() > 0 && isAllLayersItem(m_list->item(0))
            ? m_list->item(0) : nullptr;
    m_list->setCurrentItem(all);
    m_block = false;
    updateOpacityControls();
    if (notify)
        emit layerDeactivated();
}

bool LayoutLayerPanel::isAllLayersItem(const QListWidgetItem *item) const
{
    return item && item->data(kRoleGds).toInt() == kGdsAllLayers;
}

/*!*******************************************************************************************************************
 * \brief Shows the true fill opacity on the slider: the selected layer's, or the average of
 *        all layers in the layout (marked "mixed" when they differ).
 **********************************************************************************************************************/
void LayoutLayerPanel::updateOpacityControls()
{
    QListWidgetItem *cur = m_list->currentItem();
    const bool allMode = !cur || isAllLayersItem(cur);

    bool enabled = false;
    qreal op = 1.0;
    QString label = tr("Opacity");
    QString tip;
    if (allMode) {
        int n = 0;
        qreal sum = 0.0, lo = 1.0, hi = 0.0;
        for (const Entry &e : m_all) {
            if (!e.used)
                continue;
            ++n;
            sum += e.opacity;
            lo = qMin(lo, e.opacity);
            hi = qMax(hi, e.opacity);
        }
        enabled = n > 0;
        if (enabled) {
            op = sum / n;
            const int pct = int(op * 100.0 + 0.5);
            label = (hi - lo > 0.005) ? tr("Opacity %1% · all layers (mixed)").arg(pct)
                                      : tr("Opacity %1% · all layers").arg(pct);
        }
        tip = tr("Fill opacity of all layout shapes (not the Field image).\n"
                 "Select a layer to change only that layer.");
    } else {
        const int gds = cur->data(kRoleGds).toInt();
        for (const Entry &e : m_all) {
            if (e.gdsLayer == gds) {
                op = e.opacity;
                enabled = e.used; // unused stackup layers are not drawn
                break;
            }
        }
        if (enabled) {
            label = tr("Opacity %1%").arg(int(op * 100.0 + 0.5));
            tip = tr("Fill opacity of the selected layer.\n"
                     "Esc or click below the list: all layers.");
        } else {
            tip = tr("Layer is not present in the current GDS — opacity has no effect.");
        }
    }

    m_block = true;
    m_opacity->setEnabled(enabled);
    m_opacity->setValue(qBound(0, int(op * 100.0 + 0.5), 100));
    m_opacity->setToolTip(tip);
    m_opacityLabel->setText(label);
    m_block = false;
}

bool LayoutLayerPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        selectAllLayersMode(true);
        return true;
    }
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonPress) {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (!m_list->itemAt(me->pos())) {
            selectAllLayersMode(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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

    if (!m_all.isEmpty()) {
        auto *all = new QListWidgetItem(tr("All layers"), m_list);
        all->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        all->setData(kRoleGds, kGdsAllLayers);
        all->setToolTip(tr("Opacity slider acts on all layers (also: Esc, or click below the list)."));
        QFont f = all->font();
        f.setBold(true);
        all->setFont(f);
    }

    for (const Entry &e : m_all) {
        if (m_usedOnlyOn && !e.used)
            continue;
        // Display text only; lookups use kRoleName (the plain layer name).
        const QString text = e.unmapped ? tr("%1 (not mapped)").arg(e.name) : e.name;
        auto *it = new QListWidgetItem(swatchIcon(e.color), text, m_list);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        it->setCheckState(e.visible ? Qt::Checked : Qt::Unchecked);
        it->setData(kRoleGds, e.gdsLayer);
        it->setData(kRoleName, e.name);
        it->setData(kRoleKind, e.kind);
        QString tip = tr("GDS layer %1").arg(e.gdsLayer);
        if (!e.used)
            tip += tr(" (not in layout)");
        if (e.unmapped)
            tip += tr("\nNot mapped yet: no Ports / Thermal table entry with its stackup layers, so the "
                      "layout shows it at a guessed position (height in 3D).");
        it->setToolTip(tip);
        if (!e.used || e.unmapped) {
            QFont f = it->font();
            f.setItalic(true);
            it->setFont(f);
        }
        if (e.unmapped)
            it->setForeground(QColor(170, 90, 0));
    }

    m_block = false;

    if (!keepName.isEmpty())
        setHighlightedName(keepName);
    else
        selectAllLayersMode(false);
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
    if (m_block || !item || isAllLayersItem(item))
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
    flushPendingOpacity(); // belongs to the previous selection
    updateOpacityControls();
    if (!current || isAllLayersItem(current)) {
        emit layerDeactivated();
        return;
    }
    const int gds = current->data(kRoleGds).toInt();
    for (const Entry &e : m_all) {
        if (e.gdsLayer == gds) {
            if (!e.name.isEmpty())
                emit layerActivated(e.name, e.kind);
            break;
        }
    }
}

/*!*******************************************************************************************************************
 * \brief Slider value changed: updates the entries and label, then tells the view (now or deferred).
 *
 * \param value Slider position 0..100 (percent).
 **********************************************************************************************************************/
void LayoutLayerPanel::onOpacitySlider(int value)
{
    if (m_block)
        return;
    const qreal op = value / 100.0;
    QListWidgetItem *cur = m_list->currentItem();
    int target = kGdsAllLayers;
    if (!cur || isAllLayersItem(cur)) {
        for (Entry &e : m_all)
            e.opacity = op;
    } else {
        target = cur->data(kRoleGds).toInt();
        for (Entry &e : m_all) {
            if (e.gdsLayer == target) {
                e.opacity = op;
                break;
            }
        }
    }
    updateOpacityControls();

    m_pendingGds = target;
    m_pendingOpacity = op;
    m_opacityPending = true;
    if (!m_deferOpacity) {
        flushPendingOpacity();
        return;
    }
    // Dragging: wait for sliderReleased. Wheel / keys / track click: wait until it settles.
    if (!m_opacity->isSliderDown())
        m_opacitySettle->start();
}

/*!*******************************************************************************************************************
 * \brief Emits the held-back opacity change (if any) for the layer or "All layers" it was made on.
 **********************************************************************************************************************/
void LayoutLayerPanel::flushPendingOpacity()
{
    m_opacitySettle->stop();
    if (!m_opacityPending)
        return;
    m_opacityPending = false;
    if (m_pendingGds == kGdsAllLayers)
        emit allOpacityChanged(m_pendingOpacity);
    else
        emit opacityChanged(m_pendingGds, m_pendingOpacity);
}

void LayoutLayerPanel::setDeferOpacityUpdates(bool defer)
{
    m_deferOpacity = defer;
    if (!defer)
        flushPendingOpacity();
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
        if (isAllLayersItem(it))
            continue;
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
