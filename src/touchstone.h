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

#ifndef TOUCHSTONE_H
#define TOUCHSTONE_H

#include <QString>
#include <QVector>

#include <complex>

/*!*******************************************************************************************************************
 * \class TouchstoneNetwork
 * \brief Lightweight parser for Touchstone (.sNp) S-parameter files.
 *
 * Supports RI, MA and DB formats with frequency units HZ/KHZ/MHZ/GHZ/THZ.
 * Port indices in \c s() / \c sParam() are 0-based.
 **********************************************************************************************************************/
class TouchstoneNetwork
{
public:
    TouchstoneNetwork() = default;

    bool load(const QString &path, QString *error = nullptr);

    int nports() const { return m_nports; }
    QVector<double> frequencyHz() const { return m_freqHz; }
    std::complex<double> s(int freqIndex, int m, int n) const;
    QVector<std::complex<double>> sParam(int m, int n) const;

    QString path() const { return m_path; }
    QString fileName() const;
    double referenceImpedance() const { return m_z0; }
    bool isValid() const { return m_nports > 0 && !m_freqHz.isEmpty(); }
    int frequencyCount() const { return m_freqHz.size(); }

private:
    enum class Format { RI, MA, DB };

    static std::complex<double> toComplex(Format format, double a, double b);
    int indexOf(int freqIndex, int m, int n) const;

    QString m_path;
    int m_nports = 0;
    double m_z0 = 50.0;
    QVector<double> m_freqHz;
    QVector<std::complex<double>> m_s; // nfreq * nports * nports, row-major per freq
};

#endif // TOUCHSTONE_H
