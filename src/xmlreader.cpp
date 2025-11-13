#include <QFile>
#include <QDebug>
#include <QXmlStreamReader>

#include "mainwindow.h"

/*!*******************************************************************************************************************
 * \brief Parses the given XML substrate file and extracts the names of all layers of type "conductor".
 *
 * This function opens the XML file provided via \a xmlFilePath and reads through its contents using QXmlStreamReader.
 * It collects all layer names where the type attribute is "conductor" and returns them in a QStringList.
 *
 * \param xmlFilePath     Path to the XML file that defines the substrate stackup.
 * \return                List of layer names classified as conductors.
 **********************************************************************************************************************/
QStringList MainWindow::readSubstrateLayers(const QString &xmlFilePath)
{
    QStringList layerNames;

    QFile file(xmlFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error(QString("Failed to open substrate XML file: %1").arg(file.errorString()), false);
        return layerNames;
    }

    QXmlStreamReader xml(&file);

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();

        if (xml.isStartElement() && xml.name() == "Layer") {
            QString type = xml.attributes().value("Type").toString();
            QString name = xml.attributes().value("Name").toString();

            if (type.compare("conductor", Qt::CaseInsensitive) == 0) {
                layerNames.append(name);
            }
        }
    }

    if (xml.hasError()) {
        error(QString("XML parsing error: %1").arg(xml.errorString()), false);
    }

    file.close();
    return layerNames;
}

QHash<int, QString> MainWindow::readSubstrateLayerMap(const QString &xmlFilePath)
{
    QHash<int, QString> map;
    QFile file(xmlFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return map;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("Layer")) {
            const auto attrs = xml.attributes();
            const QString name  = attrs.value("Name").toString();
            const QString type  = attrs.value("Type").toString();
            bool ok=false; int layer = attrs.value("Layer").toInt(&ok);

            if (ok && !name.isEmpty() && !map.contains(layer))
                map.insert(layer, name);
        }
    }

    return map;
}


