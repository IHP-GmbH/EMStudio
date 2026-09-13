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

#ifndef RESULTSCALCULATOR_H
#define RESULTSCALCULATOR_H

#include "exprparser.h"
#include "touchstone.h"

#include <QIcon>
#include <QString>
#include <QVector>
#include <QWidget>

#include <complex>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

/*!*******************************************************************************************************************
 * \class ResultsCalculatorPanel
 * \brief Side panel for interactive RF expressions on Results plot traces.
 *
 * Cadence-style calculator sketch for Touchstone data: the user clicks curves on the
 * plot (bound as \c $1, \c $2, …), picks a function from a combo (snippet is inserted
 * into an expression line), edits the expression, and evaluates via \c ExprParser.
 *
 * Supported building blocks (v1):
 * - Capacitance from Y: \c cser / \c csh1 / \c csh2
 * - Open-style de-embed: \c ydiff_cser($1,$2) ≡ C from (Ya − Yb)
 * - Magnitude / phase: \c db(S21,$1), \c ph(S21,$1)
 * - Arithmetic: \c + − * / and parentheses
 *
 * The default frequency spinbox is used when a call omits an explicit GHz argument.
 **********************************************************************************************************************/
class ResultsCalculatorPanel : public QWidget
{
    Q_OBJECT

public:
    /*!*******************************************************************************************************************
     * \brief One Touchstone trace available to the expression evaluator.
     **********************************************************************************************************************/
    struct TraceRef
    {
        QString                     label;
        QString                     path;
        const TouchstoneNetwork    *network = nullptr;
        bool                        selected = false;
    };

    explicit                        ResultsCalculatorPanel(QWidget *parent = nullptr);

    void                            setTraces(const QVector<TraceRef> &traces, bool selectionActive);
    double                          frequencyGHz() const;

public slots:
    void                            evaluate();
    void                            insertFunctionSnippet(int index);

private:
    QVector<TraceRef>               selectedTraces() const;
    void                            rebuildFunctionCombo();

    static int                      nearestFreqIndex(const TouchstoneNetwork &nw, double fHz);
    static bool                     sToY2(const TouchstoneNetwork &nw, int fi,
                                          std::complex<double> &y11, std::complex<double> &y12,
                                          std::complex<double> &y21, std::complex<double> &y22,
                                          QString *error);
    static double                   cFromY(CapKind kind,
                                          std::complex<double> y11,
                                          std::complex<double> y12,
                                          std::complex<double> y22,
                                          double fHz);

    bool                            evalExpression(const QString &expr, double *out, QString *error) const;
    bool                            evalCap(CapKind kind, const TraceRef &trace, double fGHz,
                                            double *out, QString *error) const;
    bool                            evalYdiffCap(CapKind kind, const TraceRef &a, const TraceRef &b,
                                                 double fGHz, double *out, QString *error) const;
    bool                            evalDbPh(bool wantDb, int m, int n, const TraceRef &trace,
                                             double fGHz, double *out, QString *error) const;

    QComboBox                      *m_funcCombo = nullptr;
    QLineEdit                      *m_exprEdit = nullptr;
    QDoubleSpinBox                 *m_freqGHz = nullptr;
    QPushButton                    *m_evalBtn = nullptr;
    QPlainTextEdit                 *m_output = nullptr;
    QLabel                         *m_selectionLabel = nullptr;
    QVector<TraceRef>               m_traces;
    bool                            m_selectionActive = false;
};

/*!*******************************************************************************************************************
 * \brief Builds a small calculator-glyph icon for the Results toggle button.
 * \param size Edge length in pixels.
 **********************************************************************************************************************/
QIcon                               resultsCalculatorIcon(int size = 20);

#endif // QT_VERSION

#endif // RESULTSCALCULATOR_H
