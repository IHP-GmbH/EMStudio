#ifndef SCIDOUBLESPINBOX_H
#define SCIDOUBLESPINBOX_H

#include <QDoubleSpinBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLocale>
#include <limits>
#include <QLineEdit>

#include <cmath>

static inline QString trimZeros(QString s)
{
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s;
}

static inline QString formatCompact(double v, int maxFracDigits, int mantissaSigDigits = 2)
{
    if (v == 0.0)
        return QStringLiteral("0");

    const double av = std::abs(v);

    const bool useSci = (av >= 1e6) || (av < 1e-4);

    if (useSci) {
        const int exp10 = int(::floor(::log10(av)));
        const double mant = v / ::pow(10.0, exp10);

        QString m = QLocale::c().toString(mant, 'g', mantissaSigDigits);
        m = trimZeros(m);

        return m + QStringLiteral("e%1%2")
                       .arg(exp10 >= 0 ? QLatin1Char('+') : QLatin1Char('-'))
                       .arg(std::abs(exp10));
    }

    QString s = QLocale::c().toString(v, 'f', maxFracDigits);
    return trimZeros(s);
}

class SciDoubleSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit SciDoubleSpinBox(QWidget *parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setKeyboardTracking(false);
        setDecimals(12);
        setRange(std::numeric_limits<double>::lowest(),
                 std::numeric_limits<double>::max());

        auto *val = new QRegularExpressionValidator(
            QRegularExpression(
                R"(^\s*[+-]?(?:\d*(?:\.\d*)?)(?:[eE][+-]?\d*)?\s*$)"),
            this);
        lineEdit()->setValidator(val);

        setLocale(QLocale::C);
    }

protected:
    QValidator::State validate(QString &text, int &pos) const override {
        Q_UNUSED(pos);
        const QString t = text.trimmed();
        if (t.isEmpty()) return QValidator::Intermediate;

        static const QRegularExpression fullRe(
            R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$)"
            );
        if (fullRe.match(t).hasMatch())
            return QValidator::Acceptable;

        static const QRegularExpression partialRe(
            R"(^[+-]?(?:\d*(?:\.\d*)?)(?:[eE][+-]?\d*)?$)"
            );
        if (partialRe.match(t).hasMatch())
            return QValidator::Intermediate;

        return QValidator::Invalid;
    }

    double valueFromText(const QString &text) const override {
        bool ok = false;
        double v = QLocale::c().toDouble(text.trimmed(), &ok);
        if (!ok) v = text.trimmed().toDouble(&ok);
        return ok ? v : QDoubleSpinBox::value();
    }

    QString textFromValue(double value) const override {
        const int d = std::max(decimals(), 12);
        return formatCompact(value, d, 2);
    }
};

#endif // SCIDOUBLESPINBOX_H
