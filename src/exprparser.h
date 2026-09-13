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
 * \class ExprParser
 * \brief Recursive-descent parser for Results calculator expressions.
 *
 * Supports arithmetic (\c + − * /), parentheses, and RF helpers such as
 * \c cser($1), \c ydiff_cser($1,$2), \c db(S21,$1), \c ph(S21,$1).
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

    /*!*******************************************************************************************************************
     * \brief Constructs a parser for \a text using \a defaultFGhz when a call omits frequency.
     * \param text Expression string to parse.
     * \param defaultFGhz Default frequency in GHz.
     * \param cap Callback for \c cser/\c csh1/\c csh2.
     * \param ydiff Callback for \c ydiff_cser (and related).
     * \param dbph Callback for \c db/\c ph.
     **********************************************************************************************************************/
    ExprParser(QString text, double defaultFGhz, CapFn cap, YdiffFn ydiff, DbPhFn dbph);

    /*!*******************************************************************************************************************
     * \brief Parses the full expression into \a out.
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parse(double *out, QString *err);

private:
    /*!*******************************************************************************************************************
     * \brief Advances the cursor past whitespace.
     **********************************************************************************************************************/
    void                            skip();

    /*!*******************************************************************************************************************
     * \brief Parses an additive expression (\c term (('+'|'-') term)*).
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseExpr(double *out, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses a multiplicative term (\c factor (('*'|'/') factor)*).
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseTerm(double *out, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses a unary factor, parenthesized subexpression, number, or function call.
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseFactor(double *out, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses a floating-point literal, optionally followed by a \c G / \c GHz unit marker.
     * \param[out] out Parsed number (GHz when used as a frequency argument).
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseNumber(double *out, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses a selected-curve reference \c $N (1-based).
     * \param[out] trace1based 1-based curve index.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseTraceRef(int *trace1based, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses an optional \c ,freqGHz argument; otherwise uses the default frequency.
     * \param[out] fGHz Frequency in GHz.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseOptionalFreq(double *fGHz, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses S-parameter indices (\c S21, \c 21, or \c S2,1).
     * \param[out] m First port index (1-based).
     * \param[out] n Second port index (1-based).
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseSindices(int *m, int *n, QString *err);

    /*!*******************************************************************************************************************
     * \brief Parses a function call (\c cser, \c ydiff_cser, \c db, \c ph, …) and evaluates it via callbacks.
     * \param[out] out Numeric result.
     * \param[out] err Optional error text.
     * \return True on success.
     **********************************************************************************************************************/
    bool                            parseCall(double *out, QString *err);

    QString                         m_s;
    int                             m_i = 0;
    double                          m_defaultF = 1.0;
    CapFn                           m_cap;
    YdiffFn                         m_ydiff;
    DbPhFn                          m_dbph;
};

#endif // QT_VERSION

#endif // EXPRPARSER_H
