/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
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
