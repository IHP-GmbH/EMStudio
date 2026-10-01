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

#ifndef LAYOUTLAYERPANEL_H
#define LAYOUTLAYERPANEL_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QString>
#include <QHash>

class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QSlider;
class QLabel;

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \class LayoutLayerPanel
 * \brief Layer list for the GDS layout preview (visibility, opacity, used-only filter).
 *
 * Shown to the right of LayoutView on the Substrate tab. Inspired by KLayout's Layers panel.
 * The opacity slider shows the true 2D fill opacity: for the selected layer, or for all
 * layers when "All layers" (the first row) or nothing is selected.
 **********************************************************************************************************************/
class LayoutLayerPanel : public QWidget
{
    Q_OBJECT

public:
    struct Entry {
        int     gdsLayer = -1;
        QString name;
        QString kind;
        QColor  color;
        bool    used = true;   // present in flattened GDS
        bool    visible = true;
        qreal   opacity = 1.0; // true 2D fill opacity 0..1 (LayoutView::layerOpacity)
        bool    unmapped = false; // port / thermal marker without stackup layers: guessed position
    };

    explicit LayoutLayerPanel(QWidget *parent = nullptr);

    void                        setLayers(const QVector<Entry> &layers);
    void                        clear();
    void                        setHighlightedName(const QString &name);
    void                        clearHighlight();
    void                        setTitleVisible(bool visible);
    bool                        showCoordinates() const;
    void                        setShowCoordinates(bool on);
    bool                        usedLayersOnly() const;
    void                        setUsedLayersOnly(bool on);
    /*!
     * \brief Defers opacity signals while the slider moves (Iso3D rebuilds are costly).
     *
     * When on, \c opacityChanged / \c allOpacityChanged are emitted when the slider is released,
     * or after the value settles for wheel / key changes; the label still follows the slider.
     */
    void                        setDeferOpacityUpdates(bool defer);
    bool                        deferOpacityUpdates() const { return m_deferOpacity; }

protected:
    bool                        eventFilter(QObject *watched, QEvent *event) override;

signals:
    void                        visibilityChanged(int gdsLayer, bool visible);
    void                        opacityChanged(int gdsLayer, qreal opacity);
    /*! Slider moved with no single layer selected: every layer gets \a opacity. */
    void                        allOpacityChanged(qreal opacity);
    /*! The selection went back to "All layers" (Esc, empty-area click, "All layers" row). */
    void                        layerDeactivated();
    void                        layerActivated(const QString &name, const QString &kind);
    void                        showCoordinatesToggled(bool on);
    void                        usedLayersOnlyToggled(bool on);

private slots:
    void                        onUsedOnlyToggled(bool on);
    void                        onItemChanged(QListWidgetItem *item);
    void                        onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void                        onOpacitySlider(int value);
    /*! Emits the opacity change that was held back while the slider moved. */
    void                        flushPendingOpacity();
    void                        onListContextMenu(const QPoint &pos);
    void                        setAllVisible(bool visible);

private:
    void                        rebuildList();
    void                        selectAllLayersMode(bool notify);
    void                        updateOpacityControls();
    bool                        isAllLayersItem(const QListWidgetItem *item) const;
    QListWidgetItem            *itemForGds(int gdsLayer) const;
    static QIcon                swatchIcon(const QColor &c);

    QCheckBox                  *m_usedOnly = nullptr;
    QCheckBox                  *m_showCoords = nullptr;
    QListWidget                *m_list = nullptr;
    QLabel                     *m_title = nullptr;
    QLabel                     *m_opacityLabel = nullptr;
    QSlider                    *m_opacity = nullptr;

    QVector<Entry>              m_all;
    bool                        m_usedOnlyOn = true;
    bool                        m_block = false;
    bool                        m_deferOpacity = false;
    bool                        m_opacityPending = false;
    int                         m_pendingGds = 0;   //!< kGdsAllLayers or a GDS layer
    qreal                       m_pendingOpacity = 1.0;
    class QTimer               *m_opacitySettle = nullptr;
};

#endif // QT_VERSION

#endif // LAYOUTLAYERPANEL_H
