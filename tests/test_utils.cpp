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

#include "test_utils.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace GoldenTestUtils
{

/*!*******************************************************************************************************************
 * \brief Reads a UTF-8 encoded text file into a QString.
 *
 * Opens the file at \p path in read-only text mode and returns its entire
 * contents decoded as UTF-8. If the file cannot be opened, an empty string
 * is returned.
 *
 * \param path Absolute or relative file path.
 * \return File content as UTF-8 QString, or empty string on failure.
 **********************************************************************************************************************/
QString readUtf8(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    return QString::fromUtf8(f.readAll());
}

/*!*******************************************************************************************************************
 * \brief Normalizes script text for deterministic golden comparison.
 *
 * Performs platform-independent normalization by:
 *  - Converting CRLF to LF,
 *  - Removing trailing whitespace before newline,
 *  - Masking absolute GDS/XML paths (Windows and Linux),
 *  - Normalizing scientific notation exponents (e.g. e+0003 -> e+3).
 *
 * The resulting text is trimmed and always ends with a single newline.
 *
 * \param s Input text to normalize.
 * \return Normalized text suitable for golden comparison.
 **********************************************************************************************************************/
QString normalize(QString s)
{
    s.replace("\r\n", "\n");
    s.replace(QRegularExpression(R"([ \t]+(?=\n))"), "");

    s.replace(QRegularExpression(R"(([A-Za-z]:[\\/][^ \n'"]+\.gds))"), "<GDS_PATH>");
    s.replace(QRegularExpression(R"(([A-Za-z]:[\\/][^ \n'"]+\.xml))"), "<XML_PATH>");
    s.replace(QRegularExpression(R"((/[^ \n'"]+\.gds))"), "<GDS_PATH>");
    s.replace(QRegularExpression(R"((/[^ \n'"]+\.xml))"), "<XML_PATH>");
    s.replace(QRegularExpression(R"(e([+-])0+(\d+))"), "e\\1\\2");

    return s.trimmed() + "\n";
}

/*!*******************************************************************************************************************
 * \brief Produces a contextual diff for two multi-line texts.
 *
 * Compares \p expected and \p actual line-by-line and returns a formatted
 * description of the first mismatch including \p contextLines lines
 * around the difference.
 *
 * If the texts are identical, an empty string is returned.
 *
 * \param expected Expected full text.
 * \param actual   Actual full text.
 * \param contextLines Number of surrounding context lines to include.
 * \return Human-readable diff text for the first mismatch, or empty string if identical.
 **********************************************************************************************************************/
QString diffText(const QString& expected,
                 const QString& actual,
                 int contextLines)
{
    const QStringList exp = expected.split('\n');
    const QStringList act = actual.split('\n');

    const int maxLines = qMax(exp.size(), act.size());

    for (int i = 0; i < maxLines; ++i) {
        const QString e = (i < exp.size()) ? exp[i] : "<EOF>";
        const QString a = (i < act.size()) ? act[i] : "<EOF>";

        if (e != a) {
            QString out;
            out += QString("Difference at line %1:\n").arg(i + 1);

            const int from = qMax(0, i - contextLines);
            const int to   = qMin(maxLines - 1, i + contextLines);

            for (int j = from; j <= to; ++j) {
                const QString ee = (j < exp.size()) ? exp[j] : "<EOF>";
                const QString aa = (j < act.size()) ? act[j] : "<EOF>";

                if (j == i) {
                    out += QString(">> EXPECTED: %1\n").arg(ee);
                    out += QString(">> ACTUAL  : %1\n").arg(aa);
                } else {
                    out += QString("   exp: %1\n").arg(ee);
                    out += QString("   act: %1\n").arg(aa);
                }
            }
            return out;
        }
    }

    return {};
}

/*!*******************************************************************************************************************
 * \brief Atomically writes UTF-8 text into an existing file.
 *
 * The function writes the provided text into a temporary file located next to
 * the destination file and then replaces the original file using rename().
 * This ensures that partially written files are avoided.
 *
 * The destination file must already exist. If it does not exist, the function
 * returns false and does not create a new file.
 *
 * \param path Absolute path to the golden file.
 * \param text UTF-8 text content to write.
 * \param outErr Optional pointer receiving an error message.
 *
 * \return True on success, false on failure.
 **********************************************************************************************************************/
bool writeUtf8Atomic(const QString& path,
                     const QString& text,
                     QString* outErr)
{
    const QFileInfo fi(path);

    if (!fi.exists()) {
        if (outErr)
            *outErr = QString("Golden file does not exist: %1").arg(path);
        return false;
    }

    const QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (outErr)
            *outErr = QString("Directory does not exist: %1")
                          .arg(dir.absolutePath());
        return false;
    }

    const QString tmpPath = fi.absoluteFilePath() + ".tmp";

    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (outErr)
            *outErr = QString("Cannot open temp for write: %1 (%2)")
                          .arg(tmpPath, tmp.errorString());
        return false;
    }

    tmp.write(text.toUtf8());

    if (!tmp.flush()) {
        if (outErr)
            *outErr = QString("Flush failed for: %1 (%2)")
                          .arg(tmpPath, tmp.errorString());
        return false;
    }

    tmp.close();

    QFile::remove(fi.absoluteFilePath());

    if (!QFile::rename(tmpPath, fi.absoluteFilePath())) {
        if (outErr)
            *outErr = QString("Cannot replace golden file: %1")
                          .arg(fi.absoluteFilePath());
        QFile::remove(tmpPath);
        return false;
    }

    return true;
}

/*!*******************************************************************************************************************
 * \brief Updates the golden file with normalized content.
 *
 * Intended for controlled golden updates during development.
 * Asserts in debug mode if the update fails.
 *
 * \param goldenPath Absolute path to golden file.
 * \param normalizedContent Normalized script text to store.
 **********************************************************************************************************************/
void updateGoldenOnce(const QString& goldenPath,
                      const QString& normalizedContent)
{
    QString err;
    const bool ok = writeUtf8Atomic(goldenPath, normalizedContent, &err);
    Q_ASSERT_X(ok, "updateGoldenOnce", qPrintable(err));
}

} // namespace GoldenTestUtils
