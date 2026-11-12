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

#ifndef EMMARKUP_H
#define EMMARKUP_H

#include <QRectF>
#include <QString>
#include <QVector>
#include <QPolygonF>
#include <QStringList>
#include <QJsonObject>

#include "gdslayout.h"

/*!*******************************************************************************************************************
 * \struct EmMarkupPort
 * \brief One EM port of the layout markup: settings plus its footprint shapes.
 *
 * Settings follow klayout_em_export (property 200 of a port shape):
 *   \c port:dir=-Z;from=Metal3;to=TopMetal2;z0=50;v=1   via port (Z, -Z)
 *   \c port:dir=X;target=Metal5;z0=50;v=1               in-plane port (X, Y, -X, -Y)
 **********************************************************************************************************************/
struct EmMarkupPort
{
    int                 number = 0;            //!< Port number (GDS layer 200 + number at export)
    QString             direction = QStringLiteral("Z");
    QString             fromLayer;             //!< Via port: lower / start metal
    QString             toLayer;               //!< Via port: end metal
    QString             target;                //!< In-plane port: metal the port sits on
    double              z0 = 50.0;             //!< Port impedance [Ohm]
    double              voltage = 1.0;         //!< Excitation; 0 = not excited
    QVector<QPolygonF>  shapesUm;              //!< Footprints in GDS µm (Y-up)

    bool                isVia() const { return direction.contains(QLatin1Char('Z'), Qt::CaseInsensitive); }
    /*! Property 200 text as klayout_em_export writes it. */
    QString             encode() const;
    /*! Reads settings from a property 200 text; false when it is not a port setting. */
    static bool         decode(const QString &text, EmMarkupPort *port);
    /*! Problems of the settings (bad direction, missing / unknown / equal layers); empty when fine. */
    QStringList         problems(const QStringList &metals = {}) const;
    /*! Short description, e.g. "P1  -Z  Metal3 → TopMetal2, 50 Ω". */
    QString             summary() const;

    static const QStringList &directions();
};

/*!*******************************************************************************************************************
 * \struct EmMarkup
 * \brief EM markup of a layout (region, ports) kept beside the model, not in the design GDS.
 *
 * The markup is stored in a sidecar \c <model>.markup.json and drawn as an overlay in LayoutView.
 * A layout already annotated by the klayout_em_export KLayout plugin (ports on layers 201..299 with
 * property 200, EM region box on 800/0) is imported with \c fromLayout.
 **********************************************************************************************************************/
struct EmMarkup
{
    static constexpr int kPortPropertyKey = 200;
    static constexpr int kFirstPortLayer = 201;
    static constexpr int kLastPortLayer = 299;
    static constexpr int kRegionLayer = 800;
    static constexpr int kRegionDatatype = 0;

    QString                 layoutFile;        //!< Layout the markup was made for (informative)
    QString                 cell;              //!< Top cell of that layout
    bool                    hasRegion = false;
    QRectF                  regionUm;          //!< EM region in GDS µm, Y-up, normalized
    QVector<EmMarkupPort>   ports;

    bool                    isEmpty() const { return !hasRegion && ports.isEmpty(); }
    bool                    operator==(const EmMarkup &o) const;
    bool                    operator!=(const EmMarkup &o) const { return !(*this == o); }

    /*! Port with \a number, or nullptr. */
    const EmMarkupPort     *port(int number) const;
    /*! Smallest unused port number (1..99), 0 when all are taken. */
    int                     nextFreePortNumber() const;
    /*! Problems of the whole markup (per-port problems, duplicate numbers, ports outside the region). */
    QStringList             problems(const QStringList &metals = {}) const;

    QJsonObject             toJson() const;
    static EmMarkup         fromJson(const QJsonObject &obj);
    bool                    save(const QString &path, QString *errorMsg = nullptr) const;
    static bool             load(const QString &path, EmMarkup *out, QString *errorMsg = nullptr);
    /*! Sidecar of a model script: \c <dir>/<basename>.markup.json; empty for an empty path. */
    static QString          sidecarPath(const QString &modelPath);

    /*!
     * \brief Markup drawn into a layout by klayout_em_export.
     *
     * Ports: shapes on layers 201..299 (datatype 0) whose property 200 holds port settings; layers
     * without settings are EMStudio marker layers and are not taken. Region: bounding box of the
     * shapes on 800/0.
     *
     * \param polys     Flattened layout polygons (properties included).
     * \param warnings  Optional notes, e.g. port shapes with conflicting settings.
     */
    static EmMarkup         fromLayout(const QVector<GdsFlatPolygon> &polys, QStringList *warnings = nullptr);
    /*! True when \a polys hold klayout_em_export markup (cheap scan, no copies). */
    static bool             layoutHasMarkup(const QVector<GdsFlatPolygon> &polys);
};

#endif // EMMARKUP_H
