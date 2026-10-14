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
 *
 *  Property-browser editors maintained as part of EMStudio (GPL-3.0-or-later).
 ************************************************************************/

#ifndef FILEEDIT_H
#define FILEEDIT_H

#include <QDebug>
#include <QToolButton>

#include "qlineeditd2.h"

class QLabel;

//****************************************************************************************
// FileEdit
//****************************************************************************************
class FileEdit : public QWidget
{
    Q_OBJECT
public:
    enum TYPE{FILE, FOLDER, COLOR, KEYWORDS, RUNMODE, PASSWORD};

    FileEdit(QWidget *parent = 0, TYPE type = FILE);
    //virtual ~FileEdit();

    void                    setFilePath(const QString &);
    QString                 filePath() const;
    void                    setFilter(const QString &);
    QString                 filter() const;
    QToolButton*            getButton() const;
    TYPE                    getType() const;
    void                    setDialogTitle(const QString &);
    void                    setIconColor(const QColor &color);

    QString                 getRunMode() const;
    void                    setRunMode(const QString &mode);

    void                    setKeywords(const QString &keys);
    QString                 getKeywords() const;
    void                    emitFilePathChanged(const QString &str);

signals:
    void                    filePathChanged(const QString &filePath);

protected:
    void                    focusInEvent(QFocusEvent *e);
    void                    focusOutEvent(QFocusEvent *e);
    void                    keyPressEvent(QKeyEvent *e);
    void                    keyReleaseEvent(QKeyEvent *e);

private slots:
    void                    buttonClicked();
    void                    mouseDoubleClickSlot(QMouseEvent *);

private:
    QPalette                getNetlistPalette(QString path);

private:
    QLineEditD2*            theLineEdit;
    QString                 theFilter;
    TYPE                    type;
    QToolButton*            button;
    QString                 m_title;
    QString                 m_runMode;
    QString                 m_keywords;
    QLabel*                 iconBox;    
};

//****************************************************************************************
// FileEdit::getType
//****************************************************************************************
inline FileEdit::TYPE FileEdit::getType() const
{
    return type;
}

//****************************************************************************************
// FileEdit::setFilePath
//****************************************************************************************
inline void FileEdit::setFilePath(const QString &filePath)
{
    if(theLineEdit->text() != filePath) {
        theLineEdit->setText(filePath);
        theLineEdit->setPalette(getNetlistPalette(filePath));
    }
}

//****************************************************************************************
// FileEdit::setRunMode
//****************************************************************************************
inline void FileEdit::setRunMode(const QString &mode)
{
    m_runMode = mode;
}

//****************************************************************************************
// FileEdit::filePath
//****************************************************************************************
inline QString FileEdit::filePath() const
{
    return theLineEdit->text();
}

//****************************************************************************************
// FileEdit::setFilter
//****************************************************************************************
inline void FileEdit::setFilter(const QString &filter)
{
    theFilter = filter;
}

//****************************************************************************************
// FileEdit::filter
//****************************************************************************************
inline QString FileEdit::filter() const
{
    return theFilter;
}

//****************************************************************************************
// FileEdit::getButton
//****************************************************************************************
inline QToolButton* FileEdit::getButton() const
{
    return button;
}

//****************************************************************************************
// FileEdit::setDialogTitle
//****************************************************************************************
inline void FileEdit::setDialogTitle(const QString &title)
{
    m_title = title;
}

//****************************************************************************************
// FileEdit::getRunMode
//****************************************************************************************
inline QString FileEdit::getRunMode() const
{
    return m_runMode;
}

//****************************************************************************************
// FileEdit::getKeywords
//****************************************************************************************
inline QString FileEdit::getKeywords() const
{    
    return m_keywords;
}

//****************************************************************************************
// FileEdit::emitFilePathChanged
//****************************************************************************************
inline void FileEdit::emitFilePathChanged(const QString &str)
{
    emit filePathChanged(str);
}

#endif
