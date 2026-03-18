// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpackageio.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcShaderEditorIO, "plasmazones.shadereditor.io")

namespace PlasmaZones {
namespace ShaderPackageIO {

ShaderPackageContents loadPackage(const QString& dirPath)
{
    ShaderPackageContents contents;
    contents.dirPath = dirPath;

    QDir dir(dirPath);
    if (!dir.exists()) {
        qCWarning(lcShaderEditorIO) << "Shader package directory does not exist:" << dirPath;
        return contents;
    }

    // Load metadata.json
    const QString metadataPath = dir.absoluteFilePath(QStringLiteral("metadata.json"));
    QFile metadataFile(metadataPath);
    if (metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        contents.metadataJson = QString::fromUtf8(metadataFile.readAll());
        metadataFile.close();
    } else {
        qCWarning(lcShaderEditorIO) << "Failed to read metadata.json from:" << dirPath;
    }

    // Load all shader files (.vert, .frag, .glsl)
    const QStringList shaderFilters = {
        QStringLiteral("*.vert"),
        QStringLiteral("*.frag"),
        QStringLiteral("*.glsl"),
    };

    const QFileInfoList shaderFiles = dir.entryInfoList(shaderFilters, QDir::Files, QDir::Name);
    for (const QFileInfo& fileInfo : shaderFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ShaderFile sf;
            sf.filename = fileInfo.fileName();
            sf.content = QString::fromUtf8(file.readAll());
            contents.files.append(sf);
            file.close();
        } else {
            qCWarning(lcShaderEditorIO) << "Failed to read shader file:" << fileInfo.absoluteFilePath();
        }
    }

    qCDebug(lcShaderEditorIO) << "Loaded shader package from=" << dirPath
                              << "files=" << contents.files.size();
    return contents;
}

bool savePackage(const QString& dirPath, const ShaderPackageContents& contents)
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qCWarning(lcShaderEditorIO) << "Failed to create directory:" << dirPath;
        return false;
    }

    // Write metadata.json
    {
        const QString path = dir.absoluteFilePath(QStringLiteral("metadata.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcShaderEditorIO) << "Failed to open metadata.json for writing:" << file.errorString();
            return false;
        }
        const QByteArray data = contents.metadataJson.toUtf8();
        if (file.write(data) != data.size()) {
            qCWarning(lcShaderEditorIO) << "Incomplete write to metadata.json:" << file.errorString();
            return false;
        }
        file.flush();
    }

    // Write shader files
    for (const ShaderFile& sf : contents.files) {
        if (sf.filename.contains(QLatin1Char('/')) || sf.filename.contains(QLatin1Char('\\'))) {
            qCWarning(lcShaderEditorIO) << "Rejecting filename with path separator:" << sf.filename;
            return false;
        }
        const QString path = dir.absoluteFilePath(sf.filename);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcShaderEditorIO) << "Failed to open for writing:" << path << file.errorString();
            return false;
        }
        const QByteArray data = sf.content.toUtf8();
        if (file.write(data) != data.size()) {
            qCWarning(lcShaderEditorIO) << "Incomplete write to:" << path << file.errorString();
            return false;
        }
        file.flush();
    }

    qCDebug(lcShaderEditorIO) << "Saved shader package to=" << dirPath << "files=" << contents.files.size();
    return true;
}

ShaderPackageContents createTemplate(const QString& shaderId, const QString& shaderName)
{
    ShaderPackageContents contents;

    // metadata.json
    QJsonObject metadata;
    metadata[QStringLiteral("id")] = shaderId;
    metadata[QStringLiteral("name")] = shaderName;
    metadata[QStringLiteral("category")] = QStringLiteral("Custom");
    metadata[QStringLiteral("description")] = QString();
    metadata[QStringLiteral("author")] = QString();
    metadata[QStringLiteral("version")] = QStringLiteral("1.0");
    metadata[QStringLiteral("fragmentShader")] = QStringLiteral("effect.frag");
    metadata[QStringLiteral("vertexShader")] = QStringLiteral("zone.vert");
    metadata[QStringLiteral("multipass")] = false;
    metadata[QStringLiteral("parameters")] = QJsonArray();

    QJsonDocument doc(metadata);
    contents.metadataJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

    // zone.vert
    ShaderFile vertexShader;
    vertexShader.filename = QStringLiteral("zone.vert");
    vertexShader.content = QStringLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "layout(location = 1) out vec2 vFragCoord;\n"
        "\n"
        "#include <common.glsl>\n"
        "\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    vFragCoord = vec2(texCoord.x, 1.0 - texCoord.y) * iResolution;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n");
    contents.files.append(vertexShader);

    // effect.frag
    ShaderFile fragmentShader;
    fragmentShader.filename = QStringLiteral("effect.frag");
    fragmentShader.content = QStringLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 1) in vec2 vFragCoord;\n"
        "\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "\n"
        "#include <common.glsl>\n"
        "\n"
        "void main() {\n"
        "    vec2 uv = vFragCoord / iResolution;\n"
        "\n"
        "    // Your shader code here\n"
        "    vec3 col = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(iTime));\n"
        "\n"
        "    // Zone masking (renders only inside zone boundaries)\n"
        "    float mask = zoneMask(vFragCoord);\n"
        "    fragColor = vec4(col * mask, mask);\n"
        "}\n");
    contents.files.append(fragmentShader);

    qCDebug(lcShaderEditorIO) << "Created shader template id=" << shaderId << "name=" << shaderName;
    return contents;
}

QString userShaderDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shaders");
}

QString systemShaderDirectory()
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                  QStringLiteral("plasmazones/shaders"),
                                  QStandardPaths::LocateDirectory);
}

QString computeUniformName(const QString& type, int slot)
{
    if (type == QLatin1String("color")) {
        if (slot < 0 || slot > 15) {
            return {};
        }
        return QStringLiteral("customColor%1").arg(slot + 1);
    }

    // float, int, bool
    if (slot < 0 || slot > 31) {
        return {};
    }

    const int vecIndex = slot / 4;
    const int component = slot % 4;
    static const char* components[] = {"x", "y", "z", "w"};
    return QStringLiteral("customParams%1_%2").arg(vecIndex + 1).arg(QLatin1String(components[component]));
}

QString sanitizeId(const QString& name)
{
    QString id = name.toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression leadTrailDash(QStringLiteral("^-|-$"));
    id.replace(nonAlnum, QStringLiteral("-"));
    id.remove(leadTrailDash);
    if (id.isEmpty()) {
        id = QStringLiteral("custom-shader");
    }
    return id;
}

} // namespace ShaderPackageIO
} // namespace PlasmaZones
