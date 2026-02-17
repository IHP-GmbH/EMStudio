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
 * which contain the names of the defined cells in the layout.
 *
 * \param filePath Path to the GDSII file.
 * \return A list of extracted cell names.
 **********************************************************************************************************************/
QStringList MainWindow::extractGdsCellNames(const QString &filePath)
{
    QFile file(filePath);
    QStringList cellNames;

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

        if (recordType == 0x06 && dataType == 0x06) { // STRNAME / ASCII
            QByteArray nameData;
            nameData.resize(int(dataSize));
            if (dataSize > 0) {
                const int read = stream.readRawData(nameData.data(), int(dataSize));
                if (read != dataSize || stream.status() != QDataStream::Ok) {
                    break;
                }
            }

            QString cellName = QString::fromLatin1(nameData).trimmed();
            if (!cellName.isEmpty())
                cellNames << cellName;

        } else {
            const qint64 newPos = recStartPos + size;
            if (!file.seek(newPos)) {
                break;
            }
        }
    }

    file.close();
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
