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

#ifndef SIDEBYSIDEDIFF_H
#define SIDEBYSIDEDIFF_H

#include <QJsonArray>
#include <QPair>
#include <QPlainTextEdit>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;

/*!*******************************************************************************************************************
 * \brief Read-only Python pane of the side-by-side view: syntax highlighting and a line-number gutter
 *        that leaves filler rows (no matching line on this side) unnumbered.
 **********************************************************************************************************************/
class DiffPane : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit DiffPane(QWidget *parent = nullptr);

    /*! Text with one block per row and the source line number of each row (0 = filler). */
    void                        setRows(const QStringList &rowTexts, const QVector<int> &lineNumbers);
    int                         lineNumberOfRow(int row) const;
    int                         gutterWidth() const;
    void                        paintGutter(QPaintEvent *event);

protected:
    void                        resizeEvent(QResizeEvent *event) override;

private:
    QWidget                     *m_gutter = nullptr;
    QVector<int>                m_lineNumbers;
};

/*!*******************************************************************************************************************
 * \brief Original and changed Python code side by side, aligned row by row, with synchronized scrolling.
 *
 * Equal lines share a row; where one side has more lines the other shows grey filler rows. Changed
 * lines are tinted (original red, converted green); within lines that are mostly alike only the
 * changed characters are marked strongly. Previous / Next jump between changed blocks.
 **********************************************************************************************************************/
class SideBySideDiff : public QWidget
{
    Q_OBJECT

public:
    /*! One aligned row: source line numbers (1-based, 0 = filler) and changed character spans. */
    struct Row
    {
        int                         left = 0;
        int                         right = 0;
        bool                        changed = false;
        QVector<QPair<int, int>>    leftSpans;   //!< [start, end) in the left line
        QVector<QPair<int, int>>    rightSpans;  //!< [start, end) in the right line
    };

    explicit SideBySideDiff(QWidget *parent = nullptr);

    /*! Rows as written by convert_loose_to_settings.py ({"l", "r", "c", "ls", "rs"}). */
    static QVector<Row>         rowsFromJson(const QJsonArray &rows);

    void                        setContent(const QStringList &leftLines, const QStringList &rightLines,
                                           const QVector<Row> &rows, const QString &leftTitle,
                                           const QString &rightTitle);
    DiffPane                    *leftPane() const { return m_left; }
    DiffPane                    *rightPane() const { return m_right; }
    int                         changeCount() const { return m_changeStarts.size(); }
    int                         currentChange() const { return m_current; }
    void                        goToChange(int index);
    /*! Scrolls to the row of an original line (1-based) and makes its change current, if any. */
    bool                        goToLeftLine(int line);
    /*! Original line numbers of a changed block. */
    QVector<int>                leftLinesOfChange(int index) const;

signals:
    /*! A change was shown by Previous / Next or goToChange(); \a leftLines are its original lines. */
    void                        changeShown(int index, const QVector<int> &leftLines);

private:
    void                        applyMarks(DiffPane *pane, bool left);
    void                        showRow(int row);
    void                        updateNavigation();

    DiffPane                    *m_left = nullptr;
    DiffPane                    *m_right = nullptr;
    QLabel                      *m_leftTitle = nullptr;
    QLabel                      *m_rightTitle = nullptr;
    QLabel                      *m_position = nullptr;
    QPushButton                 *m_prev = nullptr;
    QPushButton                 *m_next = nullptr;
    QVector<Row>                m_rows;
    QVector<int>                m_changeStarts;   //!< First row of each changed block
    int                         m_current = -1;
};

#endif // SIDEBYSIDEDIFF_H
