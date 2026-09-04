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

#include "touchstone.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QtMath>

#include <cmath>

namespace {

double freqUnitToHz(const QString &unit)
{
    const QString u = unit.toUpper();
    if (u == QLatin1String("HZ"))
        return 1.0;
    if (u == QLatin1String("KHZ"))
        return 1e3;
    if (u == QLatin1String("MHZ"))
        return 1e6;
    if (u == QLatin1String("GHZ"))
        return 1e9;
    if (u == QLatin1String("THZ"))
        return 1e12;
    return 1e9; // Touchstone default is GHz when unit omitted/unknown
}

int nportsFromFileName(const QString &fileName)
{
    static const QRegularExpression re(
        QStringLiteral(R"(\.s(\d+)p$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(fileName);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt();
}

QStringList tokenizeLine(const QString &line)
{
    // Strip inline ! comments after option/data tokens
    QString cleaned = line;
    const int bang = cleaned.indexOf(QLatin1Char('!'));
    if (bang >= 0)
        cleaned = cleaned.left(bang);
    return cleaned.split(QRegularExpression(QStringLiteral(R"(\s+)")),
                         Qt::SkipEmptyParts);
}

} // namespace

std::complex<double> TouchstoneNetwork::toComplex(Format format, double a, double b)
{
    switch (format) {
    case Format::RI:
        return {a, b};
    case Format::MA: {
        const double rad = qDegreesToRadians(b);
        return {a * std::cos(rad), a * std::sin(rad)};
    }
    case Format::DB: {
        const double mag = std::pow(10.0, a / 20.0);
        const double rad = qDegreesToRadians(b);
        return {mag * std::cos(rad), mag * std::sin(rad)};
    }
    }
    return {};
}

int TouchstoneNetwork::indexOf(int freqIndex, int m, int n) const
{
    return freqIndex * m_nports * m_nports + m * m_nports + n;
}

QString TouchstoneNetwork::fileName() const
{
    return QFileInfo(m_path).fileName();
}

std::complex<double> TouchstoneNetwork::s(int freqIndex, int m, int n) const
{
    if (freqIndex < 0 || freqIndex >= m_freqHz.size()
        || m < 0 || n < 0 || m >= m_nports || n >= m_nports) {
        return {};
    }
    return m_s.at(indexOf(freqIndex, m, n));
}

QVector<std::complex<double>> TouchstoneNetwork::sParam(int m, int n) const
{
    QVector<std::complex<double>> out;
    if (m < 0 || n < 0 || m >= m_nports || n >= m_nports)
        return out;
    out.reserve(m_freqHz.size());
    for (int i = 0; i < m_freqHz.size(); ++i)
        out.append(m_s.at(indexOf(i, m, n)));
    return out;
}

bool TouchstoneNetwork::load(const QString &path, QString *error)
{
    m_path.clear();
    m_nports = 0;
    m_z0 = 50.0;
    m_freqHz.clear();
    m_s.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot open file: %1").arg(path);
        return false;
    }

    const int nportsHint = nportsFromFileName(QFileInfo(path).fileName());

    Format format = Format::MA;
    double freqScale = 1e9;
    bool sawOption = false;

    QVector<double> numbers;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('!')))
            continue;

        if (line.startsWith(QLatin1Char('#'))) {
            const QStringList toks = tokenizeLine(line.mid(1));
            sawOption = true;
            format = Format::MA;
            freqScale = 1e9;
            m_z0 = 50.0;
            for (int i = 0; i < toks.size(); ++i) {
                const QString t = toks.at(i).toUpper();
                if (t == QLatin1String("HZ") || t == QLatin1String("KHZ")
                    || t == QLatin1String("MHZ") || t == QLatin1String("GHZ")
                    || t == QLatin1String("THZ")) {
                    freqScale = freqUnitToHz(t);
                } else if (t == QLatin1String("RI")) {
                    format = Format::RI;
                } else if (t == QLatin1String("MA")) {
                    format = Format::MA;
                } else if (t == QLatin1String("DB")) {
                    format = Format::DB;
                } else if (t == QLatin1String("R") && i + 1 < toks.size()) {
                    bool ok = false;
                    const double z = toks.at(i + 1).toDouble(&ok);
                    if (ok)
                        m_z0 = z;
                    ++i;
                }
                // ignore parameter type (S/Y/Z/H/G) — we only plot S
            }
            continue;
        }

        const QStringList toks = tokenizeLine(line);
        for (const QString &tok : toks) {
            bool ok = false;
            const double v = tok.toDouble(&ok);
            if (!ok) {
                if (error)
                    *error = QStringLiteral("Invalid numeric token '%1' in %2")
                                 .arg(tok, path);
                return false;
            }
            numbers.append(v);
        }
    }

    if (!sawOption) {
        // Still accept files that omit '#'; OpenEMS sometimes still writes data-only
        format = Format::MA;
        freqScale = 1e9;
    }

    int nports = nportsHint;
    if (nports <= 0) {
        // Infer from first frequency block size when filename is nonstandard
        // Need at least 1 + 2 values; try common port counts
        for (int candidate : {1, 2, 3, 4, 5, 6, 8, 10, 12, 16}) {
            const int need = 1 + 2 * candidate * candidate;
            if (numbers.size() >= need && (numbers.size() % need) == 0) {
                nports = candidate;
                break;
            }
        }
    }

    if (nports <= 0) {
        if (error)
            *error = QStringLiteral("Cannot determine port count for %1").arg(path);
        return false;
    }

    const int valuesPerFreq = 1 + 2 * nports * nports;
    if (numbers.isEmpty() || (numbers.size() % valuesPerFreq) != 0) {
        if (error)
            *error = QStringLiteral("Incomplete Touchstone data in %1 "
                                    "(expected multiples of %2 values, got %3)")
                         .arg(path)
                         .arg(valuesPerFreq)
                         .arg(numbers.size());
        return false;
    }

    const int nfreq = numbers.size() / valuesPerFreq;
    m_nports = nports;
    m_freqHz.reserve(nfreq);
    // Pre-size S matrix; Touchstone 2-port uses column order S11,S21,S12,S22.
    m_s.resize(nfreq * nports * nports);

    int cursor = 0;
    for (int f = 0; f < nfreq; ++f) {
        m_freqHz.append(numbers.at(cursor++) * freqScale);
        if (nports == 2) {
            static const int orderM[4] = {0, 1, 0, 1};
            static const int orderN[4] = {0, 0, 1, 1};
            for (int k = 0; k < 4; ++k) {
                const double a = numbers.at(cursor++);
                const double b = numbers.at(cursor++);
                m_s[indexOf(f, orderM[k], orderN[k])] = toComplex(format, a, b);
            }
        } else {
            for (int m = 0; m < nports; ++m) {
                for (int n = 0; n < nports; ++n) {
                    const double a = numbers.at(cursor++);
                    const double b = numbers.at(cursor++);
                    m_s[indexOf(f, m, n)] = toComplex(format, a, b);
                }
            }
        }
    }

    m_path = path;
    return true;
}
