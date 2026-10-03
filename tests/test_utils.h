#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <QByteArray>
#include <QString>
#include <QPoint>
#include <QVector>
#include <tuple>

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

/*! Minimal GDSII writer for tests (records, integers, XY, REAL8). */
namespace GdsTestWriter
{
/*! One record: size, record type, data type, payload (strings padded to even length). */
QByteArray record(quint8 type, quint8 dataType, QByteArray payload = QByteArray());
QByteArray int16(int v);
QByteArray int32(qint32 v);
/*! XY record of the points (database units). */
QByteArray xy(const QVector<QPoint> &pts);
/*! GDSII REAL8: excess-64 base-16 exponent, 56-bit mantissa. */
QByteArray real8(double v);
/*! A library with one structure \a cell holding \a boundaries (layer, datatype, points), 1 nm units. */
QByteArray library(const QString &cell, const QVector<std::tuple<int, int, QVector<QPoint>>> &boundaries);
}

/*! Path of the Palace Python stub (tools/palace_python_stub.{sh,cmd}), made executable on Unix. */
QString palacePythonStub();

#endif // TEST_UTILS_H
