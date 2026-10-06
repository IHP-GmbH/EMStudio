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


#include <QSet>
#include <QPair>
#include <QFile>
#include <QDebug>
#include <QString>
#include <QFileInfo>
#include <QDateTime>
#include <QStringList>

#include "mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Identifies a version of a file: absolute path, size and modification time.
 *
 * Used to reuse what was read from a GDS file as long as the file is unchanged.
 *
 * \param filePath Path to the file.
 * \return Key, or an empty string when the file doesn't exist.
 **********************************************************************************************************************/
QString MainWindow::gdsFileKey(const QString &filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists())
        return QString();
    return fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size()) + QLatin1Char('|')
            + QString::number(fi.lastModified().toMSecsSinceEpoch());
}

/*!*******************************************************************************************************************
 * \brief Reads the cell names and the (layer, datatype) pairs of a GDSII file in one pass.
 *
 * The file is read into memory at once (record-wise reads are slow on Windows) and its records are walked:
 * STRNAME (0x06) names a cell, SNAME (0x12, SREF/AREF target) marks a referenced cell; cells that are never
 * referenced are top-level cells (in file order, like gdstk's top_level()). LAYER (0x0D) followed by
 * DATATYPE (0x0E) gives a (layer, datatype) pair.
 *
 * \param filePath Path to the GDSII file.
 * \param info     Receives cells, top cells and layers (\c key is left unchanged).
 * \return False if the file can't be read.
 **********************************************************************************************************************/
bool MainWindow::readGdsFileInfo(const QString &filePath, GdsFileInfo *info)
{
    info->cells.clear();
    info->topCells.clear();
    info->layers.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    file.close();

    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    const qint64 n = data.size();
    QSet<QString> referenced;
    int currentLayer = -1;

    for (qint64 pos = 0; pos + 4 <= n; ) {
        const int size = (bytes[pos] << 8) | bytes[pos + 1];
        const uchar recordType = bytes[pos + 2];
        const uchar dataType = bytes[pos + 3];
        // GDS: size includes the 4-byte header and is even
        if (size < 4 || (size & 1) != 0 || pos + size > n)
            break;
        const uchar *payload = bytes + pos + 4;
        const int dataSize = size - 4;

        if ((recordType == 0x06 || recordType == 0x12) && dataType == 0x06) { // STRNAME / SNAME
            // Names are NUL-padded to an even length.
            const QString name = QString::fromLatin1(reinterpret_cast<const char *>(payload), dataSize)
                                         .remove(QChar(0)).trimmed();
            if (!name.isEmpty()) {
                if (recordType == 0x06)
                    info->cells << name;
                else
                    referenced.insert(name);
            }
        } else if (recordType == 0x0D && dataType == 0x02 && dataSize >= 2) { // LAYER
            currentLayer = (payload[0] << 8) | payload[1];
        } else if (recordType == 0x0E && dataType == 0x02 && dataSize >= 2) { // DATATYPE
            if (currentLayer >= 0)
                info->layers.insert(qMakePair(currentLayer, (payload[0] << 8) | payload[1]));
        }
        pos += size;
    }

    for (const QString &name : qAsConst(info->cells))
        if (!referenced.contains(name))
            info->topCells << name;
    return true;
}
