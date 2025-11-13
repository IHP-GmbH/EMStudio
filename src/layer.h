#ifndef LAYER_H
#define LAYER_H

#include <QString>

/*!*******************************************************************************************************************
 * \class Layer
 * \brief Represents a single physical layer in the substrate stackup.
 *
 * The Layer class encapsulates metadata for a layer such as its name, type (e.g., "conductor", "via", or "dielectric"),
 * Z-position boundaries (minimum and maximum), associated material, and an optional numeric identifier.
 * It is used in modeling and visualizing semiconductor or EM substrate layer structures.
 **********************************************************************************************************************/
class Layer
{
public:
    Layer();

    QString             name() const;
    void                setName(const QString &name);

    QString             type() const;
    void                setType(const QString &type);

    double              zmin() const;
    void                setZmin(double zmin);

    double              zmax() const;
    void                setZmax(double zmax);

    QString             material() const;
    void                setMaterial(const QString &material);

    int                 layerNumber() const;
    void                setLayerNumber(int number);

private:
    QString             m_name;
    QString             m_type;
    double              m_zmin = 0.0;
    double              m_zmax = 0.0;
    QString             m_material;
    int                 m_layerNumber = 0;
};

#endif // LAYER_H
