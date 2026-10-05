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
#include <QSet>

#include <functional>

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
    /*! "EM only": unmapped layers (\c Entry::unmapped) are not listed and hidden in the layout. */
    bool                        emLayersOnly() const;
    void                        setEmLayersOnly(bool on);
    /*!
     * \brief Defers opacity signals while the slider moves (Iso3D rebuilds are costly).
     *
     * When on, \c opacityChanged / \c allOpacityChanged are emitted when the slider is released,
     * or after the value settles for wheel / key changes; the label still follows the slider.
     */
    void                        setDeferOpacityUpdates(bool defer);
    /*! Re-reads each layer's opacity from \a opacityOf and the whole-layout opacity (e.g. after the
     *  view switched between the layout and the Field view, which keep separate layout opacities)
     *  and updates the slider. */
    void                        refreshOpacities(const std::function<qreal(int)> &opacityOf,
                                                 qreal layoutOpacity);
    bool                        deferOpacityUpdates() const { return m_deferOpacity; }

protected:
    bool                        eventFilter(QObject *watched, QEvent *event) override;

signals:
    void                        visibilityChanged(int gdsLayer, bool visible);
    /*! Show / Hide all: every listed layer at once (one redraw). */
    void                        layersVisibilityChanged(const QVector<int> &gdsLayers, bool visible);
    void                        opacityChanged(int gdsLayer, qreal opacity);
    /*! Slider moved with "All layers" selected: the layout opacity factor (\c LayoutView::setLayoutOpacity;
     *  the slider shows it times the default fill opacity, 59 % for 1). */
    void                        allOpacityChanged(qreal opacity);
    /*! The selection went back to "All layers" (Esc, empty-area click, "All layers" row). */
    void                        layerDeactivated();
    void                        layerActivated(const QString &name, const QString &kind);
    void                        showCoordinatesToggled(bool on);
    void                        usedLayersOnlyToggled(bool on);
    void                        emLayersOnlyToggled(bool on);

private slots:
    void                        onUsedOnlyToggled(bool on);
    void                        onEmOnlyToggled(bool on);
    void                        onItemChanged(QListWidgetItem *item);
    void                        onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void                        onOpacitySlider(int value);
    /*! Emits the opacity change that was held back while the slider moved. */
    void                        flushPendingOpacity();
    void                        onListContextMenu(const QPoint &pos);
    void                        setAllVisible(bool visible);
    void                        hideUnmappedLayers();

private:
    void                        rebuildList();
    /*! Hides the unmapped layers while "EM only" is on, shows again those it hid that are
     *  mapped now or when it is off. Returns the layers to hide / show (signals after rebuildList). */
    void                        applyEmOnlyVisibility(QVector<int> *hide, QVector<int> *show);
    void                        emitVisibility(const QVector<int> &hide, const QVector<int> &show);
    void                        selectAllLayersMode(bool notify);
    void                        updateOpacityControls();
    /*! Fill opacity a layer with its own opacity \a layerOpacity shows on screen under the layout
     *  opacity factor (\c m_layoutOpacity): faded below 1, more opaque above, at most 1. */
    qreal                       shownLayerOpacity(qreal layerOpacity) const;
    /*! The layer's own opacity (0..1) that shows \a shown on screen; the inverse of shownLayerOpacity. */
    qreal                       layerOpacityForShown(qreal shown) const;
    /*! Check box of the "All layers" row: checked / unchecked / partly, from the listed layers. */
    void                        updateAllLayersCheck();
    bool                        isAllLayersItem(const QListWidgetItem *item) const;
    void                        setListedVisible(bool visible, const std::function<bool(const Entry &)> &which);
    QListWidgetItem            *itemForGds(int gdsLayer) const;
    static QIcon                swatchIcon(const QColor &c);

    QCheckBox                  *m_usedOnly = nullptr;
    QCheckBox                  *m_emOnly = nullptr;
    QCheckBox                  *m_showCoords = nullptr;
    QListWidget                *m_list = nullptr;
    QLabel                     *m_title = nullptr;
    QLabel                     *m_opacityLabel = nullptr;
    QSlider                    *m_opacity = nullptr;

    QVector<Entry>              m_all;
    bool                        m_usedOnlyOn = true;
    bool                        m_emOnlyOn = false;
    QSet<int>                   m_hiddenByEmOnly;   //!< Layers "EM only" hid (shown again when off)
    qreal                       m_layoutOpacity = 1.0; //!< Layout opacity factor for "All layers" (LayoutView units)
    bool                        m_block = false;
    bool                        m_deferOpacity = false;
    bool                        m_opacityPending = false;
    int                         m_pendingGds = 0;   //!< kGdsAllLayers or a GDS layer
    qreal                       m_pendingOpacity = 1.0;
    class QTimer               *m_opacitySettle = nullptr;
};

#endif // QT_VERSION

#endif // LAYOUTLAYERPANEL_H
