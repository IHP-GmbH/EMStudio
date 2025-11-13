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
