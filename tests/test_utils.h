#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <QString>

class QRegularExpression;

namespace GoldenTestUtils
{
QString readUtf8(const QString& path);

QString normalize(QString s);

QString diffText(const QString& expected,
                 const QString& actual,
                 int contextLines = 2);

bool writeUtf8Atomic(const QString& path,
                     const QString& text,
                     QString* outErr = nullptr);

void updateGoldenOnce(const QString& goldenPath,
                      const QString& normalizedContent);
}

#endif // TEST_UTILS_H
