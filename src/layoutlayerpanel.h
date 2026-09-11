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
        qreal   opacity = 1.0; // 0..1 multiplier on fill alpha
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

signals:
    void                        visibilityChanged(int gdsLayer, bool visible);
    void                        opacityChanged(int gdsLayer, qreal opacity);
    void                        layerActivated(const QString &name, const QString &kind);
    void                        showCoordinatesToggled(bool on);
    void                        usedLayersOnlyToggled(bool on);

private slots:
    void                        onUsedOnlyToggled(bool on);
    void                        onItemChanged(QListWidgetItem *item);
    void                        onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void                        onOpacitySlider(int value);
    void                        onListContextMenu(const QPoint &pos);
    void                        setAllVisible(bool visible);

private:
    void                        rebuildList();
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
};

#endif // QT_VERSION

#endif // LAYOUTLAYERPANEL_H
