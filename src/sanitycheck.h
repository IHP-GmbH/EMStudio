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

#ifndef SANITYCHECK_H
#define SANITYCHECK_H

#include <QString>
#include <QVector>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \struct SanityFinding
 * \brief One pre-run sanity finding (error or warning).
 **********************************************************************************************************************/
struct SanityFinding
{
    enum Severity { Error, Warning };

    Severity severity = Warning;
    QString  code;
    QString  message;
};

/*!*******************************************************************************************************************
 * Port direction vs from/to layer assignment rules (aligned with gds2palace).
 * Appends findings for a single port; returns nothing when the row is consistent.
 **********************************************************************************************************************/
void appendPortDirectionFindings(QVector<SanityFinding> &out,
                                 int portNumber,
                                 const QString &direction,
                                 const QString &fromLayer,
                                 const QString &toLayer);

/*!*******************************************************************************************************************
 * \brief Shows a modal list of findings. Returns true if the user chose Run anyway.
 *
 * If \a findings is empty, returns true immediately (no dialog).
 **********************************************************************************************************************/
bool showSanityCheckDialog(QWidget *parent, const QVector<SanityFinding> &findings);

#endif // QT_VERSION

#endif // SANITYCHECK_H
