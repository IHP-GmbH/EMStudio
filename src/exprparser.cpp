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

#include "exprparser.h"

#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \brief Constructs a parser for \a text using \a defaultFGhz when a call omits frequency.
 **********************************************************************************************************************/
ExprParser::ExprParser(QString text, double defaultFGhz, CapFn cap, YdiffFn ydiff, DbPhFn dbph)
    : m_s(std::move(text))
    , m_i(0)
    , m_defaultF(defaultFGhz)
    , m_cap(std::move(cap))
    , m_ydiff(std::move(ydiff))
    , m_dbph(std::move(dbph))
{
    m_s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    m_s.replace(QStringLiteral("−"), QStringLiteral("-")); // unicode minus
}

/*!*******************************************************************************************************************
 * \brief Parses the full expression into \a out.
 * \param[out] out Numeric result.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parse(double *out, QString *err)
{
    skip();
    if (!parseExpr(out, err))
        return false;
    skip();
    if (m_i < m_s.size()) {
        if (err)
            *err = QObject::tr("Unexpected '%1' at position %2")
                       .arg(m_s.mid(m_i, 1))
                       .arg(m_i + 1);
        return false;
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Advances the cursor past whitespace.
 **********************************************************************************************************************/
void ExprParser::skip()
{
    while (m_i < m_s.size() && m_s.at(m_i).isSpace())
        ++m_i;
}

/*!*******************************************************************************************************************
 * \brief Parses an additive expression (\c term (('+'|'-') term)*).
 * \param[out] out Numeric result.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseExpr(double *out, QString *err)
{
    if (!parseTerm(out, err))
        return false;
    for (;;) {
        skip();
        if (m_i >= m_s.size())
            break;
        const QChar op = m_s.at(m_i);
        if (op != QLatin1Char('+') && op != QLatin1Char('-'))
            break;
        ++m_i;
        double rhs = 0;
        if (!parseTerm(&rhs, err))
            return false;
        *out = (op == QLatin1Char('+')) ? (*out + rhs) : (*out - rhs);
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses a multiplicative term (\c factor (('*'|'/') factor)*).
 * \param[out] out Numeric result.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseTerm(double *out, QString *err)
{
    if (!parseFactor(out, err))
        return false;
    for (;;) {
        skip();
        if (m_i >= m_s.size())
            break;
        const QChar op = m_s.at(m_i);
        if (op != QLatin1Char('*') && op != QLatin1Char('/'))
            break;
        ++m_i;
        double rhs = 0;
        if (!parseFactor(&rhs, err))
            return false;
        if (op == QLatin1Char('*'))
            *out *= rhs;
        else {
            if (rhs == 0.0) {
                if (err)
                    *err = QObject::tr("Division by zero");
                return false;
            }
            *out /= rhs;
        }
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses a unary factor, parenthesized subexpression, number, or function call.
 * \param[out] out Numeric result.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseFactor(double *out, QString *err)
{
    skip();
    if (m_i >= m_s.size()) {
        if (err)
            *err = QObject::tr("Unexpected end of expression");
        return false;
    }
    if (m_s.at(m_i) == QLatin1Char('+')) {
        ++m_i;
        return parseFactor(out, err);
    }
    if (m_s.at(m_i) == QLatin1Char('-')) {
        ++m_i;
        if (!parseFactor(out, err))
            return false;
        *out = -*out;
        return true;
    }
    if (m_s.at(m_i) == QLatin1Char('(')) {
        ++m_i;
        if (!parseExpr(out, err))
            return false;
        skip();
        if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char(')')) {
            if (err)
                *err = QObject::tr("Missing ')'");
            return false;
        }
        ++m_i;
        return true;
    }
    if (m_s.at(m_i).isLetter() || m_s.at(m_i) == QLatin1Char('_'))
        return parseCall(out, err);
    return parseNumber(out, err);
}

/*!*******************************************************************************************************************
 * \brief Parses a floating-point literal, optionally followed by a \c G / \c GHz unit marker.
 * \param[out] out Parsed number (GHz when used as a frequency argument).
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseNumber(double *out, QString *err)
{
    skip();
    const int start = m_i;
    while (m_i < m_s.size()
           && (m_s.at(m_i).isDigit() || m_s.at(m_i) == QLatin1Char('.')
               || m_s.at(m_i).toLower() == QLatin1Char('e')
               || ((m_s.at(m_i) == QLatin1Char('+') || m_s.at(m_i) == QLatin1Char('-'))
                   && m_i > start
                   && m_s.at(m_i - 1).toLower() == QLatin1Char('e')))) {
        ++m_i;
    }
    if (m_i == start) {
        if (err)
            *err = QObject::tr("Expected number at position %1").arg(m_i + 1);
        return false;
    }
    bool ok = false;
    *out = m_s.mid(start, m_i - start).toDouble(&ok);
    if (!ok) {
        if (err)
            *err = QObject::tr("Bad number '%1'").arg(m_s.mid(start, m_i - start));
        return false;
    }
    // optional GHz / G suffix (unit marker in arg context; value stays in GHz units)
    skip();
    if (m_i + 2 < m_s.size()
        && m_s.mid(m_i, 3).compare(QLatin1String("GHz"), Qt::CaseInsensitive) == 0) {
        m_i += 3;
    } else if (m_i < m_s.size() && m_s.at(m_i).toUpper() == QLatin1Char('G')
               && (m_i + 1 >= m_s.size() || !m_s.at(m_i + 1).isLetter())) {
        m_i += 1;
    }
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses a selected-curve reference \c $N (1-based).
 * \param[out] trace1based 1-based curve index.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseTraceRef(int *trace1based, QString *err)
{
    skip();
    if (m_i < m_s.size() && m_s.at(m_i) == QLatin1Char('$')) {
        ++m_i;
        const int start = m_i;
        while (m_i < m_s.size() && m_s.at(m_i).isDigit())
            ++m_i;
        if (m_i == start) {
            if (err)
                *err = QObject::tr("Expected $N trace index");
            return false;
        }
        *trace1based = m_s.mid(start, m_i - start).toInt();
        if (*trace1based < 1) {
            if (err)
                *err = QObject::tr("Trace index must be ≥ 1");
            return false;
        }
        return true;
    }
    if (err)
        *err = QObject::tr("Expected $N (selected curve)");
    return false;
}

/*!*******************************************************************************************************************
 * \brief Parses an optional \c ,freqGHz argument; otherwise uses the default frequency.
 * \param[out] fGHz Frequency in GHz.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseOptionalFreq(double *fGHz, QString *err)
{
    skip();
    if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char(',')) {
        *fGHz = m_defaultF;
        return true;
    }
    ++m_i;
    double v = 0;
    if (!parseNumber(&v, err))
        return false;
    *fGHz = v;
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses S-parameter indices (\c S21, \c 21, or \c S2,1).
 * \param[out] m First port index (1-based).
 * \param[out] n Second port index (1-based).
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseSindices(int *m, int *n, QString *err)
{
    skip();
    // S21 or S2,1 or 21
    if (m_i < m_s.size()
        && (m_s.at(m_i) == QLatin1Char('S') || m_s.at(m_i) == QLatin1Char('s'))) {
        ++m_i;
    }
    skip();
    const int start = m_i;
    while (m_i < m_s.size() && m_s.at(m_i).isDigit())
        ++m_i;
    if (m_i == start) {
        if (err)
            *err = QObject::tr("Expected S-parameter like S21");
        return false;
    }
    const QString digits = m_s.mid(start, m_i - start);
    if (digits.size() == 2) {
        *m = digits.at(0).digitValue();
        *n = digits.at(1).digitValue();
        return true;
    }
    // S2,1
    skip();
    if (m_i < m_s.size() && m_s.at(m_i) == QLatin1Char(',')) {
        *m = digits.toInt();
        ++m_i;
        skip();
        const int n0 = m_i;
        while (m_i < m_s.size() && m_s.at(m_i).isDigit())
            ++m_i;
        if (m_i == n0) {
            if (err)
                *err = QObject::tr("Expected second port index");
            return false;
        }
        *n = m_s.mid(n0, m_i - n0).toInt();
        return true;
    }
    if (err)
        *err = QObject::tr("Expected S21 or S2,1");
    return false;
}

/*!*******************************************************************************************************************
 * \brief Parses a function call (\c cser, \c ydiff_cser, \c db, \c ph, …) and evaluates it via callbacks.
 * \param[out] out Numeric result.
 * \param[out] err Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ExprParser::parseCall(double *out, QString *err)
{
    const int start = m_i;
    while (m_i < m_s.size()
           && (m_s.at(m_i).isLetterOrNumber() || m_s.at(m_i) == QLatin1Char('_'))) {
        ++m_i;
    }
    const QString name = m_s.mid(start, m_i - start).toLower();
    skip();
    if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char('(')) {
        if (err)
            *err = QObject::tr("Expected '(' after %1").arg(name);
        return false;
    }
    ++m_i;

    auto expectClose = [&]() -> bool {
        skip();
        if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char(')')) {
            if (err)
                *err = QObject::tr("Missing ')' after %1").arg(name);
            return false;
        }
        ++m_i;
        return true;
    };

    CapKind capKind = CapKind::Cser;
    bool isCap = false;
    bool isYdiff = false;
    if (name == QLatin1String("cser")) {
        isCap = true;
        capKind = CapKind::Cser;
    } else if (name == QLatin1String("csh1")) {
        isCap = true;
        capKind = CapKind::Csh1;
    } else if (name == QLatin1String("csh2")) {
        isCap = true;
        capKind = CapKind::Csh2;
    } else if (name == QLatin1String("ydiff_cser") || name == QLatin1String("ycser")) {
        isYdiff = true;
        capKind = CapKind::Cser;
    } else if (name == QLatin1String("ydiff_csh1")) {
        isYdiff = true;
        capKind = CapKind::Csh1;
    } else if (name == QLatin1String("ydiff_csh2")) {
        isYdiff = true;
        capKind = CapKind::Csh2;
    }

    if (isCap) {
        int t = 0;
        if (!parseTraceRef(&t, err))
            return false;
        double f = m_defaultF;
        if (!parseOptionalFreq(&f, err))
            return false;
        if (!expectClose())
            return false;
        return m_cap(capKind, t, f, out, err);
    }
    if (isYdiff) {
        int a = 0, b = 0;
        if (!parseTraceRef(&a, err))
            return false;
        skip();
        if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char(',')) {
            if (err)
                *err = QObject::tr("%1 needs two traces: %1($1,$2)").arg(name);
            return false;
        }
        ++m_i;
        if (!parseTraceRef(&b, err))
            return false;
        double f = m_defaultF;
        if (!parseOptionalFreq(&f, err))
            return false;
        if (!expectClose())
            return false;
        return m_ydiff(capKind, a, b, f, out, err);
    }
    if (name == QLatin1String("db") || name == QLatin1String("ph")
        || name == QLatin1String("phase")) {
        const bool wantDb = (name == QLatin1String("db"));
        int m = 0, n = 0, t = 0;
        if (!parseSindices(&m, &n, err))
            return false;
        skip();
        if (m_i >= m_s.size() || m_s.at(m_i) != QLatin1Char(',')) {
            if (err)
                *err = QObject::tr("%1(S21,$1) needs a curve").arg(name);
            return false;
        }
        ++m_i;
        if (!parseTraceRef(&t, err))
            return false;
        double f = m_defaultF;
        if (!parseOptionalFreq(&f, err))
            return false;
        if (!expectClose())
            return false;
        return m_dbph(wantDb, m, n, t, f, out, err);
    }

    if (err)
        *err = QObject::tr("Unknown function '%1'").arg(name);
    return false;
}

#endif // QT_VERSION
