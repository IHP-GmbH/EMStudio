#include "keywordseditor.h"

#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QFontMetrics>
#include <QResizeEvent>
#include <algorithm>

/*!*******************************************************************************************************************
 * \brief Constructs the KeywordsEditorDialog.
 *
 * Creates a modal dialog that loads a keyword/description table from a CSV/TSV file,
 * allows filtering/sorting/editing, and can save back to disk.
 *
 * \param csvPath Full path to the keyword file (CSV/TSV).
 * \param title   Window title.
 * \param parent  Parent widget.
 **********************************************************************************************************************/
KeywordsEditorDialog::KeywordsEditorDialog(const QString& csvPath,
                                           const QString& title,
                                           QWidget* parent)
    : QDialog(parent)
    , m_csvPath(csvPath)
{
    setWindowTitle(title);
    setModal(true);
    resize(900, 600);

    buildUi();
    load();
}

/*!*******************************************************************************************************************
 * \brief Builds the UI elements and connects signals/slots.
 *
 * Creates the filter row, model/proxy/view, and bottom action buttons.
 **********************************************************************************************************************/
void KeywordsEditorDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // Path label
    m_pathLabel = new QLabel(this);
    m_pathLabel->setText(QStringLiteral("File: %1").arg(QDir::toNativeSeparators(m_csvPath)));
    root->addWidget(m_pathLabel);

    // Filter row
    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("Filter:"), this));

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Type to filter keyword/description..."));
    topRow->addWidget(m_filter, 1);

    m_btnSortAZ = new QPushButton(tr("Sort A→Z"), this);
    topRow->addWidget(m_btnSortAZ);

    root->addLayout(topRow);

    // Model + proxy
    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(2);
    m_model->setHeaderData(0, Qt::Horizontal, tr("Keyword"));
    m_model->setHeaderData(1, Qt::Horizontal, tr("Description"));

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1); // all columns
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    // View
    m_view = new QTableView(this);
    m_view->setModel(m_proxy);

    // Make sure long keywords are not elided and row numbers are hidden
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideNone);
    m_view->verticalHeader()->setVisible(false);

    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_view->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(m_view, 1);

    // Buttons
    auto* btnRow = new QHBoxLayout();

    m_btnAdd = new QPushButton(tr("Add"), this);
    m_btnRemove = new QPushButton(tr("Remove"), this);
    m_btnReload = new QPushButton(tr("Reload"), this);

    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnRemove);
    btnRow->addWidget(m_btnReload);
    btnRow->addStretch(1);

    m_btnSave = new QPushButton(tr("Save"), this);
    m_btnClose = new QPushButton(tr("Close"), this);

    m_btnSave->setDefault(true);

    btnRow->addWidget(m_btnSave);
    btnRow->addWidget(m_btnClose);

    root->addLayout(btnRow);

    // Signals
    connect(m_btnAdd,    &QPushButton::clicked, this, &KeywordsEditorDialog::onAddRow);
    connect(m_btnRemove, &QPushButton::clicked, this, &KeywordsEditorDialog::onRemoveSelected);
    connect(m_btnReload, &QPushButton::clicked, this, &KeywordsEditorDialog::onReload);
    connect(m_btnSave,   &QPushButton::clicked, this, [this](){
        if (save()) setDirty(false);
    });
    connect(m_btnClose,  &QPushButton::clicked, this, &QDialog::close);
    connect(m_filter,    &QLineEdit::textChanged, this, &KeywordsEditorDialog::onFilterTextChanged);
    connect(m_btnSortAZ, &QPushButton::clicked, this, &KeywordsEditorDialog::onSortAZ);

    connect(m_model, &QStandardItemModel::itemChanged, this, [this](){
        if (!m_loading)
            setDirty(true);
    });
}

/*!*******************************************************************************************************************
 * \brief Marks the dialog as dirty (modified) and updates the window title.
 *
 * If dirty, a '*' is appended to the title. When clean, the '*' is removed.
 *
 * \param on True to mark dirty, false to mark clean.
 **********************************************************************************************************************/
void KeywordsEditorDialog::setDirty(bool on)
{
    m_dirty = on;

    QString t = windowTitle();
    const bool hasStar = t.endsWith('*');

    if (on && !hasStar)
        setWindowTitle(t + "*");
    else if (!on && hasStar)
        setWindowTitle(t.left(t.size() - 1));
}

/*!*******************************************************************************************************************
 * \brief Intercepts close requests to ask about saving changes.
 *
 * If the dialog has unsaved changes, asks whether to save, discard, or cancel closing.
 *
 * \param e Close event.
 **********************************************************************************************************************/
void KeywordsEditorDialog::closeEvent(QCloseEvent* e)
{
    if (!m_dirty) {
        e->accept();
        return;
    }

    const auto ret = QMessageBox::question(
        this,
        tr("Unsaved changes"),
        tr("You have unsaved changes.\nDo you want to save them?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Yes);

    if (ret == QMessageBox::Cancel) {
        e->ignore();
        return;
    }

    if (ret == QMessageBox::Yes) {
        if (!save()) {
            // keep dialog open if save failed
            e->ignore();
            return;
        }
        setDirty(false);
    }

    e->accept();
}

/*!*******************************************************************************************************************
 * \brief Detects the delimiter used in the input line.
 *
 * Prefers tab if present (TSV), then ';', then ','.
 *
 * \param line A non-empty line from the file.
 * \return Delimiter string.
 **********************************************************************************************************************/
QString KeywordsEditorDialog::detectDelimiter(const QString& line) const
{
    if (line.contains('\t')) return "\t";
    if (line.contains(';'))  return ";";
    if (line.contains(','))  return ",";
    return "\t";
}

/*!*******************************************************************************************************************
 * \brief Splits a line into keyword/description.
 *
 * Minimal 2-column parser: the first delimiter separates keyword and description.
 * Everything after the first delimiter is considered description.
 *
 * \param line  Input line.
 * \param delim Delimiter to use.
 * \return QStringList with 2 elements: { keyword, description }.
 **********************************************************************************************************************/
QStringList KeywordsEditorDialog::splitLine(const QString& line, const QString& delim) const
{
    const int idx = line.indexOf(delim);
    if (idx < 0) return { line.trimmed(), QString() };

    const QString k = line.left(idx).trimmed();
    const QString d = line.mid(idx + delim.size()).trimmed();
    return { k, d };
}

/*!*******************************************************************************************************************
 * \brief Loads keyword data from disk into the model.
 *
 * If the file does not exist, the directory is created and an empty table is shown.
 * Encoding is detected: UTF-16 BOM => UTF-16, otherwise UTF-8.
 *
 * \return True on success, false on read/open error.
 **********************************************************************************************************************/
bool KeywordsEditorDialog::load()
{
    m_loading = true;

    m_model->clear();
    m_model->setColumnCount(2);
    m_model->setHeaderData(0, Qt::Horizontal, tr("Keyword"));
    m_model->setHeaderData(1, Qt::Horizontal, tr("Description"));

    QFile f(m_csvPath);
    if (!f.exists()) {
        QDir().mkpath(QFileInfo(m_csvPath).absolutePath());
        m_loading = false;
        setDirty(false);
        return true;
    }

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Keywords"), tr("Cannot open file:\n%1").arg(m_csvPath));
        m_loading = false;
        return false;
    }

    const QByteArray head = f.peek(4);
    QTextStream ts(&f);
    if (head.startsWith("\xFF\xFE") || head.startsWith("\xFE\xFF")) ts.setCodec("UTF-16");
    else ts.setCodec("UTF-8");

    bool firstNonEmpty = true;

    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.trimmed().isEmpty())
            continue;

        if (firstNonEmpty) {
            m_lastDelimiter = detectDelimiter(line);
            firstNonEmpty = false;
        }

        const QStringList cols = splitLine(line, m_lastDelimiter);
        QList<QStandardItem*> row;
        row << new QStandardItem(cols.value(0));
        row << new QStandardItem(cols.value(1));
        m_model->appendRow(row);
    }

    m_proxy->invalidate();

    m_loading = false;
    setDirty(false);

    m_view->sortByColumn(0, Qt::AscendingOrder);

    m_view->resizeColumnToContents(0);

    return true;
}

/*!*******************************************************************************************************************
 * \brief Saves the current model data back to disk.
 *
 * Validates that keywords are non-empty and unique (case-insensitive).
 * Saves using UTF-8 and the last detected delimiter (defaults to tab).
 *
 * \return True on success, false on validation or write error.
 **********************************************************************************************************************/
bool KeywordsEditorDialog::save()
{
    QSet<QString> seen;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QString k = m_model->item(r, 0) ? m_model->item(r, 0)->text().trimmed() : QString();
        if (k.isEmpty()) {
            QMessageBox::warning(this, tr("Keywords"), tr("Row %1 has empty keyword.").arg(r + 1));
            return false;
        }
        const QString kl = k.toLower();
        if (seen.contains(kl)) {
            QMessageBox::warning(this, tr("Keywords"), tr("Duplicate keyword: %1").arg(k));
            return false;
        }
        seen.insert(kl);
    }

    QDir().mkpath(QFileInfo(m_csvPath).absolutePath());

    QFile f(m_csvPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Keywords"), tr("Cannot write file:\n%1").arg(m_csvPath));
        return false;
    }

    QTextStream ts(&f);
    ts.setCodec("UTF-8");

    const QString delim = m_lastDelimiter.isEmpty() ? "\t" : m_lastDelimiter;

    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QString k = m_model->item(r, 0)->text().trimmed();
        const QString d = m_model->item(r, 1)->text().trimmed();
        ts << k << delim << d << "\n";
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Adds a new row with placeholder keyword/description.
 *
 * Focuses and starts editing the new keyword cell.
 **********************************************************************************************************************/
void KeywordsEditorDialog::onAddRow()
{
    QList<QStandardItem*> row;
    row << new QStandardItem(QStringLiteral("new_keyword"));
    row << new QStandardItem(QStringLiteral("description..."));
    m_model->appendRow(row);

    // Focus new row
    const QModelIndex srcIdx = m_model->index(m_model->rowCount() - 1, 0);
    const QModelIndex viewIdx = m_proxy->mapFromSource(srcIdx);
    if (viewIdx.isValid()) {
        m_view->scrollTo(viewIdx);
        m_view->setCurrentIndex(viewIdx);
        m_view->edit(viewIdx);
    }

    setDirty(true);
}

/*!*******************************************************************************************************************
 * \brief Removes all currently selected rows from the model.
 *
 * Rows are removed in source-model coordinates from bottom to top.
 **********************************************************************************************************************/
void KeywordsEditorDialog::onRemoveSelected()
{
    const QModelIndexList sel = m_view->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return;

    QList<int> srcRows;
    srcRows.reserve(sel.size());
    for (const QModelIndex& idx : sel) {
        const QModelIndex src = m_proxy->mapToSource(idx);
        if (src.isValid())
            srcRows << src.row();
    }

    std::sort(srcRows.begin(), srcRows.end(), std::greater<int>());
    srcRows.erase(std::unique(srcRows.begin(), srcRows.end()), srcRows.end());

    for (int r : srcRows)
        m_model->removeRow(r);

    setDirty(true);
}

/*!*******************************************************************************************************************
 * \brief Reloads the file from disk.
 *
 * If there are unsaved changes, asks the user whether to discard them.
 **********************************************************************************************************************/
void KeywordsEditorDialog::onReload()
{
    if (m_dirty) {
        const auto ret = QMessageBox::question(
            this,
            tr("Reload"),
            tr("Discard unsaved changes and reload from disk?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }
    load();
}

/*!*******************************************************************************************************************
 * \brief Handles dialog resize events.
 *
 * Keeps the keyword column width fitted to the longest keyword (limited by viewport width).
 *
 * \param e Resize event.
 **********************************************************************************************************************/
void KeywordsEditorDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
}

/*!*******************************************************************************************************************
 * \brief Updates the proxy filter using the provided text.
 *
 * The filter is applied to all columns (filterKeyColumn = -1).
 *
 * \param text Filter string typed by the user.
 **********************************************************************************************************************/
void KeywordsEditorDialog::onFilterTextChanged(const QString& text)
{
    m_proxy->setFilterFixedString(text.trimmed());
}

/*!*******************************************************************************************************************
 * \brief Sorts the table by keyword (ascending).
 **********************************************************************************************************************/
void KeywordsEditorDialog::onSortAZ()
{
    m_view->sortByColumn(0, Qt::AscendingOrder);
}
