#ifndef TST_PALACE_GOLDEN_H
#define TST_PALACE_GOLDEN_H

#include <QObject>

class PalaceGolden : public QObject
{
    Q_OBJECT

private slots:
    void defaultPalace_changeSettings_ports_and_compare();
};

#endif // TST_PALACE_GOLDEN_H
