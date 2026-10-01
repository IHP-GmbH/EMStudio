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
#include <QDataStream>
#include <QStringList>

#include "mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Extracts the list of cell names from a GDSII file.
 *
 * This function reads the binary GDSII file format and identifies records with record type 0x06 (STRNAME),
 * which contain the names of the defined cells in the layout. SNAME records (0x12, SREF/AREF targets)
 * mark referenced cells; the others are top-level cells.
 *
 * \param filePath Path to the GDSII file.
 * \param topCells Optional: receives the top-level cells in file order (like gdstk's top_level()).
 * \return A list of extracted cell names.
 **********************************************************************************************************************/
QStringList MainWindow::extractGdsCellNames(const QString &filePath, QStringList *topCells)
{
    QFile file(filePath);
    QStringList cellNames;
    QSet<QString> referenced;
    if (topCells)
        topCells->clear();

    if (!file.open(QIODevice::ReadOnly))
        return cellNames;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);

    while (file.bytesAvailable() >= 4) {

        const qint64 recStartPos = file.pos();

        quint16 size = 0;
        quint8 recordType = 0, dataType = 0;

        stream >> size >> recordType >> dataType;

        if (stream.status() != QDataStream::Ok) {
            break;
        }

        if (size < 4 || (size & 1) != 0) {
            break;
        }

        const qint64 dataSize = qint64(size) - 4;

        if (dataSize > file.bytesAvailable()) {
            break;
        }

        if ((recordType == 0x06 || recordType == 0x12) && dataType == 0x06) { // STRNAME / SNAME
            QByteArray nameData;
            nameData.resize(int(dataSize));
            if (dataSize > 0) {
                const int read = stream.readRawData(nameData.data(), int(dataSize));
                if (read != dataSize || stream.status() != QDataStream::Ok) {
                    break;
                }
            }

            // Names are NUL-padded to an even length.
            QString cellName = QString::fromLatin1(nameData).remove(QChar(0)).trimmed();
            if (cellName.isEmpty())
                continue;
            if (recordType == 0x06)
                cellNames << cellName;
            else
                referenced.insert(cellName);

        } else {
            const qint64 newPos = recStartPos + size;
            if (!file.seek(newPos)) {
                break;
            }
        }
    }

    file.close();
    if (topCells) {
        for (const QString &name : cellNames)
            if (!referenced.contains(name))
                *topCells << name;
    }
    return cellNames;
}


/*!*******************************************************************************************************************
 * \brief Extracts the set of layer and datatype pairs from a GDSII file.
 *
 * This function parses the binary GDSII file and collects all (layer, datatype) pairs by reading
 * LAYER (0x0D) and DATATYPE (0x0E) records. The extracted pairs are stored in a QSet to avoid duplicates.
 *
 * \param filePath Path to the GDSII file.
 * \return A set of unique (layer, datatype) pairs found in the file.
 **********************************************************************************************************************/
QSet<QPair<int, int>> MainWindow::extractGdsLayerNumbers(const QString &filePath)
{
    QFile file(filePath);
    QSet<QPair<int, int>> layers;

    if (!file.open(QIODevice::ReadOnly))
        return layers;

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);

    int currentLayer = -1;
    int currentDatatype = -1;

    while (file.bytesAvailable() >= 4) {

        const qint64 recStart = file.pos();

        quint16 size = 0;
        quint8 recordType = 0, dataType = 0;
        stream >> size >> recordType >> dataType;

        if (stream.status() != QDataStream::Ok)
            break;

        // GDS: size includes header(4) and usually is even
        if (size < 4 || (size & 1) != 0)
            break;

        const qint64 dataSize = qint64(size) - 4;
        if (dataSize > file.bytesAvailable())
            break;

        if (recordType == 0x0D && dataType == 0x02) { // LAYER (2 bytes)
            if (dataSize < 2) break;

            quint16 layer = 0;
            stream >> layer;
            if (stream.status() != QDataStream::Ok) break;

            currentLayer = int(layer);

        } else if (recordType == 0x0E && dataType == 0x02) { // DATATYPE (2 bytes)
            if (dataSize < 2) break;

            quint16 dtype = 0;
            stream >> dtype;
            if (stream.status() != QDataStream::Ok) break;

            currentDatatype = int(dtype);

            if (currentLayer >= 0 && currentDatatype >= 0)
                layers.insert(qMakePair(currentLayer, currentDatatype));
        }

        const qint64 nextPos = recStart + size;
        if (!file.seek(nextPos))
            break;
    }

    file.close();
    return layers;
}
