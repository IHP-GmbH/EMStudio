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

#include "resultscalculator.h"
#include "exprparser.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <QtMath>
#include <cmath>
#include <limits>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

namespace {

QString formatVal(double v, const QString &unit = QString())
{
    if (!std::isfinite(v))
        return QStringLiteral("n/a");
    QString s;
    if (std::abs(v) >= 100.0)
        s = QString::number(v, 'f', 3);
    else if (std::abs(v) >= 1.0)
        s = QString::number(v, 'f', 4);
    else
        s = QString::number(v, 'g', 6);
    if (!unit.isEmpty())
        s += QLatin1Char(' ') + unit;
    return s;
}

double toDb(const std::complex<double> &v)
{
    const double mag = std::abs(v);
    if (mag <= 0.0)
        return -200.0;
    return 20.0 * std::log10(mag);
}

double toPhaseDeg(const std::complex<double> &v)
{
    return qRadiansToDegrees(std::arg(v));
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Builds a small calculator-glyph icon for the Results toggle button.
 * \param size Edge length in pixels.
 **********************************************************************************************************************/
QIcon resultsCalculatorIcon(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Flat body
    p.setPen(QPen(QColor(60, 60, 60), 1));
    p.setBrush(QColor(240, 240, 240));
    const int m = 2;
    p.drawRect(m, m, size - 2 * m - 1, size - 2 * m - 1);

    // Flat display strip
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(180, 200, 220));
    const int pad = 4;
    const int dispH = qMax(3, (size - 2 * m) / 4);
    p.drawRect(m + pad, m + pad, size - 2 * m - 2 * pad - 1, dispH);

    // Flat 2×2 keys
    p.setBrush(QColor(120, 120, 120));
    const int keyTop = m + pad + dispH + 2;
    const int keyBottom = size - m - pad;
    const int keyArea = keyBottom - keyTop;
    const int gap = 2;
    const int key = (keyArea - gap) / 2;
    const int keyLeft = m + pad;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            p.drawRect(keyLeft + c * (key + gap),
                       keyTop + r * (key + gap),
                       key, key);
        }
    }
    p.end();
    return QIcon(pm);
}

/*!*******************************************************************************************************************
 * \brief Constructs the calculator side panel (expression line, function combo, output).
 * \param parent Optional parent widget.
 **********************************************************************************************************************/
ResultsCalculatorPanel::ResultsCalculatorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(6);

    auto *title = new QLabel(tr("Calculator"), this);
    title->setStyleSheet(QStringLiteral("font-weight: bold;"));
    lay->addWidget(title);

    m_selectionLabel = new QLabel(tr("Selected: (click a curve)"), this);
    m_selectionLabel->setWordWrap(true);
    lay->addWidget(m_selectionLabel);

    auto *freqRow = new QHBoxLayout();
    freqRow->addWidget(new QLabel(tr("Default f:"), this));
    m_freqGHz = new QDoubleSpinBox(this);
    m_freqGHz->setRange(0.001, 1e6);
    m_freqGHz->setDecimals(3);
    m_freqGHz->setSuffix(tr(" GHz"));
    m_freqGHz->setValue(1.0);
    m_freqGHz->setSingleStep(1.0);
    freqRow->addWidget(m_freqGHz, 1);
    lay->addLayout(freqRow);

    auto *insRow = new QHBoxLayout();
    m_funcCombo = new QComboBox(this);
    m_funcCombo->setEditable(false);
    connect(m_funcCombo, QOverload<int>::of(&QComboBox::activated),
            this, &ResultsCalculatorPanel::insertFunctionSnippet);
    insRow->addWidget(m_funcCombo, 1);
    lay->addLayout(insRow);

    auto *exprRow = new QHBoxLayout();
    exprRow->setSpacing(4);
    m_exprEdit = new QLineEdit(this);
    m_exprEdit->setPlaceholderText(tr("expression…"));
    m_exprEdit->setClearButtonEnabled(true);
    connect(m_exprEdit, &QLineEdit::returnPressed, this, &ResultsCalculatorPanel::evaluate);
    exprRow->addWidget(m_exprEdit, 1);

    auto *infoBtn = new QToolButton(this);
    infoBtn->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    infoBtn->setIconSize(QSize(16, 16));
    infoBtn->setAutoRaise(true);
    infoBtn->setFocusPolicy(Qt::NoFocus);
    infoBtn->setCursor(Qt::WhatsThisCursor);
    infoBtn->setToolTip(
        QStringLiteral(
            "<qt><pre style='font-family: Consolas, \"Courier New\", monospace; margin:0;'>"
            "Click curves on the plot → $1, $2, …\n"
            "Pick a function from the combo, edit, Evaluate.\n"
            "Default f is used when frequency is omitted.\n"
            "\n"
            "Formulas (2-port, S → Y):\n"
            "  cser($N)             −Im(Y12)/ω              [fF]\n"
            "  csh1($N)             (Im(Y11)+Im(Y12))/ω     [fF]\n"
            "  csh2($N)             (Im(Y22)+Im(Y12))/ω     [fF]\n"
            "  ydiff_cser($1,$2)    Cser of (Y1−Y2)         [fF]\n"
            "  db(S21,$N)           20·log10|S|             [dB]\n"
            "  ph(S21,$N)           arg(S)                  [°]\n"
            "\n"
            "Optional freq:  cser($1, 1)   // GHz\n"
            "Arithmetic:     + − * / and parentheses\n"
            "</pre></qt>"));
    exprRow->addWidget(infoBtn, 0, Qt::AlignVCenter);
    lay->addLayout(exprRow);

    m_evalBtn = new QPushButton(tr("Evaluate"), this);
    m_evalBtn->setDefault(true);
    connect(m_evalBtn, &QPushButton::clicked, this, &ResultsCalculatorPanel::evaluate);
    lay->addWidget(m_evalBtn);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setPlaceholderText(tr("Result…"));
    lay->addWidget(m_output, 1);

    setMinimumWidth(220);
    rebuildFunctionCombo();
}

/*!*******************************************************************************************************************
 * \brief Rebuilds the function combo, hiding snippets that need more selected curves than available.
 **********************************************************************************************************************/
void ResultsCalculatorPanel::rebuildFunctionCombo()
{
    if (!m_funcCombo)
        return;

    const int n = selectedTraces().size();
    m_funcCombo->blockSignals(true);
    m_funcCombo->clear();
    m_funcCombo->addItem(tr("Insert function…"), QString());

    const auto add = [&](const QString &label, const QString &snip, int minTraces) {
        if (n >= minTraces)
            m_funcCombo->addItem(label, snip);
    };

    add(QStringLiteral("cser($1)"), QStringLiteral("cser($1)"), 1);
    add(QStringLiteral("csh1($1)"), QStringLiteral("csh1($1)"), 1);
    add(QStringLiteral("csh2($1)"), QStringLiteral("csh2($1)"), 1);
    add(QStringLiteral("db(S21,$1)"), QStringLiteral("db(S21,$1)"), 1);
    add(QStringLiteral("ph(S21,$1)"), QStringLiteral("ph(S21,$1)"), 1);
    add(QStringLiteral("cser($1)-cser($2)"), QStringLiteral("cser($1)-cser($2)"), 2);
    add(QStringLiteral("ydiff_cser($1,$2)"), QStringLiteral("ydiff_cser($1,$2)"), 2);

    m_funcCombo->setCurrentIndex(0);
    m_funcCombo->setEnabled(n >= 1);
    m_funcCombo->blockSignals(false);

    if (m_exprEdit) {
        const QString cur = m_exprEdit->text().trimmed();
        if (n >= 2 && (cur.isEmpty() || cur == QLatin1String("cser($1)")))
            m_exprEdit->setText(QStringLiteral("ydiff_cser($1,$2)"));
        else if (n == 1 && (cur.isEmpty() || cur.contains(QLatin1String("$2"))))
            m_exprEdit->setText(QStringLiteral("cser($1)"));
    }
}

/*!*******************************************************************************************************************
 * \brief Inserts the selected combo snippet into the expression line at the cursor.
 * \param index Combo row (0 is the placeholder “Insert function…”).
 **********************************************************************************************************************/
void ResultsCalculatorPanel::insertFunctionSnippet(int index)
{
    if (!m_funcCombo || !m_exprEdit || index <= 0)
        return;
    const QString snip = m_funcCombo->itemData(index).toString();
    if (snip.isEmpty())
        return;
    m_exprEdit->insert(snip);
    m_exprEdit->setFocus();
    m_funcCombo->setCurrentIndex(0);
}

/*!*******************************************************************************************************************
 * \brief Updates the list of plot traces available as \c $1, \c $2, …
 * \param traces Plotted networks (selected ones participate in evaluation).
 * \param selectionActive True when the user has clicked at least one curve.
 **********************************************************************************************************************/
void ResultsCalculatorPanel::setTraces(const QVector<TraceRef> &traces, bool selectionActive)
{
    m_traces = traces;
    m_selectionActive = selectionActive;
    if (m_selectionLabel) {
        const QVector<TraceRef> sel = selectedTraces();
        if (sel.isEmpty()) {
            m_selectionLabel->setText(tr("Selected: (click a curve on the plot)"));
        } else {
            QStringList lines;
            for (int i = 0; i < sel.size(); ++i)
                lines << QStringLiteral("$%1 = %2").arg(i + 1).arg(sel.at(i).label);
            m_selectionLabel->setText(tr("Selected:\n%1").arg(lines.join(QLatin1Char('\n'))));
        }
    }
    rebuildFunctionCombo();
}

/*!*******************************************************************************************************************
 * \brief Default evaluation frequency from the spinbox (GHz), used when a call omits \c f.
 **********************************************************************************************************************/
double ResultsCalculatorPanel::frequencyGHz() const
{
    return m_freqGHz ? m_freqGHz->value() : 1.0;
}

/*!*******************************************************************************************************************
 * \brief Returns currently click-selected traces in \c $1… order.
 **********************************************************************************************************************/
QVector<ResultsCalculatorPanel::TraceRef> ResultsCalculatorPanel::selectedTraces() const
{
    QVector<TraceRef> out;
    if (!m_selectionActive)
        return out;
    for (const TraceRef &t : m_traces) {
        if (t.selected)
            out.append(t);
    }
    return out;
}

/*!*******************************************************************************************************************
 * \brief Index of the frequency point nearest to \a fHz in \a nw.
 **********************************************************************************************************************/
int ResultsCalculatorPanel::nearestFreqIndex(const TouchstoneNetwork &nw, double fHz)
{
    const auto freqs = nw.frequencyHz();
    if (freqs.isEmpty())
        return -1;
    int best = 0;
    double bestAbs = std::abs(freqs.at(0) - fHz);
    for (int i = 1; i < freqs.size(); ++i) {
        const double d = std::abs(freqs.at(i) - fHz);
        if (d < bestAbs) {
            bestAbs = d;
            best = i;
        }
    }
    return best;
}

/*!*******************************************************************************************************************
 * \brief Converts a 2-port S-matrix sample to Y (real Z0).
 * \param nw Touchstone network.
 * \param fi Frequency index.
 * \param[out] y11,y12,y21,y22 Y-parameters.
 * \param[out] error Optional error text.
 * \return True on success.
 **********************************************************************************************************************/
bool ResultsCalculatorPanel::sToY2(const TouchstoneNetwork &nw, int fi,
                                   std::complex<double> &y11, std::complex<double> &y12,
                                   std::complex<double> &y21, std::complex<double> &y22,
                                   QString *error)
{
    if (nw.nports() != 2) {
        if (error)
            *error = QObject::tr("Need 2-port Touchstone (got %1).").arg(nw.nports());
        return false;
    }
    if (fi < 0 || fi >= nw.frequencyCount()) {
        if (error)
            *error = QObject::tr("Invalid frequency index.");
        return false;
    }

    const std::complex<double> s11 = nw.s(fi, 0, 0);
    const std::complex<double> s12 = nw.s(fi, 0, 1);
    const std::complex<double> s21 = nw.s(fi, 1, 0);
    const std::complex<double> s22 = nw.s(fi, 1, 1);
    const double z0 = nw.referenceImpedance();
    if (!(z0 > 0.0)) {
        if (error)
            *error = QObject::tr("Invalid Z0.");
        return false;
    }

    const std::complex<double> one(1.0, 0.0);
    const std::complex<double> den = (one + s11) * (one + s22) - s12 * s21;
    if (std::abs(den) < 1e-18) {
        if (error)
            *error = QObject::tr("Singular (I+S).");
        return false;
    }

    y11 = ((one - s11) * (one + s22) + s12 * s21) / (z0 * den);
    y12 = (-2.0 * s12) / (z0 * den);
    y21 = (-2.0 * s21) / (z0 * den);
    y22 = ((one + s11) * (one - s22) + s12 * s21) / (z0 * den);
    return true;
}

/*!*******************************************************************************************************************
 * \brief π-model capacitance (fF) from a Y sample at frequency \a fHz.
 **********************************************************************************************************************/
double ResultsCalculatorPanel::cFromY(CapKind q, std::complex<double> y11,
                                      std::complex<double> y12, std::complex<double> y22,
                                      double fHz)
{
    const double w = 2.0 * std::acos(-1.0) * fHz;
    if (!(w > 0.0))
        return std::numeric_limits<double>::quiet_NaN();
    switch (q) {
    case CapKind::Cser:
        return -y12.imag() / w * 1e15;
    case CapKind::Csh1:
        return (y11.imag() + y12.imag()) / w * 1e15;
    case CapKind::Csh2:
        return (y22.imag() + y12.imag()) / w * 1e15;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/*!*******************************************************************************************************************
 * \brief Evaluates \c cser/\c csh1/\c csh2 for one selected trace at \a fGHz.
 **********************************************************************************************************************/
bool ResultsCalculatorPanel::evalCap(CapKind kind, const TraceRef &trace, double fGHz,
                                     double *out, QString *error) const
{
    if (!trace.network || !trace.network->isValid()) {
        if (error)
            *error = tr("Bad network for %1").arg(trace.label);
        return false;
    }
    const double fHz = fGHz * 1e9;
    const int fi = nearestFreqIndex(*trace.network, fHz);
    if (fi < 0) {
        if (error)
            *error = tr("No frequency points in %1").arg(trace.label);
        return false;
    }
    std::complex<double> y11, y12, y21, y22;
    if (!sToY2(*trace.network, fi, y11, y12, y21, y22, error))
        return false;
    *out = cFromY(kind, y11, y12, y22, trace.network->frequencyHz().at(fi));
    return true;
}

/*!*******************************************************************************************************************
 * \brief Capacitance from open-style de-embed: C(Ya − Yb) at \a fGHz.
 **********************************************************************************************************************/
bool ResultsCalculatorPanel::evalYdiffCap(CapKind kind, const TraceRef &a, const TraceRef &b,
                                          double fGHz, double *out, QString *error) const
{
    if (!a.network || !b.network) {
        if (error)
            *error = tr("Need two valid traces for Y-diff");
        return false;
    }
    const double fHz = fGHz * 1e9;
    const int fiA = nearestFreqIndex(*a.network, fHz);
    const int fiB = nearestFreqIndex(*b.network, fHz);
    if (fiA < 0 || fiB < 0) {
        if (error)
            *error = tr("Missing frequency points for Y-diff");
        return false;
    }
    std::complex<double> a11, a12, a21, a22, b11, b12, b21, b22;
    if (!sToY2(*a.network, fiA, a11, a12, a21, a22, error))
        return false;
    if (!sToY2(*b.network, fiB, b11, b12, b21, b22, error))
        return false;
    const double fUsed = a.network->frequencyHz().at(fiA);
    *out = cFromY(kind, a11 - b11, a12 - b12, a22 - b22, fUsed);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Magnitude (dB) or phase (°) of S(m,n) for one trace at \a fGHz.
 **********************************************************************************************************************/
bool ResultsCalculatorPanel::evalDbPh(bool wantDb, int m, int n, const TraceRef &trace,
                                      double fGHz, double *out, QString *error) const
{
    if (!trace.network || !trace.network->isValid()) {
        if (error)
            *error = tr("Bad network for %1").arg(trace.label);
        return false;
    }
    if (m < 1 || n < 1 || m > trace.network->nports() || n > trace.network->nports()) {
        if (error)
            *error = tr("S%1%2 out of range for %3-port")
                         .arg(m)
                         .arg(n)
                         .arg(trace.network->nports());
        return false;
    }
    const int fi = nearestFreqIndex(*trace.network, fGHz * 1e9);
    if (fi < 0) {
        if (error)
            *error = tr("No frequency points");
        return false;
    }
    const auto s = trace.network->s(fi, m - 1, n - 1);
    *out = wantDb ? toDb(s) : toPhaseDeg(s);
    return true;
}

/*!*******************************************************************************************************************
 * \brief Parses and evaluates a calculator expression against the selected curves.
 * \param expr Expression text (e.g. \c ydiff_cser($1,$2)).
 * \param[out] out Numeric result.
 * \param[out] error Optional parse/eval error.
 * \return True on success.
 **********************************************************************************************************************/
bool ResultsCalculatorPanel::evalExpression(const QString &expr, double *out, QString *error) const
{
    const QVector<TraceRef> sel = selectedTraces();
    auto resolve = [&](int oneBased, TraceRef *refOut, QString *err) -> bool {
        if (oneBased < 1 || oneBased > sel.size()) {
            if (err)
                *err = QObject::tr("$%1 not selected (have %2 curve(s))")
                           .arg(oneBased)
                           .arg(sel.size());
            return false;
        }
        *refOut = sel.at(oneBased - 1);
        return true;
    };

    ExprParser parser(
        expr,
        frequencyGHz(),
        [&](CapKind kind, int t, double f, double *v, QString *err) {
            TraceRef ref;
            if (!resolve(t, &ref, err))
                return false;
            return evalCap(kind, ref, f, v, err);
        },
        [&](CapKind kind, int a, int b, double f, double *v, QString *err) {
            TraceRef ta, tb;
            if (!resolve(a, &ta, err) || !resolve(b, &tb, err))
                return false;
            return evalYdiffCap(kind, ta, tb, f, v, err);
        },
        [&](bool wantDb, int m, int n, int t, double f, double *v, QString *err) {
            TraceRef ref;
            if (!resolve(t, &ref, err))
                return false;
            return evalDbPh(wantDb, m, n, ref, f, v, err);
        });

    return parser.parse(out, error);
}

/*!*******************************************************************************************************************
 * \brief Evaluates the expression line and writes a human-readable result to the output pane.
 **********************************************************************************************************************/
void ResultsCalculatorPanel::evaluate()
{
    if (!m_output || !m_exprEdit)
        return;

    if (selectedTraces().isEmpty()) {
        m_output->setPlainText(tr("Click one or more curves on the plot ($1, $2, …),\n"
                                  "then Evaluate the expression."));
        return;
    }

    const QString expr = m_exprEdit->text().trimmed();
    if (expr.isEmpty()) {
        m_output->setPlainText(tr("Empty expression. Pick a function from the combo or type one."));
        return;
    }

    double value = 0;
    QString err;
    if (!evalExpression(expr, &value, &err)) {
        m_output->setPlainText(tr("Error:\n%1\n\nExpression:\n%2").arg(err, expr));
        return;
    }

    QStringList lines;
    lines << tr("expr: %1").arg(expr);
    lines << tr("default f: %1 GHz").arg(frequencyGHz(), 0, 'f', 3);
    lines << QString();
    const QVector<TraceRef> sel = selectedTraces();
    for (int i = 0; i < sel.size(); ++i)
        lines << QStringLiteral("$%1 = %2").arg(i + 1).arg(sel.at(i).label);
    lines << QString();

    // Unit hint from leading function name
    QString unit;
    const QString head = expr.left(12).toLower();
    if (head.contains(QLatin1String("cser")) || head.contains(QLatin1String("csh"))
        || head.contains(QLatin1String("ydiff")))
        unit = QStringLiteral("fF");
    else if (head.startsWith(QLatin1String("db")))
        unit = QStringLiteral("dB");
    else if (head.startsWith(QLatin1String("ph")))
        unit = QStringLiteral("°");

    lines << tr("→  %1").arg(formatVal(value, unit));
    m_output->setPlainText(lines.join(QLatin1Char('\n')));
}

#endif // QT_VERSION
