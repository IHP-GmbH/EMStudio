#ifndef SCIDOUBLESPINBOX_H
#define SCIDOUBLESPINBOX_H

#include <QDoubleSpinBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLocale>
#include <limits>
#include <QLineEdit>

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

        // Full scientific/decimal number
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
        if (!ok) v = text.trimmed().toDouble(&ok); // fallback
        return ok ? v : QDoubleSpinBox::value();
    }

    QString textFromValue(double value) const override {
        return QLocale::c().toString(value, 'g', 12);
    }
};

#endif // SCIDOUBLESPINBOX_H
