/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#ifndef STACKUPEDITOR_H
#define STACKUPEDITOR_H

#include "substrate.h"

#include <QDialog>
#include <QStyledItemDelegate>

class QTabWidget;
class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;

/*!*******************************************************************************************************************
 * \brief Delegate for Materials Color column: swatch icon, tinted hex text, color picker on edit.
 **********************************************************************************************************************/
class StackupColorDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit StackupColorDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

    static QColor parseColor(const QString &raw);
    static QIcon  colorIcon(const QColor &c);
    static bool   isExpression(const QString &raw);
};

class StackupEditor : public QDialog
{
    Q_OBJECT
public:
    explicit StackupEditor(QWidget *parent = nullptr);

    void setSubstrate(const Substrate &substrate);
    Substrate substrate() const;

    void setFilePath(const QString &path);
    QString filePath() const { return m_filePath; }
    bool isModified() const { return m_modified; }

    /*! Select Dielectrics or Layers row matching a SubstrateView click. */
    void selectStackItem(const QString &name, const QString &kind);
    void clearTableSelection();

public slots:
    void markModified();

signals:
    /*! Emitted after a successful Save / Save As (dialog stays open). */
    void stackupSaved(const QString &filePath);
    /*! Esc pressed — do not close; ask host to clear stack highlight. */
    void clearStackHighlightRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onAddRow();
    void onRemoveRow();
    void onSaveAs();
    void onSave();
    void onMaterialsCellChanged(int row, int column);
    void onColorCellActivated(const QModelIndex &index);
    void onImportAdsSubst();
    void onImportAdsLtd();
    void onExportAdsSubst();

private:
    QTableWidget *currentTable() const;
    void rebuildTablesFromModel();
    void collectModelFromTables();
    void refreshResolvedColumns();
    void updateWindowTitle();
    void setModified(bool modified);
    void styleColorCell(int row, const QString &raw);
    void styleMaterialNameCell(int row, const QString &colorRaw);
    void styleVariableColorCells(int row);
    bool saveToPath(const QString &path);
    bool maybeSave();
    QString resolveAdsConvertScript() const;
    QString resolveHostPython() const;
    bool runAdsConvert(const QStringList &args, QString *stdoutText, QString *stderrText);
    bool loadImportedXml(const QString &xmlPath, const QString &sourceLabel);

    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_tblVars = nullptr;
    QTableWidget *m_tblMats = nullptr;
    QTableWidget *m_tblDiels = nullptr;
    QTableWidget *m_tblLayers = nullptr;
    QTableWidget *m_tblDerived = nullptr;
    QTableWidget *m_tblTables = nullptr;
    QPlainTextEdit *m_edDescription = nullptr;
    QLabel *m_lblSchema = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    StackupColorDelegate *m_colorDelegate = nullptr;
    QString m_filePath;
    Substrate m_substrate;
    bool m_modified = false;
    bool m_blockChangeSignals = false;
};

#endif // STACKUPEDITOR_H
