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
#include <QStringList>
#include <QVector>

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
    QStringList portNames;   //!< Ordered port names (Touchstone index = i+1)
};

/*! True when EMStudio was built with CommonDB ROOM (can write .emmodel.room). */
bool emModelWriterAvailable();

/*!
 * Writes \a fields to a ROOM EmModel file at \a fields.outputPath.
 * \return Empty string on success; otherwise an error message.
 */
QString writeEmModelRoomFile(const EmModelPublishFields &fields);
