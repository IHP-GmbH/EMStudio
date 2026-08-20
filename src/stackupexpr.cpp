/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#include "stackupexpr.h"

#include <QRegularExpression>
#include <cmath>

namespace {

struct Parser {
    QString s;
    int i = 0;
    QHash<QString, QVariant> vars;
    QString err;

    void skip() {
        while (i < s.size() && s.at(i).isSpace())
            ++i;
    }

    bool match(QChar c) {
        skip();
        if (i < s.size() && s.at(i) == c) {
            ++i;
            return true;
        }
        return false;
    }

    QVariant parseExpr() { return parseAdd(); }

    QVariant parseAdd() {
        QVariant v = parseMul();
        for (;;) {
            if (match('+')) {
                const QVariant r = parseMul();
                v = asNum(v) + asNum(r);
            } else if (match('-')) {
                const QVariant r = parseMul();
                v = asNum(v) - asNum(r);
            } else {
                break;
            }
        }
        return v;
    }

    QVariant parseMul() {
        QVariant v = parseUnary();
        for (;;) {
            if (match('*')) {
                const QVariant r = parseUnary();
                v = asNum(v) * asNum(r);
            } else if (match('/')) {
                const QVariant r = parseUnary();
                const double den = asNum(r);
                if (std::abs(den) < 1e-30) {
                    err = QStringLiteral("Division by zero");
                    return 0.0;
                }
                v = asNum(v) / den;
            } else {
                break;
            }
        }
        return v;
    }

    QVariant parseUnary() {
        if (match('+'))
            return parseUnary();
        if (match('-'))
            return -asNum(parseUnary());
        return parsePrimary();
    }

    QVariant parsePrimary() {
        skip();
        if (match('(')) {
            QVariant v = parseExpr();
            if (!match(')'))
                err = QStringLiteral("Missing ')'");
            return v;
        }

        if (i < s.size() && (s.at(i).isDigit() || s.at(i) == '.')) {
            const int start = i;
            while (i < s.size() && (s.at(i).isDigit() || s.at(i) == '.' || s.at(i) == 'e'
                                    || s.at(i) == 'E' || s.at(i) == '+' || s.at(i) == '-')) {
                if ((s.at(i) == '+' || s.at(i) == '-') && i > start
                    && s.at(i - 1) != 'e' && s.at(i - 1) != 'E')
                    break;
                ++i;
            }
            bool ok = false;
            const double d = s.mid(start, i - start).toDouble(&ok);
            if (!ok) {
                err = QStringLiteral("Invalid number");
                return 0.0;
            }
            return d;
        }

        if (i < s.size() && (s.at(i).isLetter() || s.at(i) == '_')) {
            const int start = i;
            while (i < s.size() && (s.at(i).isLetterOrNumber() || s.at(i) == '_'))
                ++i;
            const QString name = s.mid(start, i - start);
            if (!vars.contains(name)) {
                err = QStringLiteral("Unknown variable '%1'").arg(name);
                return 0.0;
            }
            return vars.value(name);
        }

        err = QStringLiteral("Unexpected token near '%1'").arg(s.mid(i, 8));
        return 0.0;
    }

    static double asNum(const QVariant &v) {
        if (v.type() == QVariant::Double || v.type() == QVariant::Int)
            return v.toDouble();
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? d : 0.0;
    }
};

} // namespace

bool StackupExpr::isExpression(const QString &raw)
{
    return raw.trimmed().startsWith(QLatin1Char('='));
}

QString StackupExpr::stripEquals(const QString &raw)
{
    QString t = raw.trimmed();
    if (t.startsWith(QLatin1Char('=')))
        t = t.mid(1).trimmed();
    return t;
}

QVariant StackupExpr::eval(const QString &raw,
                           const QHash<QString, QVariant> &vars,
                           QString *error)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty expression");
        return QVariant();
    }

    // Plain non-expression literal
    if (!isExpression(trimmed)) {
        bool ok = false;
        const double d = trimmed.toDouble(&ok);
        if (ok)
            return d;
        return trimmed; // string literal
    }

    Parser p;
    p.s = stripEquals(trimmed);
    p.vars = vars;
    const QVariant v = p.parseExpr();
    p.skip();
    if (p.i < p.s.size() && p.err.isEmpty())
        p.err = QStringLiteral("Trailing characters");
    if (error)
        *error = p.err;
    if (!p.err.isEmpty())
        return QVariant();
    return v;
}

bool StackupExpr::evalNumber(const QString &raw,
                             const QHash<QString, QVariant> &vars,
                             double *out,
                             QString *error)
{
    const QVariant v = eval(raw, vars, error);
    if (!v.isValid())
        return false;
    if (v.type() == QVariant::String) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Expected number");
            return false;
        }
        if (out)
            *out = d;
        return true;
    }
    if (out)
        *out = v.toDouble();
    return true;
}
