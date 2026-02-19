#ifndef WSLHELPER_H
#define WSLHELPER_H

#include <QMap>
#include <QString>
#include <QVariant>

QString wslExePath();
bool isWslAvailable();

bool existsPortable(const QString &path,
                    const QString &distro,
                    int timeoutMs);

bool isReadablePortable(const QString &path,
                        const QString,
                        int timeoutMs);

bool isExecutablePortable(const QString &path,
                          const QString &distro,
                          int timeoutMs);

bool isReadableLocalThenWsl(const QString &path,
                            const QString &distro,
                            int timeoutMs);

QString toLinuxPathPortable(const QString &path,
                            const QString &distro,
                            int timeoutMs);

QString runWslCmdCapture(const QString &distro,
                         const QStringList &cmd,
                         int timeoutMs = 2000);

QString decodeWslOutput(const QByteArray &ba);

QString shellQuoteSingle(const QString &s);

QStringList listWslDistrosFromSystem(int timeoutMs = 8000);
void exportWslDistroToEnv(const QMap<QString, QVariant>& prefs);

#endif // WSLHELPER_H
