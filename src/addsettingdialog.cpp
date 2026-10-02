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

#include "addsettingdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
constexpr int kRoleName = Qt::UserRole + 1;
QString topicOf(const AddSettingDialog::Keyword &k)
{
    return k.topic.isEmpty() ? QObject::tr("Other") : k.topic;
}
} // namespace

/*!*******************************************************************************************************************
 * \brief Builds the dialog.
 *
 * \param keywords    Keywords of the current tool, in keyword-file order.
 * \param present     Keys the model already has (shown greyed, can't be added).
 * \param presetTopic Topic to show first (empty: all topics).
 * \param parent      Parent widget.
 **********************************************************************************************************************/
AddSettingDialog::AddSettingDialog(const QVector<Keyword> &keywords, const QSet<QString> &present,
                                   const QString &presetTopic, QWidget *parent)
    : QDialog(parent), m_keywords(keywords), m_present(present)
{
    setWindowTitle(tr("Add Setting"));
    setModal(true);
    resize(560, 520);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_topicCombo = new QComboBox(this);
    m_topicCombo->setObjectName(QStringLiteral("addSettingTopic"));
    m_topicCombo->addItem(tr("All topics"), QString());
    QStringList topics;
    for (const Keyword &k : m_keywords)
        if (!topics.contains(topicOf(k)))
            topics << topicOf(k);
    for (const QString &t : topics)
        m_topicCombo->addItem(t, t);
    const int preset = m_topicCombo->findData(presetTopic);
    m_topicCombo->setCurrentIndex(preset >= 0 ? preset : 0);
    form->addRow(tr("Topic:"), m_topicCombo);
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("addSettingFilter"));
    m_filter->setPlaceholderText(tr("Filter keywords and descriptions..."));
    m_filter->setClearButtonEnabled(true);
    form->addRow(tr("Filter:"), m_filter);
    root->addLayout(form);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("addSettingList"));
    root->addWidget(m_list, 1);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(48);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_info);

    auto *valueForm = new QFormLayout;
    m_custom = new QCheckBox(tr("Custom keyword (not in the list, goes to Other)"), this);
    m_custom->setObjectName(QStringLiteral("addSettingCustom"));
    valueForm->addRow(m_custom);
    m_customName = new QLineEdit(this);
    m_customName->setObjectName(QStringLiteral("addSettingCustomName"));
    m_customName->setEnabled(false);
    m_customName->setPlaceholderText(tr("keyword"));
    valueForm->addRow(tr("Keyword:"), m_customName);
    m_value = new QLineEdit(this);
    m_value->setObjectName(QStringLiteral("addSettingValue"));
    m_value->setPlaceholderText(tr("Python value, e.g. 10, True, 'text', [1e9, 2e9]"));
    valueForm->addRow(tr("Value:"), m_value);
    root->addLayout(valueForm);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttons);

    connect(m_topicCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AddSettingDialog::refreshList);
    connect(m_filter, &QLineEdit::textChanged, this, &AddSettingDialog::refreshList);
    connect(m_list, &QListWidget::currentItemChanged, this, &AddSettingDialog::onKeywordChanged);
    connect(m_custom, &QCheckBox::toggled, this, [this](bool on) {
        m_customName->setEnabled(on);
        m_list->setEnabled(!on);
        if (on)
            m_customName->setFocus();
        onKeywordChanged();
    });
    connect(m_customName, &QLineEdit::textChanged, this, &AddSettingDialog::updateOk);
    connect(m_value, &QLineEdit::textChanged, this, &AddSettingDialog::updateOk);

    refreshList();
}

/*!*******************************************************************************************************************
 * \brief Refills the keyword list for the topic and filter; selects the first addable keyword.
 **********************************************************************************************************************/
void AddSettingDialog::refreshList()
{
    const QString topic = m_topicCombo->currentData().toString();
    const QString filter = m_filter->text().trimmed();
    m_list->clear();
    QListWidgetItem *first = nullptr;
    for (const Keyword &k : m_keywords) {
        if (!topic.isEmpty() && topicOf(k) != topic)
            continue;
        if (!filter.isEmpty() && !k.name.contains(filter, Qt::CaseInsensitive)
            && !k.description.contains(filter, Qt::CaseInsensitive))
            continue;
        const bool inModel = m_present.contains(k.name);
        auto *it = new QListWidgetItem(inModel ? tr("%1   (in model)").arg(k.name) : k.name, m_list);
        it->setData(kRoleName, k.name);
        it->setToolTip(k.description);
        QFont f = it->font();
        f.setBold(k.required);
        it->setFont(f);
        if (inModel)
            it->setFlags(it->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable));
        else if (!first)
            first = it;
    }
    if (first)
        m_list->setCurrentItem(first);
    else
        onKeywordChanged();
}

/*!*******************************************************************************************************************
 * \brief Shows the selected keyword's description and prefills its default value.
 **********************************************************************************************************************/
void AddSettingDialog::onKeywordChanged()
{
    if (m_custom->isChecked()) {
        m_info->setText(tr("A keyword the keyword file doesn't list. It is added to the settings dict "
                           "and shown under Other; the workflow must know it to use it."));
        updateOk();
        return;
    }
    const QListWidgetItem *it = m_list->currentItem();
    const QString name = it ? it->data(kRoleName).toString() : QString();
    for (const Keyword &k : m_keywords) {
        if (k.name != name)
            continue;
        QString text = QStringLiteral("<b>%1</b> (%2)").arg(k.name.toHtmlEscaped(), topicOf(k).toHtmlEscaped());
        if (k.required)
            text += tr(" <b>Required.</b>");
        text += QStringLiteral("<br>") + k.description.toHtmlEscaped();
        if (!k.defaultValue.isEmpty())
            text += tr("<br>Workflow default: <tt>%1</tt>").arg(k.defaultValue.toHtmlEscaped());
        m_info->setText(text);
        m_value->setText(k.defaultValue);
        updateOk();
        return;
    }
    m_info->setText(m_list->count() ? tr("Select a keyword.") : tr("No keyword to add for this topic and filter."));
    updateOk();
}

/*!*******************************************************************************************************************
 * \brief Enables Add for a valid keyword and value.
 **********************************************************************************************************************/
void AddSettingDialog::updateOk()
{
    QString literal;
    const bool valueOk = toPythonLiteral(m_value->text(), &literal);
    bool keyOk = false;
    if (m_custom->isChecked()) {
        static const QRegularExpression reName(QStringLiteral(R"(^[A-Za-z_]\w*$)"));
        const QString name = m_customName->text().trimmed();
        keyOk = reName.match(name).hasMatch() && !m_present.contains(name);
    } else {
        keyOk = m_list->currentItem() && (m_list->currentItem()->flags() & Qt::ItemIsEnabled);
    }
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(keyOk && valueOk);
}

QString AddSettingDialog::key() const
{
    if (m_custom->isChecked())
        return m_customName->text().trimmed();
    return m_list->currentItem() ? m_list->currentItem()->data(kRoleName).toString() : QString();
}

QString AddSettingDialog::pythonValue() const
{
    QString literal;
    toPythonLiteral(m_value->text(), &literal);
    return literal;
}

/*!*******************************************************************************************************************
 * \brief Converts typed text to a Python literal.
 *
 * \param text    Text from the value field.
 * \param literal Receives the literal.
 * \return False for an empty value or unbalanced brackets / quotes.
 **********************************************************************************************************************/
bool AddSettingDialog::toPythonLiteral(const QString &text, QString *literal)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    static const QRegularExpression reNumber(
        QStringLiteral(R"(^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$)"));
    static const QRegularExpression reQuoted(QStringLiteral(R"(^(['"]).*\1$)"));
    if (reNumber.match(t).hasMatch() || t == QLatin1String("True") || t == QLatin1String("False")
        || t == QLatin1String("None") || reQuoted.match(t).hasMatch()) {
        *literal = t;
        return true;
    }
    const QChar first = t.front();
    if (first == QLatin1Char('[') || first == QLatin1Char('{') || first == QLatin1Char('(')) {
        int depth = 0;
        QChar quote;
        for (int i = 0; i < t.size(); ++i) {
            const QChar c = t.at(i);
            if (!quote.isNull()) {
                if (c == QLatin1Char('\\'))
                    ++i;
                else if (c == quote)
                    quote = QChar();
            } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
                quote = c;
            } else if (c == QLatin1Char('[') || c == QLatin1Char('{') || c == QLatin1Char('(')) {
                ++depth;
            } else if (c == QLatin1Char(']') || c == QLatin1Char('}') || c == QLatin1Char(')')) {
                if (--depth < 0)
                    return false;
            }
        }
        if (depth != 0 || !quote.isNull())
            return false;
        *literal = t;
        return true;
    }
    // Plain text: a quoted string.
    *literal = QStringLiteral("'%1'").arg(QString(t).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                              .replace(QLatin1Char('\''), QStringLiteral("\\'")));
    return true;
}
