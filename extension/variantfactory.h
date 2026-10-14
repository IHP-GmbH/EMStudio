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

#ifndef VARIANTFACTORY_H
#define VARIANTFACTORY_H

#include "QtPropertyBrowser/qtvariantproperty.h"
#include "QtPropertyBrowser/SciDoubleSpinBox.h"

class FileEdit;
class QtDoubleSpinBoxFactory;

class VariantFactory : public QtVariantEditorFactory
{
    Q_OBJECT
public:
    VariantFactory(QObject *parent = 0)
        : QtVariantEditorFactory(parent)
            { }

    virtual ~VariantFactory();
protected:
    virtual void connectPropertyManager(QtVariantPropertyManager *manager);
    virtual QWidget *createEditor(QtVariantPropertyManager *manager, QtProperty *property,
                QWidget *parent);
    virtual void disconnectPropertyManager(QtVariantPropertyManager *manager);
private slots:
    void slotPropertyChanged(QtProperty *property, const QVariant &value);
    void slotPropertyAttributeChanged(QtProperty *property, const QString &attribute, const QVariant &value);
    void slotSetValue(const QString &value);
    void slotEditorDestroyed(QObject *object);
    void slotSetDoubleValue(double);
    void slotDoubleEditorDestroyed(QObject *object);
private:
    QMap<QtProperty *, QList<FileEdit *> > theCreatedEditors;
    QMap<FileEdit *, QtProperty *> theEditorToProperty;

    QMap<QtProperty*, QList<SciDoubleSpinBox*>> theCreatedDoubleEditors;
    QMap<SciDoubleSpinBox*, QtProperty*>        theDoubleEditorToProperty;

    void applyDoubleAttributes(SciDoubleSpinBox* w,
                               QtVariantPropertyManager* mgr,
                               QtProperty* prop);
};



#endif
