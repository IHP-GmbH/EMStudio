/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QDialog>

class QtProperty;
class QtTreePropertyBrowser;
class QtVariantEditorFactory;
class QtVariantPropertyManager;

namespace Ui {
class Preferences;
}

/*!*******************************************************************************************************************
 * \class Preferences
 * \brief Dialog for editing user-configurable application preferences.
 *
 * Provides a property-based UI using QtPropertyBrowser for managing key-value settings stored in a QMap.
 * Preferences are applied directly to the referenced settings map.
 *
 * Features:
 * - Tree-based property editor with variant support
 * - Cancel and Apply actions
 *
 * \see QtVariantPropertyManager, QtTreePropertyBrowser
 **********************************************************************************************************************/
class Preferences : public QDialog
{
    Q_OBJECT

public:
    explicit Preferences(QMap<QString, QVariant> &preferences, QWidget *parent = nullptr);
    ~Preferences();

private slots:
    void                            on_btnCancel_clicked();
    void                            on_btnApply_clicked();

private:
    void                            setupPreferencesPanel();

private:
    Ui::Preferences                 *m_ui;

    QtVariantPropertyManager        *m_variantManager = nullptr;
    QtTreePropertyBrowser           *m_propertyBrowser = nullptr;

    QMap<QString, QVariant>         &m_preferences;
};

#endif // PREFERENCES_H
