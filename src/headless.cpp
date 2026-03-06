#include <QFile>
#include <QDebug>
#include <QTimer>

#include "wslHelper.h"
#include "mainwindow.h"
#include "preferences.h"
#include "ui_mainwindow.h"
#include "substrateview.h"
#include "pythonparser.h"
#include "keywordseditor.h"

void MainWindow::runHeadless(const QString& simKeyLower)
{
    m_headless = true;

    const QString key = simKeyLower.trimmed().toLower();
    if (key == QLatin1String("palace")) {
        runPalace(false);
        return;
    }
    if (key == QLatin1String("openems")) {
        runOpenEMS(false);
        return;
    }

    qCritical() << "Unknown backend for headless run:" << simKeyLower;
    QCoreApplication::exit(1);
}
