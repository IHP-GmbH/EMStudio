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

    while (!stream.atEnd()) {
        quint16 size;
        quint8 recordType, dataType;

        stream >> size >> recordType >> dataType;
        int dataSize = size - 4;

        if (recordType == 0x06 && dataType == 0x06) {
            QByteArray nameData;
            nameData.resize(dataSize);
            stream.readRawData(nameData.data(), dataSize);

            QString cellName = QString::fromLatin1(nameData).trimmed();
            cellNames << cellName;
        } else {
            file.seek(file.pos() + dataSize);
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

    while (!stream.atEnd()) {
        quint16 size;
        quint8 recordType, dataType;

        stream >> size >> recordType >> dataType;

        if (recordType == 0x0D && dataType == 0x02) { // LAYER
            quint16 layer;
            stream >> layer;
            currentLayer = layer;
            file.seek(file.pos() + size - 6);
        } else if (recordType == 0x0E && dataType == 0x02) { // DATATYPE
            quint16 dtype;
            stream >> dtype;
            currentDatatype = dtype;
            layers.insert({currentLayer, currentDatatype});
            file.seek(file.pos() + size - 6);
        } else {
            file.seek(file.pos() + size - 4);
        }
    }

    file.close();
    return layers;
}
