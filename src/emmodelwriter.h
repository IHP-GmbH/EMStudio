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

#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QVector>

/*! One EM port for EmModel + lookalike symbol (Touchstone index = i+1 when index unset). */
struct EmModelPublishPort
{
    QString name;
    int     index = 0; //!< 1-based; 0 → order in the list
    double  xUm = 0.0;
    double  yUm = 0.0;
    bool    hasPosition = false;
};

/*! Fields collected on the Output page for ViewType::EmModel publish. */
struct EmModelPublishFields
{
    QString     outputPath;
    QString     cellName;
    QString     snpPath;
    QString     variant;
    QString     tool;
    QString     modelPath;
    QString     layoutPath;
    QString     substratePath;
    double      z0 = 50.0;
    QVector<EmModelPublishPort> ports;
    /*! Optional lookalike body from GDS flatten (µm); used when layout.room load fails. */
    QVector<QPolygonF> outlinePolysUm;
    bool        writeLookalikeSymbol = true; //!< Also write `<cell>.symbol.room`
};

/*! Result of EmModel (+ optional lookalike symbol) publish. */
struct EmModelPublishResult
{
    QString error;          //!< Empty on success
    QString emmodelPath;
    QString symbolPath;     //!< Set when a lookalike symbol was written
};

/*! True when EMStudio was built with CommonDB ROOM (can write .emmodel.room). */
bool emModelWriterAvailable();

/*!
 * Writes \a fields to a ROOM EmModel file at \a fields.outputPath.
 * When \a fields.writeLookalikeSymbol is true, also writes a layout-lookalike
 * `<cell>.symbol.room` beside it (outline from layout.room or outlinePolysUm).
 * \return result.error empty on success.
 */
EmModelPublishResult writeEmModelRoomFile(const EmModelPublishFields &fields);
