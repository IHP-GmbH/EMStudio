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
 ************************************************************************/

#include "tst_keywords_editor_dialog.h"

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextStream>

#include "keywordseditor.h"

/*!*******************************************************************************************************************
 * \brief Reads a UTF-8 text file for verification in keyword editor tests.
 *
 * \param path File path.
 * \return File contents, or empty string on failure.
 **********************************************************************************************************************/
static QString readUtf8Text(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    return QString::fromUtf8(f.readAll());
}

/*!*******************************************************************************************************************
 * \brief Verifies loading, filtering, adding, sorting and saving in KeywordsEditorDialog.
 *
 * The test creates a temporary TSV file, opens the dialog, checks initial load,
 * applies a filter, adds a row, edits the inserted values directly through the model,
 * triggers sorting and save, and verifies that the resulting file contains expected data.
 **********************************************************************************************************************/
void KeywordsEditorDialogTest::load_add_filter_sort_save_roundtrip()
{
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "Failed to create temporary directory");

    const QString csvPath = QDir(tmpDir.path()).filePath("keywords.tsv");

    {
        QFile f(csvPath);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text), "Failed to create temporary keywords file");

        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        ts << "banana\tYellow fruit\n";
        ts << "apple\tRed fruit\n";
    }

    KeywordsEditorDialog dlg(csvPath, "Keywords Test");

    auto* filter = dlg.findChild<QLineEdit*>();
    auto* view = dlg.findChild<QTableView*>();
    auto* model = dlg.findChild<QStandardItemModel*>();
    auto* proxy = dlg.findChild<QSortFilterProxyModel*>();

    QVERIFY2(filter, "Filter line edit not found");
    QVERIFY2(view, "Table view not found");
    QVERIFY2(model, "Standard item model not found");
    QVERIFY2(proxy, "Proxy model not found");

    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->columnCount(), 2);

    filter->setText("banana");
    QCOMPARE(proxy->rowCount(), 1);

    filter->clear();
    QCOMPARE(proxy->rowCount(), 2);

    const auto buttons = dlg.findChildren<QPushButton*>();
    QVERIFY2(!buttons.isEmpty(), "No buttons found in KeywordsEditorDialog");

    QPushButton* btnAdd = nullptr;
    QPushButton* btnSave = nullptr;
    QPushButton* btnSort = nullptr;

    for (QPushButton* btn : buttons) {
        if (btn->text().contains("Add", Qt::CaseInsensitive))
            btnAdd = btn;
        else if (btn->text().contains("Save", Qt::CaseInsensitive))
            btnSave = btn;
        else if (btn->text().contains("Sort", Qt::CaseInsensitive))
            btnSort = btn;
    }

    QVERIFY2(btnAdd, "Add button not found");
    QVERIFY2(btnSave, "Save button not found");
    QVERIFY2(btnSort, "Sort button not found");

    QTest::mouseClick(btnAdd, Qt::LeftButton);
    QCOMPARE(model->rowCount(), 3);

    model->item(model->rowCount() - 1, 0)->setText("cherry");
    model->item(model->rowCount() - 1, 1)->setText("Dark red fruit");

    QTest::mouseClick(btnSort, Qt::LeftButton);
    QTest::mouseClick(btnSave, Qt::LeftButton);

    const QString saved = readUtf8Text(csvPath);

    QVERIFY2(saved.contains("apple\tRed fruit"), qPrintable(saved));
    QVERIFY2(saved.contains("banana\tYellow fruit"), qPrintable(saved));
    QVERIFY2(saved.contains("cherry\tDark red fruit"), qPrintable(saved));
}
