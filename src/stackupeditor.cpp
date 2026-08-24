/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2025 IHP Authors
 ************************************************************************/

#include "stackupeditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QSizePolicy>
#include <QFileDialog>
#include <QMessageBox>
#include <QHash>
#include <QColorDialog>
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QPixmap>
#include <QApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStandardPaths>
#include <QKeySequence>
#include <QDir>
#include <QKeyEvent>
#include <QComboBox>
#include <QFont>
#include <QApplication>
#include <QSet>

namespace {

constexpr int kColorCol = 8;
constexpr int kNameCol = 0;

QTableWidget *makeTable(const QStringList &headers)
{
    auto *t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(true);
    // Do NOT use AllEditTriggers: opening editors while setItem() fills rows
    // can commit empty combo/line-edit values and wipe the model.
    t->setEditTriggers(QAbstractItemView::DoubleClicked
                       | QAbstractItemView::EditKeyPressed
                       | QAbstractItemView::AnyKeyPressed);
    // Inherit application font (HiDPI-aware). Do not force setFont() here.
    return t;
}

QString itemText(QTableWidget *t, int row, int col)
{
    if (auto *it = t->item(row, col))
        return it->text().trimmed();
    return {};
}

void setItem(QTableWidget *t, int row, int col, const QString &text, bool readOnly = false)
{
    auto *it = new QTableWidgetItem(text);
    if (readOnly)
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    t->setItem(row, col, it);
}

void connectTableModified(QTableWidget *t, StackupEditor *ed)
{
    QObject::connect(t, &QTableWidget::itemChanged, ed, &StackupEditor::markModified);
}

} // namespace

// -------------------------------------------------------------------------------------------------
// StackupColorDelegate
// -------------------------------------------------------------------------------------------------

StackupColorDelegate::StackupColorDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

bool StackupColorDelegate::isExpression(const QString &raw)
{
    return raw.trimmed().startsWith(QLatin1Char('='));
}

QColor StackupColorDelegate::parseColor(const QString &raw)
{
    QString h = raw.trimmed();
    if (isExpression(h) || h.isEmpty())
        return QColor();
    if (h.startsWith(QLatin1Char('#')))
        h = h.mid(1);
    if (h.size() == 6) {
        QColor c(QLatin1Char('#') + h);
        if (c.isValid())
            return c;
    }
    QColor c(raw.trimmed());
    return c.isValid() ? c : QColor();
}

QIcon StackupColorDelegate::colorIcon(const QColor &c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (c.isValid()) {
        p.setBrush(c);
        p.setPen(QPen(Qt::black, 1));
    } else {
        p.setBrush(Qt::white);
        p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    }
    p.drawRoundedRect(1, 1, 12, 12, 2, 2);
    return QIcon(pm);
}

void StackupColorDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QString raw = index.data(Qt::EditRole).toString();
    const QColor c = parseColor(raw);

    if (c.isValid() && !isExpression(raw)) {
        // Contrast: dark text on light colors, light text on dark colors
        const double luma = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
        opt.palette.setColor(QPalette::Text, luma > 0.55 ? QColor(20, 20, 20) : QColor(250, 250, 250));
        opt.palette.setColor(QPalette::HighlightedText,
                             luma > 0.55 ? QColor(20, 20, 20) : QColor(250, 250, 250));
        if (!(opt.state & QStyle::State_Selected))
            opt.backgroundBrush = c;
    }

    if (!opt.icon.isNull()) {
        // keep decoration from model
    } else if (c.isValid()) {
        opt.icon = colorIcon(c);
        opt.decorationSize = QSize(14, 14);
        opt.features |= QStyleOptionViewItem::HasDecoration;
    }

    QStyledItemDelegate::paint(painter, opt, index);
}

QWidget *StackupColorDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &,
                                            const QModelIndex &index) const
{
    const QString raw = index.data(Qt::EditRole).toString().trimmed();
    if (isExpression(raw)) {
        auto *ed = new QLineEdit(parent);
        ed->setPlaceholderText(QStringLiteral("=variable or RRGGBB"));
        return ed;
    }

    // For hex colors: open picker immediately via editorEvent; fallback line edit
    auto *ed = new QLineEdit(parent);
    ed->setPlaceholderText(QStringLiteral("RRGGBB or =variable"));
    return ed;
}

void StackupColorDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *ed = qobject_cast<QLineEdit *>(editor))
        ed->setText(index.data(Qt::EditRole).toString());
}

void StackupColorDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    if (auto *ed = qobject_cast<QLineEdit *>(editor)) {
        QString t = ed->text().trimmed();
        if (t.startsWith(QLatin1Char('#')))
            t = t.mid(1);
        model->setData(index, t, Qt::EditRole);
    }
}

bool StackupColorDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        const QString raw = index.data(Qt::EditRole).toString().trimmed();
        if (!isExpression(raw)) {
            QColor initial = parseColor(raw);
            if (!initial.isValid())
                initial = Qt::white;
            const QColor chosen = QColorDialog::getColor(
                initial, qobject_cast<QWidget *>(parent()),
                tr("Select material color"),
                QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                model->setData(index, chosen.name(QColor::HexRgb).mid(1), Qt::EditRole);
                return true;
            }
            return true; // consume dbl-click even if cancelled
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// -------------------------------------------------------------------------------------------------
// StackupComboDelegate
// -------------------------------------------------------------------------------------------------

StackupComboDelegate::StackupComboDelegate(ItemsFn itemsFn, QObject *parent,
                                           bool editable, bool allowEmpty)
    : QStyledItemDelegate(parent)
    , m_itemsFn(std::move(itemsFn))
    , m_editable(editable)
    , m_allowEmpty(allowEmpty)
{
}

QWidget *StackupComboDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &,
                                            const QModelIndex &) const
{
    auto *cb = new QComboBox(parent);
    cb->setEditable(m_editable);
    cb->setInsertPolicy(QComboBox::NoInsert);
    if (m_allowEmpty)
        cb->addItem(QString());
    if (m_itemsFn) {
        const QStringList items = m_itemsFn();
        for (const QString &s : items) {
            if (s.isEmpty())
                continue;
            if (cb->findText(s, Qt::MatchExactly) < 0)
                cb->addItem(s);
        }
    }
    return cb;
}

void StackupComboDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *cb = qobject_cast<QComboBox *>(editor);
    if (!cb)
        return;
    const QString cur = index.data(Qt::EditRole).toString();
    int i = cb->findText(cur, Qt::MatchExactly);
    if (i < 0 && !cur.isEmpty()) {
        cb->addItem(cur);
        i = cb->findText(cur, Qt::MatchExactly);
    }
    if (i >= 0)
        cb->setCurrentIndex(i);
    else
        cb->setEditText(cur);
}

void StackupComboDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    auto *cb = qobject_cast<QComboBox *>(editor);
    if (!cb)
        return;
    model->setData(index, cb->currentText().trimmed(), Qt::EditRole);
}

void StackupComboDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                                const QModelIndex &) const
{
    editor->setGeometry(option.rect);
}

// -------------------------------------------------------------------------------------------------
// StackupEditor
// -------------------------------------------------------------------------------------------------

StackupEditor::StackupEditor(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Stackup Editor"));
    setWindowFlag(Qt::Window);
    setModal(false);
    resize(1100, 700);
    applyUniformFonts();

    auto *root = new QVBoxLayout(this);

    auto *menuBar = new QMenuBar(this);
    auto *fileMenu = menuBar->addMenu(tr("&File"));
    auto *importMenu = fileMenu->addMenu(tr("&Import"));
    importMenu->addAction(tr("ADS Momentum (*.subst + materials.matdb)..."),
                          this, &StackupEditor::onImportAdsSubst);
    importMenu->addAction(tr("ADS Momentum (*.ltd)..."),
                          this, &StackupEditor::onImportAdsLtd);
    auto *exportMenu = fileMenu->addMenu(tr("&Export"));
    exportMenu->addAction(tr("ADS Momentum (*.subst + materials.matdb)..."),
                          this, &StackupEditor::onExportAdsSubst);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save"), this, &StackupEditor::onSave, QKeySequence::Save);
    fileMenu->addAction(tr("Save &As..."), this, &StackupEditor::onSaveAs, QKeySequence::SaveAs);
    root->setMenuBar(menuBar);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Description:")), 0, Qt::AlignTop);
    m_edDescription = new QPlainTextEdit;
    m_edDescription->setPlaceholderText(tr("Optional file description (saved as XML comment)"));
    m_edDescription->setTabChangesFocus(true);
    m_edDescription->setMinimumHeight(54);
    m_edDescription->setMaximumHeight(96);
    m_edDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    top->addWidget(m_edDescription, 1);
    m_lblSchema = new QLabel;
    m_lblSchema->setAlignment(Qt::AlignTop | Qt::AlignRight);
    top->addWidget(m_lblSchema, 0, Qt::AlignTop);
    root->addLayout(top);

    m_tabs = new QTabWidget;
    m_tblVars = makeTable({tr("Name"), tr("Value"), tr("Type"), tr("Resolved")});
    m_tblMats = makeTable({tr("Name"), tr("Type"), tr("Permittivity"), tr("LossTan"),
                           tr("Conductivity"), tr("Rs"), tr("ThermalCond"),
                           tr("ThermalTable"), tr("Color")});
    m_tblDiels = makeTable({tr("Name"), tr("Material"), tr("Thickness"), tr("Reference"),
                            tr("Ref. Edge"), tr("Zmin (resulting)"), tr("Zmax (resulting)")});
    m_tblLayers = makeTable({tr("Name"), tr("Type"), tr("Material"), tr("GDS Layer"),
                             tr("Zmin"), tr("Zmax"), tr("Reference"), tr("Ref. Edge"),
                             tr("Zmin (resulting)"), tr("Zmax (resulting)")});
    m_tblDerived = makeTable({tr("Name"), tr("GDS Layer"), tr("Operation"), tr("Operands")});
    m_tblTables = makeTable({tr("Table"), tr("Temperature"), tr("Value")});

    m_colorDelegate = new StackupColorDelegate(this);
    m_tblMats->setItemDelegateForColumn(kColorCol, m_colorDelegate);
    m_tblMats->setIconSize(QSize(14, 14));
    installComboDelegates();

    m_tabs->addTab(m_tblVars, tr("Variables"));
    m_tabs->addTab(m_tblMats, tr("Materials"));
    m_tabs->addTab(m_tblDiels, tr("Dielectrics"));
    m_tabs->addTab(m_tblLayers, tr("Layers"));
    m_tabs->addTab(m_tblDerived, tr("Derived Layers"));
    m_tabs->addTab(m_tblTables, tr("Thermal Tables"));
    root->addWidget(m_tabs, 1);

    auto *rowBtns = new QHBoxLayout;
    auto *btnAdd = new QPushButton(tr("Add row"));
    auto *btnRemove = new QPushButton(tr("Remove row"));
    auto *btnRefresh = new QPushButton(tr("Recompute"));
    auto *btnPickColor = new QPushButton(tr("Pick Color..."));
    btnPickColor->setToolTip(tr("Open color selector for the selected Materials row"));
    connect(btnAdd, &QPushButton::clicked, this, &StackupEditor::onAddRow);
    connect(btnRemove, &QPushButton::clicked, this, &StackupEditor::onRemoveRow);
    connect(btnRefresh, &QPushButton::clicked, this, &StackupEditor::refreshResolvedColumns);
    connect(btnPickColor, &QPushButton::clicked, this, [this]() {
        if (m_tabs->currentWidget() != m_tblMats)
            m_tabs->setCurrentWidget(m_tblMats);
        const int row = m_tblMats->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("Pick Color"),
                                     tr("Select a Materials row first."));
            return;
        }
        onColorCellActivated(m_tblMats->model()->index(row, kColorCol));
    });
    auto *btnImportSubst = new QPushButton(tr("Import ADS…"));
    btnImportSubst->setToolTip(tr("Import ADS Momentum *.subst (+ materials.matdb) or *.ltd"));
    auto *btnExportSubst = new QPushButton(tr("Export ADS…"));
    btnExportSubst->setToolTip(tr("Export current stackup to ADS *.subst + materials.matdb"));
    connect(btnImportSubst, &QPushButton::clicked, this, [this]() {
        QMessageBox box(this);
        box.setWindowTitle(tr("Import ADS"));
        box.setText(tr("Choose ADS Momentum format:"));
        auto *substBtn = box.addButton(tr("*.subst + materials.matdb"), QMessageBox::AcceptRole);
        auto *ltdBtn = box.addButton(tr("*.ltd"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == substBtn)
            onImportAdsSubst();
        else if (box.clickedButton() == ltdBtn)
            onImportAdsLtd();
    });
    connect(btnExportSubst, &QPushButton::clicked, this, &StackupEditor::onExportAdsSubst);

    rowBtns->addWidget(btnAdd);
    rowBtns->addWidget(btnRemove);
    rowBtns->addWidget(btnRefresh);
    rowBtns->addWidget(btnPickColor);
    rowBtns->addStretch();
    rowBtns->addWidget(btnImportSubst);
    rowBtns->addWidget(btnExportSubst);
    root->addLayout(rowBtns);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    auto *btnSaveAs = m_buttons->addButton(tr("Save As..."), QDialogButtonBox::ActionRole);
    connect(btnSaveAs, &QPushButton::clicked, this, &StackupEditor::onSaveAs);
    connect(m_buttons->button(QDialogButtonBox::Save), &QPushButton::clicked,
            this, &StackupEditor::onSave);
    connect(m_buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &StackupEditor::close);
    m_buttons->button(QDialogButtonBox::Save)->setDefault(true);
    root->addWidget(m_buttons);

    connect(m_edDescription, &QPlainTextEdit::textChanged, this, &StackupEditor::markModified);
    connectTableModified(m_tblVars, this);
    connectTableModified(m_tblMats, this);
    connectTableModified(m_tblDiels, this);
    connectTableModified(m_tblLayers, this);
    connectTableModified(m_tblDerived, this);
    connectTableModified(m_tblTables, this);

    connect(m_tblMats, &QTableWidget::cellChanged, this, &StackupEditor::onMaterialsCellChanged);
    connect(m_tblVars, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (m_blockChangeSignals)
            return;
        if (column == 1 || column == 3) {
            m_blockChangeSignals = true;
            styleVariableColorCells(row);
            m_blockChangeSignals = false;
        }
    });

    applyUniformFonts();
}

void StackupEditor::setFilePath(const QString &path)
{
    m_filePath = path;
    updateWindowTitle();
}

void StackupEditor::updateWindowTitle()
{
    QString title = tr("Stackup Editor");
    if (!m_filePath.isEmpty())
        title += QStringLiteral(" — %1").arg(QFileInfo(m_filePath).fileName());
    if (m_modified)
        title += QStringLiteral(" *");
    setWindowTitle(title);
}

void StackupEditor::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    updateWindowTitle();
}

void StackupEditor::markModified()
{
    if (m_blockChangeSignals)
        return;
    setModified(true);
}

void StackupEditor::setSubstrate(const Substrate &substrate)
{
    m_substrate = substrate;
    m_blockChangeSignals = true;
    m_edDescription->setPlainText(m_substrate.description());
    rebuildTablesFromModel();
    m_lblSchema->setText(tr("schema %1").arg(m_substrate.computeMinimumSchemaVersion()));
    m_blockChangeSignals = false;
    setModified(false);
}

Substrate StackupEditor::substrate() const
{
    return m_substrate;
}

void StackupEditor::applyUniformFonts()
{
    // Intentionally empty: widgets inherit the HiDPI-normalized application
    // font from main(). Forcing QApplication::font() onto each control can
    // lock a pixel size and leave the UI at ~100% while the native menu
    // follows Windows display scaling (e.g. 150% on 4K).
}

QStringList StackupEditor::materialNames() const
{
    QStringList names;
    if (m_tblMats) {
        for (int r = 0; r < m_tblMats->rowCount(); ++r) {
            const QString n = itemText(m_tblMats, r, 0);
            if (!n.isEmpty())
                names << n;
        }
    }
    if (names.isEmpty()) {
        for (const Material &m : m_substrate.materials()) {
            if (!m.name().isEmpty())
                names << m.name();
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList StackupEditor::dielectricNames() const
{
    QStringList names;
    if (m_tblDiels) {
        for (int r = 0; r < m_tblDiels->rowCount(); ++r) {
            const QString n = itemText(m_tblDiels, r, 0);
            if (!n.isEmpty())
                names << n;
        }
    }
    if (names.isEmpty()) {
        for (const Dielectric &d : m_substrate.dielectrics()) {
            if (!d.name().isEmpty())
                names << d.name();
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList StackupEditor::layerNames() const
{
    QStringList names;
    if (m_tblLayers) {
        for (int r = 0; r < m_tblLayers->rowCount(); ++r) {
            const QString n = itemText(m_tblLayers, r, 0);
            if (!n.isEmpty())
                names << n;
        }
    }
    if (names.isEmpty()) {
        for (const Layer &l : m_substrate.layers()) {
            if (!l.name().isEmpty())
                names << l.name();
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

QStringList StackupEditor::referenceCandidates() const
{
    QStringList names = dielectricNames();
    names += layerNames();
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void StackupEditor::installComboDelegates()
{
    auto fixed = [this](const QStringList &items, bool editable = false, bool allowEmpty = true) {
        return new StackupComboDelegate([items]() { return items; }, this, editable, allowEmpty);
    };

    // Variables.Type
    m_tblVars->setItemDelegateForColumn(
        2, fixed({QStringLiteral("number"), QStringLiteral("string")}, false, true));

    // Materials.Type — EM material class (NOT layer geometry role).
    // In Volker/IHP XML, via metals are still Material Type="Conductor";
    // the via/conductor distinction lives on Layers.Type.
    auto *matTypeDelegate = fixed(
        {QStringLiteral("conductor"), QStringLiteral("dielectric"),
         QStringLiteral("semiconductor"), QStringLiteral("Conductor"),
         QStringLiteral("Dielectric"), QStringLiteral("Semiconductor")},
        true, false);
    m_tblMats->setItemDelegateForColumn(1, matTypeDelegate);
    if (auto *h = m_tblMats->horizontalHeaderItem(1)) {
        h->setToolTip(tr("Material EM class: Conductor / Dielectric / Semiconductor.\n"
                         "Via metals are still Conductor here — set via/conductor on the Layers tab."));
    }

    // Dielectrics.Material / Reference / Ref.Edge
    m_tblDiels->setItemDelegateForColumn(
        1, new StackupComboDelegate([this]() { return materialNames(); }, this, true, false));
    m_tblDiels->setItemDelegateForColumn(
        3, new StackupComboDelegate([this]() { return referenceCandidates(); }, this, true, true));
    m_tblDiels->setItemDelegateForColumn(
        4, fixed({QStringLiteral("Top"), QStringLiteral("Bottom")}, false, true));

    // Layers.Type / Material / Reference / Ref.Edge
    m_tblLayers->setItemDelegateForColumn(
        1, fixed({QStringLiteral("conductor"), QStringLiteral("via"),
                  QStringLiteral("dielectric")}, true, false));
    if (auto *h = m_tblLayers->horizontalHeaderItem(1)) {
        h->setToolTip(tr("Layer geometry role: conductor / via / dielectric.\n"
                         "This is where vias are marked — not in Materials."));
    }
    m_tblLayers->setItemDelegateForColumn(
        2, new StackupComboDelegate([this]() { return materialNames(); }, this, true, false));
    m_tblLayers->setItemDelegateForColumn(
        6, new StackupComboDelegate([this]() { return referenceCandidates(); }, this, true, true));
    m_tblLayers->setItemDelegateForColumn(
        7, fixed({QStringLiteral("Top"), QStringLiteral("Bottom")}, false, true));

    // Derived.Operation
    m_tblDerived->setItemDelegateForColumn(
        2, fixed({QStringLiteral("AND"), QStringLiteral("OR"), QStringLiteral("XOR"),
                  QStringLiteral("NOT"), QStringLiteral("SIZE")}, false, false));

    // Single-click opens combo editors only (not while rebuilding rows)
    auto openComboOnClick = [](QTableWidget *table, const QSet<int> &comboCols) {
        QObject::connect(table, &QTableWidget::clicked, table,
                         [table, comboCols](const QModelIndex &idx) {
                             if (!idx.isValid() || !comboCols.contains(idx.column()))
                                 return;
                             table->edit(idx);
                         });
    };
    openComboOnClick(m_tblVars, {2});
    openComboOnClick(m_tblMats, {1});
    openComboOnClick(m_tblDiels, {1, 3, 4});
    openComboOnClick(m_tblLayers, {1, 2, 6, 7});
    openComboOnClick(m_tblDerived, {2});
}

QTableWidget *StackupEditor::currentTable() const
{
    return qobject_cast<QTableWidget *>(m_tabs->currentWidget());
}

void StackupEditor::styleColorCell(int row, const QString &raw)
{
    auto *it = m_tblMats->item(row, kColorCol);
    if (!it)
        return;

    const QColor c = StackupColorDelegate::parseColor(raw);
    it->setText(raw.trimmed());
    if (c.isValid() && !StackupColorDelegate::isExpression(raw)) {
        it->setIcon(StackupColorDelegate::colorIcon(c));
        const double luma = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
        it->setForeground(luma > 0.55 ? QBrush(QColor(20, 20, 20)) : QBrush(QColor(250, 250, 250)));
        it->setBackground(c);
    } else {
        it->setIcon(StackupColorDelegate::colorIcon(QColor()));
        it->setForeground(palette().brush(QPalette::Text));
        it->setBackground(QBrush());
        if (StackupColorDelegate::isExpression(raw))
            it->setToolTip(tr("Expression color — edit as text, or replace with a hex value"));
    }
}

void StackupEditor::styleMaterialNameCell(int row, const QString &colorRaw)
{
    auto *it = m_tblMats->item(row, kNameCol);
    if (!it)
        return;
    const QColor c = StackupColorDelegate::parseColor(colorRaw);
    it->setIcon(StackupColorDelegate::colorIcon(c));
}

void StackupEditor::styleVariableColorCells(int row)
{
    auto paintIfHex = [this](QTableWidgetItem *it) {
        if (!it)
            return;
        const QString raw = it->text().trimmed();
        const QColor c = StackupColorDelegate::parseColor(raw);
        if (c.isValid() && !StackupColorDelegate::isExpression(raw)) {
            it->setForeground(QBrush(c));
            it->setToolTip(tr("Color value #%1").arg(c.name(QColor::HexRgb).mid(1)));
        } else {
            it->setData(Qt::ForegroundRole, QVariant());
            it->setToolTip(QString());
        }
    };
    paintIfHex(m_tblVars->item(row, 1));
    paintIfHex(m_tblVars->item(row, 3));
}

void StackupEditor::onMaterialsCellChanged(int row, int column)
{
    if (m_blockChangeSignals)
        return;
    if (column == kColorCol) {
        m_blockChangeSignals = true;
        styleColorCell(row, itemText(m_tblMats, row, kColorCol));
        styleMaterialNameCell(row, itemText(m_tblMats, row, kColorCol));
        m_blockChangeSignals = false;
    }
    markModified();
}

void StackupEditor::onColorCellActivated(const QModelIndex &index)
{
    if (!index.isValid() || index.column() != kColorCol)
        return;

    const QString raw = index.data(Qt::EditRole).toString().trimmed();
    if (StackupColorDelegate::isExpression(raw)) {
        m_tblMats->edit(index);
        return;
    }

    QColor initial = StackupColorDelegate::parseColor(raw);
    if (!initial.isValid())
        initial = Qt::white;

    const QColor chosen = QColorDialog::getColor(initial, this, tr("Select material color"));
    if (!chosen.isValid())
        return;

    m_blockChangeSignals = true;
    if (auto *it = m_tblMats->item(index.row(), kColorCol))
        it->setText(chosen.name(QColor::HexRgb).mid(1));
    else
        setItem(m_tblMats, index.row(), kColorCol, chosen.name(QColor::HexRgb).mid(1));
    styleColorCell(index.row(), itemText(m_tblMats, index.row(), kColorCol));
    styleMaterialNameCell(index.row(), itemText(m_tblMats, index.row(), kColorCol));
    m_blockChangeSignals = false;
    markModified();
}

void StackupEditor::rebuildTablesFromModel()
{
    m_blockChangeSignals = true;

    const QList<QTableWidget *> tables = {
        m_tblVars, m_tblMats, m_tblDiels, m_tblLayers, m_tblDerived, m_tblTables
    };
    QVector<QAbstractItemView::EditTriggers> savedTriggers;
    savedTriggers.reserve(tables.size());
    for (QTableWidget *t : tables) {
        savedTriggers.append(t->editTriggers());
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->clearSelection();
        t->setCurrentCell(-1, -1);
    }

    m_tblVars->setRowCount(0);
    for (const StackupVariable &v : m_substrate.variables()) {
        const int r = m_tblVars->rowCount();
        m_tblVars->insertRow(r);
        setItem(m_tblVars, r, 0, v.name);
        setItem(m_tblVars, r, 1, v.valueRaw);
        setItem(m_tblVars, r, 2, v.type);
        setItem(m_tblVars, r, 3, v.resolved, true);
        styleVariableColorCells(r);
    }

    m_tblMats->setRowCount(0);
    for (const Material &m : m_substrate.materials()) {
        const int r = m_tblMats->rowCount();
        m_tblMats->insertRow(r);
        setItem(m_tblMats, r, 0, m.name());
        setItem(m_tblMats, r, 1, m.type());
        setItem(m_tblMats, r, 2, m.permittivityRaw().isEmpty() && m.permittivity() != 0.0
                                      ? QString::number(m.permittivity())
                                      : m.permittivityRaw());
        setItem(m_tblMats, r, 3, m.lossTangentRaw().isEmpty() && m.lossTangent() != 0.0
                                      ? QString::number(m.lossTangent())
                                      : m.lossTangentRaw());
        setItem(m_tblMats, r, 4, m.conductivityRaw().isEmpty() && m.conductivity() != 0.0
                                      ? QString::number(m.conductivity())
                                      : m.conductivityRaw());
        setItem(m_tblMats, r, 5, m.rsRaw());
        setItem(m_tblMats, r, 6, m.thermalConductivityRaw());
        setItem(m_tblMats, r, 7, m.thermalConductivityTable());
        setItem(m_tblMats, r, 8, m.colorHex());
        styleColorCell(r, m.colorHex());
        styleMaterialNameCell(r, m.colorHex());
    }

    m_tblDiels->setRowCount(0);
    for (const Dielectric &d : m_substrate.dielectrics()) {
        const int r = m_tblDiels->rowCount();
        m_tblDiels->insertRow(r);
        setItem(m_tblDiels, r, 0, d.name());
        setItem(m_tblDiels, r, 1, d.material());
        setItem(m_tblDiels, r, 2, d.thicknessRaw().isEmpty()
                                      ? QString::number(d.thickness(), 'f', 4)
                                      : d.thicknessRaw());
        setItem(m_tblDiels, r, 3, d.reference());
        setItem(m_tblDiels, r, 4, d.referenceEdge());
        setItem(m_tblDiels, r, 5, QString::number(d.resolvedZmin(), 'f', 4), true);
        setItem(m_tblDiels, r, 6, QString::number(d.resolvedZmax(), 'f', 4), true);

        // Color icon on dielectric name from its material
        QColor c;
        for (const Material &mat : m_substrate.materials()) {
            if (mat.name() == d.material()) {
                c = mat.color();
                break;
            }
        }
        if (auto *it = m_tblDiels->item(r, 0))
            it->setIcon(StackupColorDelegate::colorIcon(c));
    }

    m_tblLayers->setRowCount(0);
    for (const Layer &l : m_substrate.layers()) {
        const int r = m_tblLayers->rowCount();
        m_tblLayers->insertRow(r);
        setItem(m_tblLayers, r, 0, l.name());
        setItem(m_tblLayers, r, 1, l.type());
        setItem(m_tblLayers, r, 2, l.material());
        setItem(m_tblLayers, r, 3, QString::number(l.layerNumber()));
        setItem(m_tblLayers, r, 4, l.zminRaw().isEmpty() ? QString::number(l.zmin(), 'f', 4)
                                                         : l.zminRaw());
        setItem(m_tblLayers, r, 5, l.zmaxRaw().isEmpty() ? QString::number(l.zmax(), 'f', 4)
                                                         : l.zmaxRaw());
        setItem(m_tblLayers, r, 6, l.reference());
        setItem(m_tblLayers, r, 7, l.referenceEdge());
        setItem(m_tblLayers, r, 8, QString::number(l.zmin(), 'f', 4), true);
        setItem(m_tblLayers, r, 9, QString::number(l.zmax(), 'f', 4), true);

        QColor c;
        for (const Material &mat : m_substrate.materials()) {
            if (mat.name() == l.material()) {
                c = mat.color();
                break;
            }
        }
        if (auto *it = m_tblLayers->item(r, 0))
            it->setIcon(StackupColorDelegate::colorIcon(c));
    }

    m_tblDerived->setRowCount(0);
    for (const DerivedLayer &d : m_substrate.derivedLayers()) {
        const int r = m_tblDerived->rowCount();
        m_tblDerived->insertRow(r);
        setItem(m_tblDerived, r, 0, d.name);
        setItem(m_tblDerived, r, 1, QString::number(d.layerNumber));
        setItem(m_tblDerived, r, 2, d.operation);
        QStringList ops;
        for (int o : d.operands)
            ops << QString::number(o);
        setItem(m_tblDerived, r, 3, ops.join(QLatin1Char(',')));
    }

    m_tblTables->setRowCount(0);
    for (const ThermalTable &t : m_substrate.thermalTables()) {
        for (const ThermalTablePoint &p : t.points) {
            const int r = m_tblTables->rowCount();
            m_tblTables->insertRow(r);
            setItem(m_tblTables, r, 0, t.name);
            setItem(m_tblTables, r, 1, p.temperatureRaw);
            setItem(m_tblTables, r, 2, p.valueRaw);
        }
    }

    for (int i = 0; i < tables.size(); ++i)
        tables[i]->setEditTriggers(savedTriggers[i]);

    m_blockChangeSignals = false;
}

void StackupEditor::collectModelFromTables()
{
    m_substrate.setDescription(m_edDescription->toPlainText().trimmed());

    m_substrate.variables().clear();
    for (int r = 0; r < m_tblVars->rowCount(); ++r) {
        StackupVariable v;
        v.name = itemText(m_tblVars, r, 0);
        v.valueRaw = itemText(m_tblVars, r, 1);
        v.type = itemText(m_tblVars, r, 2);
        if (!v.name.isEmpty())
            m_substrate.variables() << v;
    }

    m_substrate.materials().clear();
    for (int r = 0; r < m_tblMats->rowCount(); ++r) {
        Material m;
        m.setName(itemText(m_tblMats, r, 0));
        m.setType(itemText(m_tblMats, r, 1));
        m.setPermittivityRaw(itemText(m_tblMats, r, 2));
        m.setLossTangentRaw(itemText(m_tblMats, r, 3));
        m.setConductivityRaw(itemText(m_tblMats, r, 4));
        m.setRsRaw(itemText(m_tblMats, r, 5));
        m.setThermalConductivityRaw(itemText(m_tblMats, r, 6));
        m.setThermalConductivityTable(itemText(m_tblMats, r, 7));
        m.setColorHex(itemText(m_tblMats, r, 8));
        if (!m.name().isEmpty())
            m_substrate.materials() << m;
    }

    m_substrate.dielectrics().clear();
    for (int r = 0; r < m_tblDiels->rowCount(); ++r) {
        Dielectric d;
        d.setName(itemText(m_tblDiels, r, 0));
        d.setMaterial(itemText(m_tblDiels, r, 1));
        d.setThicknessRaw(itemText(m_tblDiels, r, 2));
        d.setReference(itemText(m_tblDiels, r, 3));
        d.setReferenceEdge(itemText(m_tblDiels, r, 4));
        if (!d.name().isEmpty())
            m_substrate.dielectrics() << d;
    }

    m_substrate.layers().clear();
    for (int r = 0; r < m_tblLayers->rowCount(); ++r) {
        Layer l;
        l.setName(itemText(m_tblLayers, r, 0));
        l.setType(itemText(m_tblLayers, r, 1));
        l.setMaterial(itemText(m_tblLayers, r, 2));
        l.setLayerNumber(itemText(m_tblLayers, r, 3).toInt());
        l.setZminRaw(itemText(m_tblLayers, r, 4));
        l.setZmaxRaw(itemText(m_tblLayers, r, 5));
        l.setReference(itemText(m_tblLayers, r, 6));
        l.setReferenceEdge(itemText(m_tblLayers, r, 7));
        if (!l.name().isEmpty())
            m_substrate.layers() << l;
    }

    m_substrate.derivedLayers().clear();
    for (int r = 0; r < m_tblDerived->rowCount(); ++r) {
        DerivedLayer d;
        d.name = itemText(m_tblDerived, r, 0);
        d.layerNumber = itemText(m_tblDerived, r, 1).toInt();
        d.operation = itemText(m_tblDerived, r, 2).toUpper();
        const QStringList ops = itemText(m_tblDerived, r, 3).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &o : ops)
            d.operands << o.trimmed().toInt();
        if (!d.name.isEmpty())
            m_substrate.derivedLayers() << d;
    }

    m_substrate.thermalTables().clear();
    QHash<QString, int> tableIndex;
    for (int r = 0; r < m_tblTables->rowCount(); ++r) {
        const QString tname = itemText(m_tblTables, r, 0);
        if (tname.isEmpty())
            continue;
        if (!tableIndex.contains(tname)) {
            ThermalTable t;
            t.name = tname;
            tableIndex.insert(tname, m_substrate.thermalTables().size());
            m_substrate.thermalTables() << t;
        }
        ThermalTablePoint p;
        p.temperatureRaw = itemText(m_tblTables, r, 1);
        p.valueRaw = itemText(m_tblTables, r, 2);
        m_substrate.thermalTables()[tableIndex.value(tname)].points << p;
    }

    QString err;
    m_substrate.resolve({}, &err);
    m_substrate.setSchemaVersion(m_substrate.computeMinimumSchemaVersion());
    m_lblSchema->setText(tr("schema %1").arg(m_substrate.schemaVersion()));
    if (!err.isEmpty())
        QMessageBox::warning(this, tr("Resolve"), err);
}

void StackupEditor::refreshResolvedColumns()
{
    collectModelFromTables();
    rebuildTablesFromModel();
    markModified();
}

void StackupEditor::onAddRow()
{
    if (auto *t = currentTable()) {
        t->insertRow(t->rowCount());
        markModified();
    }
}

void StackupEditor::onRemoveRow()
{
    if (auto *t = currentTable()) {
        const int r = t->currentRow();
        if (r >= 0) {
            t->removeRow(r);
            markModified();
        }
    }
}

bool StackupEditor::saveToPath(const QString &path)
{
    collectModelFromTables();
    if (!m_substrate.writeXmlFile(path)) {
        QMessageBox::critical(this, tr("Save"), tr("Failed to write:\n%1").arg(path));
        return false;
    }
    m_filePath = path;
    setModified(false);
    updateWindowTitle();
    emit stackupSaved(path);
    return true;
}

void StackupEditor::onSave()
{
    if (m_filePath.isEmpty()) {
        onSaveAs();
        return;
    }
    saveToPath(m_filePath);
}

void StackupEditor::onSaveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Stackup XML"), m_filePath, tr("XML files (*.xml)"));
    if (path.isEmpty())
        return;
    saveToPath(path);
}

bool StackupEditor::maybeSave()
{
    if (!m_modified)
        return true;

    const auto ret = QMessageBox::warning(
        this, tr("Unsaved changes"),
        tr("The stackup has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (ret == QMessageBox::Save) {
        onSave();
        return !m_modified;
    }
    if (ret == QMessageBox::Cancel)
        return false;
    return true;
}

void StackupEditor::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void StackupEditor::selectStackItem(const QString &name, const QString &kind)
{
    if (name.isEmpty())
        return;

    const QString k = kind.trimmed().toLower();
    QTableWidget *preferred = nullptr;
    if (k == QLatin1String("dielectric"))
        preferred = m_tblDiels;
    else
        preferred = m_tblLayers;

    auto selectIn = [this](QTableWidget *table, const QString &n) -> bool {
        if (!table)
            return false;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (itemText(table, r, 0).compare(n, Qt::CaseInsensitive) == 0) {
                m_tabs->setCurrentWidget(table);
                table->setCurrentCell(r, 0);
                table->selectRow(r);
                if (auto *it = table->item(r, 0))
                    table->scrollToItem(it, QAbstractItemView::PositionAtCenter);
                table->setFocus(Qt::OtherFocusReason);
                return true;
            }
        }
        return false;
    };

    if (selectIn(preferred, name))
        return;
    // Fallback: name might live on the other tab (e.g. dielectric-type layer)
    if (preferred != m_tblLayers && selectIn(m_tblLayers, name))
        return;
    if (preferred != m_tblDiels && selectIn(m_tblDiels, name))
        return;
    if (selectIn(m_tblMats, name))
        return;
}

void StackupEditor::clearTableSelection()
{
    const QList<QTableWidget *> tables = {
        m_tblVars, m_tblMats, m_tblDiels, m_tblLayers, m_tblDerived, m_tblTables
    };
    for (QTableWidget *t : tables) {
        if (!t)
            continue;
        t->clearSelection();
        t->setCurrentCell(-1, -1);
    }
}

void StackupEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        clearTableSelection();
        emit clearStackHighlightRequested();
        event->accept();
        return; // do not close non-modal editor on Esc
    }
    QDialog::keyPressEvent(event);
}

QString StackupEditor::resolveAdsConvertScript() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString nearExe = QDir(appDir).filePath(QStringLiteral("scripts/ads/ads_convert.py"));
    if (QFileInfo::exists(nearExe))
        return nearExe;

    // Shadow/build trees: walk up looking for scripts/ads/ads_convert.py
    QDir d(appDir);
    for (int i = 0; i < 6; ++i) {
        const QString p = d.filePath(QStringLiteral("scripts/ads/ads_convert.py"));
        if (QFileInfo::exists(p))
            return p;
        if (!d.cdUp())
            break;
    }
    return nearExe;
}

QString StackupEditor::resolveHostPython() const
{
    QSettings settings(QStringLiteral("EMStudio"), QStringLiteral("EMStudioApp"));
    settings.beginGroup(QStringLiteral("Preferences"));
    const QString elmerPy = settings.value(QStringLiteral("ELMER_PYTHON")).toString().trimmed();
    settings.endGroup();

    auto usable = [](const QString &p) {
        return !p.isEmpty() && QFileInfo::exists(p);
    };

    if (usable(elmerPy) && !elmerPy.startsWith(QLatin1Char('/')))
        return elmerPy;

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (usable(fromPath))
        return fromPath;

    const QString pyLauncher = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (usable(pyLauncher))
        return pyLauncher;

    return QStringLiteral("python");
}

bool StackupEditor::runAdsConvert(const QStringList &args, QString *stdoutText, QString *stderrText)
{
    const QString script = resolveAdsConvertScript();
    if (!QFileInfo::exists(script)) {
        QMessageBox::critical(this, tr("ADS convert"),
                              tr("ads_convert.py not found:\n%1\n\n"
                                 "Ensure scripts/ads/ is deployed next to EMStudio.")
                                  .arg(script));
        return false;
    }

    QString python = resolveHostPython();
    QStringList fullArgs;
    if (QFileInfo(python).fileName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0
        || QFileInfo(python).fileName().compare(QStringLiteral("py.exe"), Qt::CaseInsensitive) == 0) {
        fullArgs << QStringLiteral("-3");
    }
    fullArgs << script << args;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(python, fullArgs);
    if (!proc.waitForStarted(8000)) {
        QMessageBox::critical(this, tr("ADS convert"),
                              tr("Failed to start Python:\n%1").arg(python));
        return false;
    }
    if (!proc.waitForFinished(120000)) {
        proc.kill();
        QMessageBox::critical(this, tr("ADS convert"), tr("Conversion timed out."));
        return false;
    }

    if (stdoutText)
        *stdoutText = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (stderrText)
        *stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        QMessageBox::critical(
            this, tr("ADS convert failed"),
            tr("Command:\n%1 %2\n\n%3")
                .arg(python, fullArgs.join(QLatin1Char(' ')),
                     stderrText ? *stderrText : QString()));
        return false;
    }
    return true;
}

bool StackupEditor::loadImportedXml(const QString &xmlPath, const QString &sourceLabel)
{
    Substrate imported;
    if (!imported.parseXmlFile(xmlPath)) {
        QMessageBox::critical(this, tr("Import failed"),
                              tr("Could not parse imported XML:\n%1").arg(xmlPath));
        return false;
    }

    m_substrate = imported;
    m_filePath.clear(); // force Save As — never overwrite silently
    m_blockChangeSignals = true;
    m_edDescription->setPlainText(m_substrate.description().isEmpty()
                                 ? tr("Imported from %1").arg(sourceLabel)
                                 : m_substrate.description());
    rebuildTablesFromModel();
    m_lblSchema->setText(tr("schema %1").arg(m_substrate.computeMinimumSchemaVersion()));
    m_blockChangeSignals = false;
    setModified(true);
    setWindowTitle(tr("Stackup Editor — imported from %1 *").arg(sourceLabel));
    return true;
}

void StackupEditor::onImportAdsSubst()
{
    if (!maybeSave())
        return;

    const QString subst = QFileDialog::getOpenFileName(
        this, tr("Import ADS Momentum Substrate"),
        QFileInfo(m_filePath).absolutePath(),
        tr("ADS substrate (*.subst);;All files (*.*)"));
    if (subst.isEmpty())
        return;

    const QString matdb = QDir(QFileInfo(subst).absolutePath()).filePath(QStringLiteral("materials.matdb"));
    if (!QFileInfo::exists(matdb)) {
        QMessageBox::critical(
            this, tr("Import"),
            tr("Could not find materials.matdb next to %1.\n\n"
               "A *.subst file always requires materials.matdb in the same folder.")
                .arg(QFileInfo(subst).fileName()));
        return;
    }

    bool ok = false;
    const double air = QInputDialog::getDouble(
        this, tr("Top Air Thickness"),
        tr("Thickness of the open-boundary AIR region above the stack (um):"),
        300.0, 0.0, 1e6, 2, &ok);
    if (!ok)
        return;

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QMessageBox::critical(this, tr("Import"), tr("Could not create temporary directory."));
        return;
    }
    const QString outXml = QDir(tmp.path()).filePath(QStringLiteral("imported.xml"));

    QString out, err;
    if (!runAdsConvert(QStringList()
                           << QStringLiteral("import-subst") << subst << outXml
                           << QStringLiteral("--air") << QString::number(air),
                       &out, &err))
        return;

    if (!loadImportedXml(outXml, QFileInfo(subst).fileName()))
        return;

    if (!err.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Import warnings"),
                                 tr("Imported with warnings:\n\n%1").arg(err));
    } else {
        QMessageBox::information(this, tr("Import complete"),
                                 tr("Stackup imported successfully.\nUse Save As… to store the XML."));
    }
}

void StackupEditor::onImportAdsLtd()
{
    if (!maybeSave())
        return;

    const QString ltd = QFileDialog::getOpenFileName(
        this, tr("Import ADS Momentum Technology"),
        QFileInfo(m_filePath).absolutePath(),
        tr("ADS technology (*.ltd);;All files (*.*)"));
    if (ltd.isEmpty())
        return;

    bool ok = false;
    const double air = QInputDialog::getDouble(
        this, tr("Top Air Thickness"),
        tr("Thickness of the open-boundary AIR region above the stack (um):"),
        300.0, 0.0, 1e6, 2, &ok);
    if (!ok)
        return;

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QMessageBox::critical(this, tr("Import"), tr("Could not create temporary directory."));
        return;
    }
    const QString outXml = QDir(tmp.path()).filePath(QStringLiteral("imported.xml"));

    QString out, err;
    if (!runAdsConvert(QStringList()
                           << QStringLiteral("import-ltd") << ltd << outXml
                           << QStringLiteral("--air") << QString::number(air),
                       &out, &err))
        return;

    if (!loadImportedXml(outXml, QFileInfo(ltd).fileName()))
        return;

    if (!err.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Import warnings"),
                                 tr("Imported with warnings:\n\n%1").arg(err));
    } else {
        QMessageBox::information(this, tr("Import complete"),
                                 tr("Stackup imported successfully.\nUse Save As… to store the XML."));
    }
}

void StackupEditor::onExportAdsSubst()
{
    collectModelFromTables();

    QString start = m_filePath;
    if (start.isEmpty())
        start = QDir::homePath() + QStringLiteral("/stackup.subst");
    else
        start = QFileInfo(m_filePath).absolutePath() + QStringLiteral("/")
                + QFileInfo(m_filePath).completeBaseName() + QStringLiteral(".subst");

    const QString subst = QFileDialog::getSaveFileName(
        this, tr("Export ADS Momentum Substrate"), start,
        tr("ADS substrate (*.subst)"));
    if (subst.isEmpty())
        return;

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QMessageBox::critical(this, tr("Export"), tr("Could not create temporary directory."));
        return;
    }
    const QString tmpXml = QDir(tmp.path()).filePath(QStringLiteral("export.xml"));
    if (!m_substrate.writeXmlFile(tmpXml)) {
        QMessageBox::critical(this, tr("Export"), tr("Failed to write temporary XML."));
        return;
    }

    QString out, err;
    if (!runAdsConvert(QStringList()
                           << QStringLiteral("export-subst") << tmpXml << subst,
                       &out, &err))
        return;

    QMessageBox::information(
        this, tr("Export complete"),
        tr("Wrote:\n%1\n\n%2")
            .arg(out.isEmpty() ? subst : out,
                 err.isEmpty()
                     ? tr("Open in ADS and verify the stackup.")
                     : err));
}
