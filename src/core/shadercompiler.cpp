// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shadercompiler.h"
#include "logging.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace PlasmaZones {

// Boilerplate prepended to all user shaders
// Post-processing approach: zones are pre-rendered, shader applies effects
static const char *SHADER_BOILERPLATE = R"(#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Qt-required uniforms
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

// Pre-rendered zones texture (from ShaderEffectSource)
// This contains the zones already rendered with their colors
layout(binding = 1) uniform sampler2D source;

// ═══════════════════════════════════════════════════════════════════════════
// POST-PROCESSING SHADER API (v2)
// ═══════════════════════════════════════════════════════════════════════════
//
// The 'source' texture contains pre-rendered zones. Your shader should:
// 1. Sample source to get the zone color at each pixel
// 2. Apply post-processing effects (glow, blur, distortion, etc.)
// 3. Output the final color to fragColor
//
// Example:
//   vec4 zoneColor = texture(source, qt_TexCoord0);
//   // Apply your effect...
//   fragColor = result * qt_Opacity;
//
// Helper: Get pixel size for sampling offsets
// vec2 pixelSize = vec2(dFdx(qt_TexCoord0.x), dFdy(qt_TexCoord0.y));
//
// ═══════════════════════════════════════════════════════════════════════════

// Helper function: sample surrounding pixels for glow/blur effects
vec4 sampleGlow(vec2 uv, float radius, int samples) {
    vec2 pixelSize = vec2(dFdx(uv.x), dFdy(uv.y));
    if (abs(pixelSize.x) < 0.0001) pixelSize.x = 0.001;
    if (abs(pixelSize.y) < 0.0001) pixelSize.y = 0.001;
    
    vec4 glow = vec4(0.0);
    float totalWeight = 0.0;
    float angleStep = 6.28318530718 / float(samples);
    
    for (int i = 0; i < samples; i++) {
        float angle = float(i) * angleStep;
        vec2 dir = vec2(cos(angle), sin(angle));
        
        for (float r = 2.0; r <= radius; r += 2.5) {
            vec2 offset = dir * r * abs(pixelSize);
            vec4 s = texture(source, uv + offset);
            float weight = exp(-r * r / (radius * radius * 0.5));
            if (s.a > 0.01) {
                glow += s * weight;
                totalWeight += weight;
            }
        }
    }
    
    return totalWeight > 0.0 ? glow / totalWeight : vec4(0.0);
}

// Helper function: detect edges (where alpha transitions)
float detectEdge(vec2 uv, float radius) {
    vec2 pixelSize = vec2(dFdx(uv.x), dFdy(uv.y));
    if (abs(pixelSize.x) < 0.0001) pixelSize.x = 0.001;
    if (abs(pixelSize.y) < 0.0001) pixelSize.y = 0.001;
    
    vec4 center = texture(source, uv);
    float edgeStrength = 0.0;
    
    for (int i = 0; i < 8; i++) {
        float angle = float(i) * 0.785398;
        vec2 offset = vec2(cos(angle), sin(angle)) * radius * abs(pixelSize);
        vec4 s = texture(source, uv + offset);
        edgeStrength += abs(s.a - center.a);
    }
    
    return edgeStrength;
}

// ============ USER CODE BELOW ============
)";

ShaderCompiler::ShaderCompiler(QObject *parent)
    : QObject(parent)
    , m_boilerplate(QString::fromUtf8(SHADER_BOILERPLATE))
{
}

QString ShaderCompiler::qsbToolPath()
{
    // Try to find qsb in PATH first
    QString qsbPath = QStandardPaths::findExecutable(QStringLiteral("qsb"));
    if (!qsbPath.isEmpty()) {
        return qsbPath;
    }

    // Try common Qt installation directories for various distros
    const QStringList searchPaths = {
        QStringLiteral("/usr/lib/qt6/bin"),
        QStringLiteral("/usr/lib64/qt6/bin"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/bin"),  // Debian/Ubuntu
        QStringLiteral("/usr/lib/aarch64-linux-gnu/qt6/bin"), // ARM64
        QStringLiteral("/opt/qt6/bin"),
        QStringLiteral("/usr/local/qt6/bin"),
    };

    qsbPath = QStandardPaths::findExecutable(QStringLiteral("qsb"), searchPaths);
    if (!qsbPath.isEmpty()) {
        return qsbPath;
    }

    // Try qsb-qt6 variant (some distros)
    qsbPath = QStandardPaths::findExecutable(QStringLiteral("qsb-qt6"));
    if (!qsbPath.isEmpty()) {
        return qsbPath;
    }

    return QString();
}

QString ShaderCompiler::shaderBoilerplate()
{
    return QString::fromUtf8(SHADER_BOILERPLATE);
}

bool ShaderCompiler::isQsbAvailable() const
{
    return !qsbToolPath().isEmpty();
}

QString ShaderCompiler::wrapWithBoilerplate(const QString &userCode) const
{
    return m_boilerplate + userCode;
}

ShaderCompiler::Result ShaderCompiler::compile(const QString &fragPath, const QString &outputPath)
{
    QFile file(fragPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("Cannot open shader file: %1").arg(fragPath);
        qCWarning(lcCore) << m_lastError;
        return Result::InvalidInput;
    }

    QString userCode = QString::fromUtf8(file.readAll());
    file.close();

    return compileWithBoilerplate(userCode, outputPath);
}

ShaderCompiler::Result ShaderCompiler::compileWithBoilerplate(const QString &userCode,
                                                               const QString &outputPath)
{
    if (!isQsbAvailable()) {
        m_lastError = QStringLiteral("qsb tool not found. Install qt6-shadertools.");
        qCWarning(lcCore) << m_lastError;
        return Result::QsbToolNotFound;
    }

    // Write wrapped shader to temp file
    QTemporaryFile tempFile;
    tempFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/plasmazones_shader_XXXXXX.frag"));
    if (!tempFile.open()) {
        m_lastError = QStringLiteral("Cannot create temporary file for shader compilation");
        qCWarning(lcCore) << m_lastError;
        return Result::WriteError;
    }

    QString wrappedCode = wrapWithBoilerplate(userCode);
    tempFile.write(wrappedCode.toUtf8());
    tempFile.close();

    return runQsb(tempFile.fileName(), outputPath);
}

ShaderCompiler::Result ShaderCompiler::runQsb(const QString &inputPath, const QString &outputPath)
{
    Q_EMIT compilationStarted(inputPath);

    QProcess qsb;
    QStringList args;

    // Target multiple shader languages for cross-platform compatibility
    args << QStringLiteral("--glsl") << QStringLiteral("100es,120,150");
    args << QStringLiteral("--hlsl") << QStringLiteral("50");
    args << QStringLiteral("--msl") << QStringLiteral("12");
    args << QStringLiteral("-b");  // Batchable for ShaderEffect (CRITICAL!)
    args << QStringLiteral("-o") << outputPath;
    args << inputPath;

    qCDebug(lcCore) << "Running qsb:" << qsbToolPath() << args;

    qsb.start(qsbToolPath(), args);

    if (!qsb.waitForStarted(5000)) {
        m_lastError = QStringLiteral("Failed to start qsb process: %1").arg(qsb.errorString());
        qCWarning(lcCore) << m_lastError;
        Q_EMIT compilationFinished(inputPath, Result::QsbToolNotFound);
        return Result::QsbToolNotFound;
    }

    if (!qsb.waitForFinished(CompilationTimeoutMs)) {
        qsb.kill();
        m_lastError = QStringLiteral("qsb compilation timed out after %1 seconds")
                          .arg(CompilationTimeoutMs / 1000);
        qCWarning(lcCore) << m_lastError;
        Q_EMIT compilationFinished(inputPath, Result::Timeout);
        return Result::Timeout;
    }

    if (qsb.exitCode() != 0) {
        m_lastError = QString::fromUtf8(qsb.readAllStandardError());
        if (m_lastError.isEmpty()) {
            m_lastError = QString::fromUtf8(qsb.readAllStandardOutput());
        }
        qCWarning(lcCore) << "Shader compilation failed:" << m_lastError;
        Q_EMIT compilationFinished(inputPath, Result::CompilationError);
        return Result::CompilationError;
    }

    qCDebug(lcCore) << "Shader compiled successfully:" << outputPath;
    Q_EMIT compilationFinished(inputPath, Result::Success);
    return Result::Success;
}

} // namespace PlasmaZones
