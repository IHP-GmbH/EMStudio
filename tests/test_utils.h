#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <QByteArray>
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

/*! Restores keywords/<name> next to the test binary on scope exit (tests rewrite them). */
class KeywordFileBackup
{
public:
    explicit KeywordFileBackup(const QString &name);
    ~KeywordFileBackup();
    const QString &path() const { return m_path; }
    /*! Replaces the file with \a text (creates the keywords folder if needed). */
    bool write(const QByteArray &text) const;

private:
    QString m_path;
    QByteArray m_data;
    bool m_existed = false;
};

/*! Path of the Palace Python stub (tools/palace_python_stub.{sh,cmd}), made executable on Unix. */
QString palacePythonStub();

#endif // TEST_UTILS_H
