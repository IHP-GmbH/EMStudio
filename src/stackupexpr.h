/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
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

#ifndef STACKUPEXPR_H
#define STACKUPEXPR_H

#include <QHash>
#include <QString>
#include <QVariant>

/*!*******************************************************************************************************************
 * \brief Evaluates stackup attribute expressions (literals, "=expr", variable names).
 *
 * Supports +, -, *, /, parentheses and identifiers that resolve from \a vars.
 * String-typed variables are returned as QString when the whole expression is a bare name.
 **********************************************************************************************************************/
class StackupExpr
{
public:
    static bool isExpression(const QString &raw);
    static QString stripEquals(const QString &raw);

    static bool evalNumber(const QString &raw,
                           const QHash<QString, QVariant> &vars,
                           double *out,
                           QString *error = nullptr);

    static QVariant eval(const QString &raw,
                         const QHash<QString, QVariant> &vars,
                         QString *error = nullptr);
};

#endif // STACKUPEXPR_H
