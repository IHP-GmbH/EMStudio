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

#ifndef EXPRPARSER_H
#define EXPRPARSER_H

#include <QString>

#include <QtGlobal>
#include <functional>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \brief π-model capacitance extracted from a 2-port Y matrix (Results calculator).
 **********************************************************************************************************************/
enum class CapKind { Cser, Csh1, Csh2 };

/*!*******************************************************************************************************************
 * \brief Series L / R / Q from Y, or S21 phase delay (Results calculator).
 **********************************************************************************************************************/
enum class IndKind { Lser, Rser, Q, Delay };

/*!*******************************************************************************************************************
 * \class ExprParser
 * \brief Recursive-descent parser for Results calculator expressions.
 *
 * Supports arithmetic (\c + − * /), parentheses, and RF helpers such as
 * \c cser($1), \c lser($1), \c delay($1), \c ydiff_cser($1,$2), \c db(S21,$1).
 * Trace indices \c $N are resolved through the callbacks supplied at construction.
 **********************************************************************************************************************/
class ExprParser
{
public:
    using CapFn = std::function<bool(CapKind, int /*trace1based*/,
                                     double /*fGHz*/, double *, QString *)>;
    using YdiffFn = std::function<bool(CapKind, int, int,
                                       double /*fGHz*/, double *, QString *)>;
    using DbPhFn = std::function<bool(bool /*db*/, int m, int n, int trace1based,
                                      double /*fGHz*/, double *, QString *)>;
    /*! t2==0 → single-trace; t2>0 → Ya−Yb style (used for \c ydiff_lser). */
    using IndFn = std::function<bool(IndKind, int /*t1*/, int /*t2*/,
                                     double /*fGHz*/, double *, QString *)>;

    /*!*******************************************************************************************************************
     * \brief Constructs a parser for \a text using \a defaultFGhz when a call omits frequency.
     **********************************************************************************************************************/
    ExprParser(QString text, double defaultFGhz,
               CapFn cap, YdiffFn ydiff, DbPhFn dbph, IndFn ind);

    /*!*******************************************************************************************************************
     * \brief Parses the full expression into \a out.
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parse(double *out, QString *err);

private:
    void                            skip();
    bool                            parseExpr(double *out, QString *err);
    bool                            parseTerm(double *out, QString *err);
    bool                            parseFactor(double *out, QString *err);
    bool                            parseNumber(double *out, QString *err);
    bool                            parseTraceRef(int *trace1based, QString *err);
    bool                            parseOptionalFreq(double *fGHz, QString *err);
    bool                            parseSindices(int *m, int *n, QString *err);
    bool                            parseCall(double *out, QString *err);

    QString                         m_s;
    int                             m_i = 0;
    double                          m_defaultF = 1.0;
    CapFn                           m_cap;
    YdiffFn                         m_ydiff;
    DbPhFn                          m_dbph;
    IndFn                           m_ind;
};

#endif // QT_VERSION

#endif // EXPRPARSER_H
