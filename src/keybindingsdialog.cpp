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

#include "keybindingsdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

/*!*******************************************************************************************************************
 * \brief Builds the dialog: style combo, description, binding table, OK / Cancel.
 *
 * \param style  Style selected initially.
 * \param parent Parent widget.
 **********************************************************************************************************************/
KeyBindingsDialog::KeyBindingsDialog(NavStyle style, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Key Bindings"));
    setModal(true);
    resize(720, 640);

    auto *root = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    m_styleCombo = new QComboBox(this);
    m_styleCombo->setObjectName(QStringLiteral("navStyleCombo"));
    for (NavStyle s : {NavStyle::EMStudio, NavStyle::SetupEM})
        m_styleCombo->addItem(NavigationStyle::displayName(s), NavigationStyle::id(s));
    m_styleCombo->setCurrentIndex(m_styleCombo->findData(NavigationStyle::id(style)));
    m_styleCombo->setToolTip(tr("Mouse navigation in the Layout preview and the 3D field viewer."));
    form->addRow(tr("Navigation style:"), m_styleCombo);
    root->addLayout(form);

    m_descriptionLbl = new QLabel(this);
    m_descriptionLbl->setWordWrap(true);
    root->addWidget(m_descriptionLbl);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("keyBindingsTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Viewer"), tr("Action"), tr("Binding")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &KeyBindingsDialog::refreshTable);
    refreshTable();
}

/*!*******************************************************************************************************************
 * \brief Style currently selected in the combo.
 * \return Navigation style.
 **********************************************************************************************************************/
NavStyle KeyBindingsDialog::style() const
{
    return NavigationStyle::fromId(m_styleCombo->currentData().toString());
}

/*!*******************************************************************************************************************
 * \brief Refills the description and the binding table for the selected style.
 **********************************************************************************************************************/
void KeyBindingsDialog::refreshTable()
{
    const NavStyle s = style();
    m_descriptionLbl->setText(NavigationStyle::description(s));

    const QVector<BindingRow> rows = NavigationStyle::bindingTable(s);
    m_table->setRowCount(rows.size());
    QString lastViewer;
    for (int r = 0; r < rows.size(); ++r) {
        const BindingRow &row = rows.at(r);
        // Show the viewer name once per group.
        auto *viewerItem = new QTableWidgetItem(row.viewer == lastViewer ? QString() : row.viewer);
        viewerItem->setToolTip(row.viewer);
        lastViewer = row.viewer;
        m_table->setItem(r, 0, viewerItem);
        m_table->setItem(r, 1, new QTableWidgetItem(row.action));
        m_table->setItem(r, 2, new QTableWidgetItem(row.binding));
    }
}
