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

#include <QString>

/*! Layout input kind for the Main-tab Layout File field (GDS or ROOM). */
enum class LayoutFileKind {
    Unknown,
    Gds,
    Room
};

LayoutFileKind layoutFileKind(const QString &path);
bool           isRoomLayoutPath(const QString &path);
bool           isGdsLayoutPath(const QString &path);

/*! File dialog filter for Layout File browse. */
QString layoutFileDialogFilter();

/*!
 * Converts a ROOM layout to a cached GDS via \a roomToGdsExe (\c room_to_gds).
 * \return Absolute GDS path on success; empty on failure (\a errorMsg set).
 */
QString materializeRoomLayoutGds(const QString &roomPath,
                                 const QString &roomToGdsExe,
                                 QString *errorMsg = nullptr);

/*! Replace layout path forms in a model script with \a toPath (for pre-run Room→GDS). */
int rewriteLayoutPathInScript(QString *script, const QString &fromPath, const QString &toPath);
