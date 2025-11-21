#ifndef PYTHONPARSER_H
#define PYTHONPARSER_H

#include <QMap>
#include <QVariant>
#include <QString>

class PythonParser
{
public:
    struct Result
    {
        bool                        ok = false;
        QMap<QString, QVariant>     settings;
        QString                     error;

        QString                     simPath;
        QString                     gdsFilename;
        QString                     xmlFilename;

        QString getGdsFilename() const { return gdsFilename; }
        QString getXmlFilename() const { return xmlFilename; }
    };

    static Result parseSettings(const QString &filePath);
};

#endif // PYTHONPARSER_H
