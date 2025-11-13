/****************************************************************************
**
** Copyright (C) 2006 Trolltech ASA. All rights reserved.
**
** This file is part of the documentation of Qt. It was originally
** published as part of Qt Quarterly.
**
** This file may be used under the terms of the GNU General Public License
** version 2.0 as published by the Free Software Foundation or under the
** terms of the Qt Commercial License Agreement. The respective license
** texts for these are provided with the open source and commercial
** editions of Qt.
**
** If you are unsure which license is appropriate for your use, please
** review the following information:
** http://www.trolltech.com/products/qt/licensing.html or contact the
** sales department at sales@trolltech.com.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

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
