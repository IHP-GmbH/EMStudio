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
 ************************************************************************/

#include "sanitycheck.h"

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)

void appendPortDirectionFindings(QVector<SanityFinding> &out,
                                 int portNumber,
                                 const QString &direction,
                                 const QString &fromLayer,
                                 const QString &toLayer)
{
    const QString dir = direction.trimmed().toLower();
    const QString from = fromLayer.trimmed();
    const QString to = toLayer.trimmed();
    const bool hasFrom = !from.isEmpty();
    const bool hasTo = !to.isEmpty();
    const bool hasBoth = hasFrom && hasTo;
    const bool hasEither = hasFrom || hasTo;
    const QString portLabel = portNumber > 0
            ? QStringLiteral("Port %1").arg(portNumber)
            : QStringLiteral("Port");

    const bool isZ = dir.contains(QLatin1Char('z'));
    const bool isXY = dir.contains(QLatin1Char('x')) || dir.contains(QLatin1Char('y'));

    if (isZ) {
        if (!hasBoth) {
            SanityFinding f;
            f.severity = SanityFinding::Error;
            f.code = QStringLiteral("port_z_needs_from_to");
            f.message = QObject::tr("%1: direction '%2' requires both From and To layers.")
                            .arg(portLabel, dir.isEmpty() ? QStringLiteral("z") : dir);
            out.append(f);
        }
        // z with only one side filled is already an Error above; if somehow
        // only one of from/to conceptually used as target — warn when neither both nor empty-but-single-target pattern
    } else if (isXY) {
        if (!hasEither) {
            SanityFinding f;
            f.severity = SanityFinding::Error;
            f.code = QStringLiteral("port_xy_needs_target");
            f.message = QObject::tr("%1: direction '%2' requires a target layer (From or To).")
                            .arg(portLabel, dir);
            out.append(f);
        } else if (hasBoth) {
            SanityFinding f;
            f.severity = SanityFinding::Warning;
            f.code = QStringLiteral("port_xy_has_from_and_to");
            f.message = QObject::tr("%1: direction '%2' is in-plane; From+To together looks like a via port.")
                            .arg(portLabel, dir);
            out.append(f);
        }
    } else if (dir.isEmpty()) {
        SanityFinding f;
        f.severity = SanityFinding::Warning;
        f.code = QStringLiteral("port_empty_direction");
        f.message = QObject::tr("%1: direction is empty (default is usually z).").arg(portLabel);
        out.append(f);
    }
}

bool showSanityCheckDialog(QWidget *parent, const QVector<SanityFinding> &findings)
{
    if (findings.isEmpty())
        return true;

    int nErr = 0;
    int nWarn = 0;
    for (const SanityFinding &f : findings) {
        if (f.severity == SanityFinding::Error)
            ++nErr;
        else
            ++nWarn;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Pre-Run Sanity Check"));
    dlg.setMinimumWidth(520);
    dlg.setMinimumHeight(320);

    auto *lay = new QVBoxLayout(&dlg);
    auto *summary = new QLabel(
        QObject::tr("%1 error(s), %2 warning(s). Review before starting a long simulation.")
            .arg(nErr)
            .arg(nWarn),
        &dlg);
    summary->setWordWrap(true);
    lay->addWidget(summary);

    auto *list = new QListWidget(&dlg);
    for (const SanityFinding &f : findings) {
        const QString prefix = (f.severity == SanityFinding::Error)
                ? QObject::tr("[Error] ")
                : QObject::tr("[Warning] ");
        auto *item = new QListWidgetItem(prefix + f.message, list);
        if (f.severity == SanityFinding::Error)
            item->setForeground(QColor(176, 0, 0));
        else
            item->setForeground(QColor(160, 110, 0));
    }
    lay->addWidget(list, 1);

    auto *buttons = new QDialogButtonBox(&dlg);
    auto *cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    auto *runBtn = buttons->addButton(QObject::tr("Run anyway"), QDialogButtonBox::AcceptRole);
    runBtn->setDefault(nErr == 0);
    cancelBtn->setDefault(nErr > 0);
    lay->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

#endif // QT_VERSION
