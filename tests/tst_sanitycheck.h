/************************************************************************
 *  EMStudio – GUI tool for setting up, running and analysing
 *  electromagnetic simulations with IHP PDKs.
 *
 *  Copyright (C) 2023–2026 IHP Authors
 ************************************************************************/

#ifndef TST_SANITYCHECK_H
#define TST_SANITYCHECK_H

#include <QObject>

class SanityCheckTest : public QObject
{
    Q_OBJECT

private slots:
    void appendPortDirection_zRequiresFromTo();
    void appendPortDirection_xyRules();
    void appendPortDirection_emptyDirection();
    void showDialog_emptyReturnsTrue();
    void showDialog_cancelAndAccept();
};

#endif // TST_SANITYCHECK_H
