#ifndef WSLHELPER_H
#define WSLHELPER_H

#include <QString>

QString wslExePath();
bool isWslAvailable();

bool existsPortable(const QString &path,
                    const QString &distro = QString(),
                    int timeoutMs = 2000);

bool isReadablePortable(const QString &path,
                        const QString &distro = QString(),
                        int timeoutMs = 2000);

bool isExecutablePortable(const QString &path,
                          const QString &distro = QString(),
                          int timeoutMs = 2000);

bool isReadableLocalThenWsl(const QString &path,
                            const QString &distro = QString(),
                            int timeoutMs = 2000);

QString toLinuxPathPortable(const QString &path,
                            const QString &distro = QString(),
                            int timeoutMs = 2000);

QString runWslCmdCapture(const QString &distro,
                         const QStringList &cmd,
                         int timeoutMs = 2000);

#endif // WSLHELPER_H
