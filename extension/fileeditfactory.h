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

#ifndef FILEEDITFACTORY_H
#define FILEEDITFACTORY_H

#include "QtPropertyBrowser/qtpropertybrowser.h"
#include "filepathmanager.h"

class FileEdit;

class FileEditFactory : public QtAbstractEditorFactory<FilePathManager>
{
    Q_OBJECT
public:
    FileEditFactory(QObject *parent = 0)
        : QtAbstractEditorFactory<FilePathManager>(parent)
            { }
    virtual ~FileEditFactory();
protected:
    virtual void connectPropertyManager(FilePathManager *manager);
    virtual QWidget *createEditor(FilePathManager *manager, QtProperty *property,
                QWidget *parent);
    virtual void disconnectPropertyManager(FilePathManager *manager);
private slots:
    void slotPropertyChanged(QtProperty *property, const QString &value);
    void slotFilterChanged(QtProperty *property, const QString &filter);
    void slotSetValue(const QString &value);
    void slotEditorDestroyed(QObject *object);
private:
    QMap<QtProperty *, QList<FileEdit *> > theCreatedEditors;
    QMap<FileEdit *, QtProperty *> theEditorToProperty;
};

#endif
