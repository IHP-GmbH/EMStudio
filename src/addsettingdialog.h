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

#ifndef ADDSETTINGDIALOG_H
#define ADDSETTINGDIALOG_H

#include <QDialog>
#include <QSet>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;

/*!*******************************************************************************************************************
 * \class AddSettingDialog
 * \brief Settings grid → Add setting: pick a keyword by topic (or type a custom one) and its value.
 *
 * The keyword list comes from keywords/<tool>.csv; keys already in the model are shown greyed.
 * The value is prefilled with the workflow default and returned as a Python literal.
 **********************************************************************************************************************/
class AddSettingDialog : public QDialog
{
    Q_OBJECT

public:
    /*! One keyword offered by the dialog. */
    struct Keyword
    {
        QString             name;
        QString             description;
        QString             topic;          //!< Empty = "Other"
        QString             defaultValue;   //!< Python literal, may be empty
        bool                required = false;
    };

    AddSettingDialog(const QVector<Keyword> &keywords, const QSet<QString> &present,
                     const QString &presetTopic, QWidget *parent = nullptr);

    /*! Key to add. */
    QString             key() const;
    /*! Value as a Python literal. */
    QString             pythonValue() const;

    /*! Text → Python literal: numbers, True/False/None, quoted strings, and bracketed
     *  lists / dicts / tuples are kept; other text is quoted. False for unbalanced brackets. */
    static bool         toPythonLiteral(const QString &text, QString *literal);

private slots:
    void                refreshList();
    void                onKeywordChanged();
    void                updateOk();

private:
    QVector<Keyword>    m_keywords;
    QSet<QString>       m_present;
    QComboBox          *m_topicCombo = nullptr;
    QLineEdit          *m_filter = nullptr;
    QListWidget        *m_list = nullptr;
    QLabel             *m_info = nullptr;
    QLineEdit          *m_value = nullptr;
    QCheckBox          *m_custom = nullptr;
    QLineEdit          *m_customName = nullptr;
    QDialogButtonBox   *m_buttons = nullptr;
};

#endif // ADDSETTINGDIALOG_H
