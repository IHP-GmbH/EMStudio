#ifndef TST_PALACE_SIM_E2E_H
#define TST_PALACE_SIM_E2E_H

#include <QObject>

/*!*******************************************************************************************************************
 * \brief Full EMStudio → gds2palace → Palace → Touchstone path (Linux CI with a prebuilt Palace).
 *
 * Skipped unless \c EMSTUDIO_PALACE_E2E=1 and \c PALACE_INSTALL_PATH/bin/palace exists.
 **********************************************************************************************************************/
class PalaceSimE2E : public QObject
{
    Q_OBJECT

private slots:
    void lineViaPort_runsPalace_andMatchesGoldenSparams();
};

#endif // TST_PALACE_SIM_E2E_H
