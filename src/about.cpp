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

#include "about.h"
#include "ui_about.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QPixmap>

/*!*******************************************************************************************************************
 * \brief Constructs the About dialog and initializes UI elements.
 *
 * Sets up the dialog layout, removes the Windows context help button,
 * adjusts the initial window size, and connects the Close button.
 *
 * \param parent Pointer to the parent widget.
 **********************************************************************************************************************/
AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::AboutDialog)
{
    m_ui->setupUi(this);

    // Remove '?' help button on Windows title bar
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    initUi();

    resize(360, sizeHint().height());

    connect(m_ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
}

/*!*******************************************************************************************************************
 * \brief Destructor of AboutDialog.
 *
 * Cleans up allocated UI resources.
 **********************************************************************************************************************/
AboutDialog::~AboutDialog()
{
    delete m_ui;
}

/*!*******************************************************************************************************************
 * \brief Initializes dynamic information shown in the About dialog.
 *
 * Populates version information, Qt runtime version, build type
 * (Debug/Release), build timestamp, and loads the application logo.
 *
 * The application version is retrieved from QCoreApplication,
 * which is set during application startup.
 **********************************************************************************************************************/
void AboutDialog::initUi()
{
    // Application logo
    m_ui->lblLogo->setPixmap(QPixmap(":/logo"));

    // Application version (MAJOR.MINOR from git-based versioning)
    m_ui->lblVersion->setText(
        QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("dev")
            : QCoreApplication::applicationVersion()
        );

    // Qt runtime version
    m_ui->lblQt->setText(QString::fromLatin1(qVersion()));

#ifdef QT_DEBUG
    const QString buildType = QStringLiteral("Debug");
#else
    const QString buildType = QStringLiteral("Release");
#endif

    // Build info (type + timestamp)
    m_ui->lblBuild->setText(
        QStringLiteral("%1 | %2")
            .arg(buildType,
                 QDateTime::currentDateTime().toString(Qt::ISODate))
        );
}
