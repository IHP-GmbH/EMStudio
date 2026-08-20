# Minimal stackup_writer shim for ADS Momentum import (Volker momentum_import.py).
# Enough to build a schemaVersion 2.0-style XML tree that EMStudio can load.

import xml.etree.ElementTree as ET


def new_stackup_tree():
    root = ET.Element("Stackup", schemaVersion="2.0")
    ET.SubElement(root, "Materials")
    elayers = ET.SubElement(root, "ELayers", LengthUnit="um")
    ET.SubElement(elayers, "Dielectrics")
    ET.SubElement(elayers, "Layers")
    return ET.ElementTree(root)


def get_materials_element(root):
    return root.find("Materials")


def get_dielectrics_element(root):
    return root.find("ELayers/Dielectrics")


def get_layers_element(root):
    return root.find("ELayers/Layers")


def get_derived_layers_element(root):
    elayers = root.find("ELayers")
    if elayers is None:
        return None
    el = elayers.find("DerivedLayers")
    if el is None:
        el = ET.SubElement(elayers, "DerivedLayers")
    return el


def get_substrate_offset_element(root):
    layers = get_layers_element(root)
    if layers is None:
        return None
    return layers.find("Substrate")


def add_material(root, **attrs):
    mats = get_materials_element(root)
    el = ET.SubElement(mats, "Material")
    for k, v in attrs.items():
        if v is not None and str(v) != "":
            el.set(k, str(v))
    return el


def add_dielectric(root, **attrs):
    diels = get_dielectrics_element(root)
    el = ET.SubElement(diels, "Dielectric")
    for k, v in attrs.items():
        if v is not None and str(v) != "":
            el.set(k, str(v))
    return el


def add_layer(root, **attrs):
    layers = get_layers_element(root)
    el = ET.SubElement(layers, "Layer")
    for k, v in attrs.items():
        if v is not None and str(v) != "":
            el.set(k, str(v))
    return el


def set_substrate_offset(root, value):
    layers = get_layers_element(root)
    el = layers.find("Substrate")
    if el is None:
        # Insert Substrate as first child of Layers
        el = ET.Element("Substrate")
        layers.insert(0, el)
    el.set("Offset", str(value))
    return el


def add_derived_layer(root, **attrs):
    parent = get_derived_layers_element(root)
    el = ET.SubElement(parent, "DerivedLayer")
    for k, v in attrs.items():
        if v is not None and str(v) != "":
            el.set(k, str(v))
    return el


def set_operands(derived_el, operand_layers):
    for child in list(derived_el):
        if child.tag == "Operand":
            derived_el.remove(child)
    for layer in operand_layers:
        ET.SubElement(derived_el, "Operand", Layer=str(layer))


def validate_stackup(root):
    # Light checks only — full Volker validation lives in setupEM.
    errors = []
    if root.find("Materials") is None:
        errors.append("Missing <Materials>")
    if root.find("ELayers") is None:
        errors.append("Missing <ELayers>")
    return errors


def save_stackup_tree(tree, path):
    root = tree.getroot()
    # Pretty-print for readability
    try:
        ET.indent(root, space="  ")
    except AttributeError:
        pass
    tree.write(path, encoding="utf-8", xml_declaration=True)
