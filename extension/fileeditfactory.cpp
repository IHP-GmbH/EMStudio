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

#include "fileeditfactory.h"
#include "fileedit.h"

FileEditFactory::~FileEditFactory()
{
    QList<FileEdit *> editors = theEditorToProperty.keys();
    QListIterator<FileEdit *> it(editors);
    while (it.hasNext())
        delete it.next();
}

void FileEditFactory::connectPropertyManager(FilePathManager *manager)
{
    connect(manager, SIGNAL(valueChanged(QtProperty *, const QString &)),
                this, SLOT(slotPropertyChanged(QtProperty *, const QString &)));
    connect(manager, SIGNAL(filterChanged(QtProperty *, const QString &)),
                this, SLOT(slotFilterChanged(QtProperty *, const QString &)));
}

QWidget *FileEditFactory::createEditor(FilePathManager *manager,
        QtProperty *property, QWidget *parent)
{
    FileEdit *editor = new FileEdit(parent);
    editor->setFilePath(manager->value(property));
    editor->setFilter(manager->filter(property));
    theCreatedEditors[property].append(editor);
    theEditorToProperty[editor] = property;

    connect(editor, SIGNAL(filePathChanged(const QString &)),
                this, SLOT(slotSetValue(const QString &)));
    connect(editor, SIGNAL(destroyed(QObject *)),
                this, SLOT(slotEditorDestroyed(QObject *)));
    return editor;
}

void FileEditFactory::disconnectPropertyManager(FilePathManager *manager)
{
    disconnect(manager, SIGNAL(valueChanged(QtProperty *, const QString &)),
                this, SLOT(slotPropertyChanged(QtProperty *, const QString &)));
    disconnect(manager, SIGNAL(filterChanged(QtProperty *, const QString &)),
                this, SLOT(slotFilterChanged(QtProperty *, const QString &)));
}

void FileEditFactory::slotPropertyChanged(QtProperty *property,
                const QString &value)
{
    if (!theCreatedEditors.contains(property))
        return;

    QList<FileEdit *> editors = theCreatedEditors[property];
    QListIterator<FileEdit *> itEditor(editors);
    while (itEditor.hasNext())
        itEditor.next()->setFilePath(value);
}

void FileEditFactory::slotFilterChanged(QtProperty *property,
            const QString &filter)
{
    if (!theCreatedEditors.contains(property))
        return;

    QList<FileEdit *> editors = theCreatedEditors[property];
    QListIterator<FileEdit *> itEditor(editors);
    while (itEditor.hasNext())
        itEditor.next()->setFilter(filter);
}

void FileEditFactory::slotSetValue(const QString &value)
{
    QObject *object = sender();
    QMap<FileEdit *, QtProperty *>::ConstIterator itEditor =
                theEditorToProperty.constBegin();
    while (itEditor != theEditorToProperty.constEnd()) {
        if (itEditor.key() == object) {
            QtProperty *property = itEditor.value();
            FilePathManager *manager = propertyManager(property);
            if (!manager)
                return;
            manager->setValue(property, value);
            return;
        }
        itEditor++;
    }
}

void FileEditFactory::slotEditorDestroyed(QObject *object)
{
    QMap<FileEdit *, QtProperty *>::ConstIterator itEditor =
                theEditorToProperty.constBegin();
    while (itEditor != theEditorToProperty.constEnd()) {
        if (itEditor.key() == object) {
            FileEdit *editor = itEditor.key();
            QtProperty *property = itEditor.value();
            theEditorToProperty.remove(editor);
            theCreatedEditors[property].removeAll(editor);
            if (theCreatedEditors[property].isEmpty())
                theCreatedEditors.remove(property);
            return;
        }
        itEditor++;
    }
}

