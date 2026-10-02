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

#include "sidebysidediff.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>

#include "pythonsyntaxhighlighter.h"

namespace {

/*! Line-number gutter of a DiffPane; painting is done by the pane. */
class DiffGutter : public QWidget
{
public:
    explicit DiffGutter(DiffPane *pane) : QWidget(pane), m_pane(pane) {}
    QSize sizeHint() const override { return QSize(m_pane->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { m_pane->paintGutter(event); }

private:
    DiffPane *m_pane;
};

QVector<QPair<int, int>> spansFromJson(const QJsonValue &v)
{
    QVector<QPair<int, int>> out;
    for (const QJsonValue &s : v.toArray()) {
        const QJsonArray a = s.toArray();
        if (a.size() == 2)
            out.append({a.at(0).toInt(), a.at(1).toInt()});
    }
    return out;
}

} // namespace

/*!*******************************************************************************************************************
 * \brief Creates a read-only, non-wrapping Python pane with a monospace font and a line-number gutter.
 * \param parent Parent widget.
 **********************************************************************************************************************/
DiffPane::DiffPane(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new PythonSyntaxHighlighter(document());
    m_gutter = new DiffGutter(this);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy)
            m_gutter->scroll(0, dy);
        else
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

/*!*******************************************************************************************************************
 * \brief Shows one text block per row.
 * \param rowTexts    Row texts (filler rows are empty).
 * \param lineNumbers Source line number of each row, 0 for filler rows.
 **********************************************************************************************************************/
void DiffPane::setRows(const QStringList &rowTexts, const QVector<int> &lineNumbers)
{
    m_lineNumbers = lineNumbers;
    setPlainText(rowTexts.join(QLatin1Char('\n')));
    setViewportMargins(gutterWidth(), 0, 0, 0);
    m_gutter->update();
}

/*!*******************************************************************************************************************
 * \brief Source line number shown for a row.
 * \param row Row (text block) index.
 * \return 1-based line number, or 0 for a filler row.
 **********************************************************************************************************************/
int DiffPane::lineNumberOfRow(int row) const
{
    return (row >= 0 && row < m_lineNumbers.size()) ? m_lineNumbers.at(row) : 0;
}

/*!*******************************************************************************************************************
 * \brief Gutter width for the largest line number.
 * \return Width in pixels.
 **********************************************************************************************************************/
int DiffPane::gutterWidth() const
{
    int maxLine = 1;
    for (int n : m_lineNumbers)
        maxLine = qMax(maxLine, n);
    return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * QString::number(maxLine).size();
}

/*!*******************************************************************************************************************
 * \brief Keeps the gutter next to the text area.
 * \param event Resize event.
 **********************************************************************************************************************/
void DiffPane::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

/*!*******************************************************************************************************************
 * \brief Paints the line numbers of the visible rows; filler rows stay blank.
 * \param event Paint event of the gutter.
 **********************************************************************************************************************/
void DiffPane::paintGutter(QPaintEvent *event)
{
    QPainter p(m_gutter);
    p.fillRect(event->rect(), palette().color(QPalette::Window));
    p.setPen(palette().color(QPalette::Disabled, QPalette::Text));
    p.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        const int n = lineNumberOfRow(block.blockNumber());
        if (block.isVisible() && n > 0 && top + height >= event->rect().top())
            p.drawText(0, top, m_gutter->width() - 5, height, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(n));
        top += height;
        block = block.next();
    }
}

/*!*******************************************************************************************************************
 * \brief Builds the two panes, their titles and the change navigation; scrolling is synchronized.
 * \param parent Parent widget.
 **********************************************************************************************************************/
SideBySideDiff::SideBySideDiff(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *nav = new QHBoxLayout;
    auto *legend = new QLabel(tr("<span style='background:#f4c7c7'>&nbsp;red&nbsp;</span> lines that change, "
                                 "<span style='background:#c8ebc8'>&nbsp;green&nbsp;</span> their new form "
                                 "(the changed part darker), "
                                 "<span style='background:#dddddd'>&nbsp;grey&nbsp;</span> no matching line "
                                 "on that side"), this);
    nav->addWidget(legend, 1);
    m_position = new QLabel(this);
    m_prev = new QPushButton(tr("◀ Previous change"), this);
    m_next = new QPushButton(tr("Next change ▶"), this);
    m_prev->setObjectName(QStringLiteral("diffPrevChange"));
    m_next->setObjectName(QStringLiteral("diffNextChange"));
    nav->addWidget(m_position);
    nav->addWidget(m_prev);
    nav->addWidget(m_next);
    layout->addLayout(nav);

    auto *split = new QSplitter(Qt::Horizontal, this);
    auto makeSide = [split, this](QLabel **title, DiffPane **pane) {
        auto *box = new QWidget(split);
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        *title = new QLabel(box);
        QFont f = (*title)->font();
        f.setBold(true);
        (*title)->setFont(f);
        *pane = new DiffPane(box);
        v->addWidget(*title);
        v->addWidget(*pane, 1);
        split->addWidget(box);
    };
    makeSide(&m_leftTitle, &m_left);
    makeSide(&m_rightTitle, &m_right);
    m_left->setObjectName(QStringLiteral("diffLeftPane"));
    m_right->setObjectName(QStringLiteral("diffRightPane"));
    layout->addWidget(split, 1);

    // Both panes have the same rows, so equal scroll values show the same rows.
    for (auto pair : {qMakePair(m_left, m_right), qMakePair(m_right, m_left)}) {
        DiffPane *from = pair.first;
        DiffPane *to = pair.second;
        connect(from->verticalScrollBar(), &QScrollBar::valueChanged, to->verticalScrollBar(),
                &QScrollBar::setValue);
        connect(from->horizontalScrollBar(), &QScrollBar::valueChanged, to->horizontalScrollBar(),
                &QScrollBar::setValue);
    }

    connect(m_prev, &QPushButton::clicked, this, [this]() { goToChange(m_current - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { goToChange(m_current + 1); });
    updateNavigation();
}

/*!*******************************************************************************************************************
 * \brief Parses the converter's aligned rows.
 * \param rows JSON array of {"l", "r", "c", "ls", "rs"}.
 * \return Rows.
 **********************************************************************************************************************/
QVector<SideBySideDiff::Row> SideBySideDiff::rowsFromJson(const QJsonArray &rows)
{
    QVector<Row> out;
    out.reserve(rows.size());
    for (const QJsonValue &v : rows) {
        const QJsonObject o = v.toObject();
        Row r;
        r.left = o.value(QStringLiteral("l")).toInt();
        r.right = o.value(QStringLiteral("r")).toInt();
        r.changed = o.value(QStringLiteral("c")).toBool();
        r.leftSpans = spansFromJson(o.value(QStringLiteral("ls")));
        r.rightSpans = spansFromJson(o.value(QStringLiteral("rs")));
        out.append(r);
    }
    return out;
}

/*!*******************************************************************************************************************
 * \brief Shows two texts aligned by \a rows.
 * \param leftLines  Lines of the original.
 * \param rightLines Lines of the changed text.
 * \param rows       Alignment (line numbers per side, 0 = filler).
 * \param leftTitle  Title above the left pane.
 * \param rightTitle Title above the right pane.
 **********************************************************************************************************************/
void SideBySideDiff::setContent(const QStringList &leftLines, const QStringList &rightLines,
                                const QVector<Row> &rows, const QString &leftTitle, const QString &rightTitle)
{
    m_rows = rows;
    m_leftTitle->setText(leftTitle);
    m_rightTitle->setText(rightTitle);

    QStringList lt, rt;
    QVector<int> ln, rn;
    for (const Row &r : rows) {
        lt << ((r.left > 0 && r.left <= leftLines.size()) ? leftLines.at(r.left - 1) : QString());
        rt << ((r.right > 0 && r.right <= rightLines.size()) ? rightLines.at(r.right - 1) : QString());
        ln << r.left;
        rn << r.right;
    }
    m_left->setRows(lt, ln);
    m_right->setRows(rt, rn);
    applyMarks(m_left, true);
    applyMarks(m_right, false);

    m_changeStarts.clear();
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).changed && (i == 0 || !rows.at(i - 1).changed))
            m_changeStarts.append(i);
    m_current = -1;
    updateNavigation();
}

/*!*******************************************************************************************************************
 * \brief Row backgrounds (changed / filler) and the changed character spans of one pane.
 * \param pane Pane to mark.
 * \param left True for the original (red), false for the changed side (green).
 **********************************************************************************************************************/
void SideBySideDiff::applyMarks(DiffPane *pane, bool left)
{
    const QColor lineColor = left ? QColor(220, 40, 40, 55) : QColor(30, 160, 30, 55);
    const QColor spanColor = left ? QColor(220, 40, 40, 120) : QColor(30, 160, 30, 120);
    const QColor fillerColor(128, 128, 128, 50);

    QList<QTextEdit::ExtraSelection> marks;
    QTextBlock block = pane->document()->firstBlock();
    for (int i = 0; i < m_rows.size() && block.isValid(); ++i, block = block.next()) {
        const Row &r = m_rows.at(i);
        const int line = left ? r.left : r.right;
        if (line == 0 || r.changed) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(line == 0 ? fillerColor : lineColor);
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            marks.append(sel);
        }
        for (const auto &span : left ? r.leftSpans : r.rightSpans) {
            const int start = qBound(0, span.first, block.length() - 1);
            const int end = qBound(start, span.second, block.length() - 1);
            if (end <= start)
                continue;
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(spanColor);
            sel.cursor = QTextCursor(block);
            sel.cursor.setPosition(block.position() + start);
            sel.cursor.setPosition(block.position() + end, QTextCursor::KeepAnchor);
            marks.append(sel);
        }
    }
    pane->setExtraSelections(marks);
}

/*!*******************************************************************************************************************
 * \brief Scrolls both panes to a changed block (the other pane follows through the synced scroll bars).
 * \param index Change index, clamped to the valid range.
 **********************************************************************************************************************/
void SideBySideDiff::goToChange(int index)
{
    if (m_changeStarts.isEmpty())
        return;
    m_current = qBound(0, index, m_changeStarts.size() - 1);
    showRow(m_changeStarts.at(m_current));
    updateNavigation();
    emit changeShown(m_current, leftLinesOfChange(m_current));
}

/*!*******************************************************************************************************************
 * \brief Centers both panes on a row and puts the cursors there.
 * \param row Row (text block) index.
 **********************************************************************************************************************/
void SideBySideDiff::showRow(int row)
{
    const QTextBlock block = m_left->document()->findBlockByNumber(row);
    m_left->setTextCursor(QTextCursor(block));
    m_left->centerCursor();
    m_right->setTextCursor(QTextCursor(m_right->document()->findBlockByNumber(row)));
    m_right->verticalScrollBar()->setValue(m_left->verticalScrollBar()->value());
}

/*!*******************************************************************************************************************
 * \brief Original line numbers of a changed block.
 * \param index Change index.
 * \return 1-based original lines (filler rows skipped); empty for an invalid index.
 **********************************************************************************************************************/
QVector<int> SideBySideDiff::leftLinesOfChange(int index) const
{
    QVector<int> lines;
    if (index < 0 || index >= m_changeStarts.size())
        return lines;
    for (int i = m_changeStarts.at(index); i < m_rows.size() && m_rows.at(i).changed; ++i)
        if (m_rows.at(i).left > 0)
            lines.append(m_rows.at(i).left);
    return lines;
}

/*!*******************************************************************************************************************
 * \brief Scrolls to the row of an original line; when it is part of a change, that change becomes current.
 * \param line 1-based original line.
 * \return False when the line is not shown.
 **********************************************************************************************************************/
bool SideBySideDiff::goToLeftLine(int line)
{
    int row = -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).left == line) {
            row = i;
            break;
        }
    if (row < 0)
        return false;
    showRow(row);
    if (m_rows.at(row).changed) {
        for (int c = m_changeStarts.size() - 1; c >= 0; --c)
            if (m_changeStarts.at(c) <= row) {
                m_current = c;
                break;
            }
    }
    updateNavigation();
    return true;
}

/*!*******************************************************************************************************************
 * \brief Enables Previous / Next and shows "Change i of n".
 **********************************************************************************************************************/
void SideBySideDiff::updateNavigation()
{
    const int n = m_changeStarts.size();
    m_prev->setEnabled(n > 0 && m_current > 0);
    m_next->setEnabled(n > 0 && m_current < n - 1);
    m_position->setText(n == 0 ? tr("No changes")
                               : m_current < 0 ? tr("%n change(s)", nullptr, n)
                                               : tr("Change %1 of %2").arg(m_current + 1).arg(n));
}
