#ifndef DIELECTRIC_H
#define DIELECTRIC_H

#include <QString>

class Dielectric
{
public:
    Dielectric();

    QString             name() const;
    void                setName(const QString &name);

    QString             material() const;
    void                setMaterial(const QString &material);

    double              thickness() const;
    void                setThickness(double thickness);

private:
    QString             m_name;
    QString             m_material;
    double              m_thickness = 0.0;
};

#endif // DIELECTRIC_H
