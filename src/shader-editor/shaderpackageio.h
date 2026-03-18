// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace PlasmaZones {

struct ShaderFile {
    QString filename;
    QString content;
};

struct ShaderPackageContents {
    QString dirPath;
    QString metadataJson;
    QList<ShaderFile> files; // zone.vert, effect.frag, pass0.frag, etc.
};

namespace ShaderPackageIO {
    ShaderPackageContents loadPackage(const QString& dirPath);
    bool savePackage(const QString& dirPath, const ShaderPackageContents& contents);
    ShaderPackageContents createTemplate(const QString& shaderId, const QString& shaderName);
    QString userShaderDirectory();
    QString systemShaderDirectory();

    /**
     * Compute the uniform name for a shader parameter given its type and slot.
     * For "color" type: returns "customColor<slot+1>" (slot 0-15).
     * For "float"/"int"/"bool": returns "customParams<vec+1>_<component>" (slot 0-31).
     */
    QString computeUniformName(const QString& type, int slot);

    /**
     * Sanitize a human-readable name into a kebab-case ID suitable for shader IDs
     * and filesystem use. Lowercases, replaces non-alphanumeric chars with hyphens,
     * strips leading/trailing hyphens.
     */
    QString sanitizeId(const QString& name);
}

} // namespace PlasmaZones
