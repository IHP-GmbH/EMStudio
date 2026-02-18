#ifndef WSLHELPER_H
#define WSLHELPER_H

#include <QString>

namespace WslHelper {

bool isWslAvailable();

bool isReadableLocalThenWsl(const QString &path, const QString &distro = QStringLiteral("Ubuntu"), int timeoutMs = 2000);
bool existsPortable(const QString &path, const QString &distro = QStringLiteral("Ubuntu"), int timeoutMs = 2000);
bool isReadablePortable(const QString &path, const QString &distro = QStringLiteral("Ubuntu"), int timeoutMs = 2000);
bool isExecutablePortable(const QString &path, const QString &distro = QStringLiteral("Ubuntu"), int timeoutMs = 2000);

QString toLinuxPathPortable(const QString &path, const QString &distro = QStringLiteral("Ubuntu"), int timeoutMs = 2000);

}

#endif // WSLHELPER_H
