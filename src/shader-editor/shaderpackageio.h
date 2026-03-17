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
}

} // namespace PlasmaZones
