// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include "internal.h"

#include <QCache>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace PhosphorRendering {

// ═══════════════════════════════════════════════════════════════════════════════
// Bake Targets
// ═══════════════════════════════════════════════════════════════════════════════

const QList<QShaderBaker::GeneratedShader>& ShaderCompiler::bakeTargets()
{
    static const QList<QShaderBaker::GeneratedShader> targets = {
        // SPIR-V 1.0: Qt's Vulkan QRhi backend looks up QShaderKey(SpirvShader, QShaderVersion(100)).
        {QShader::SpirvShader, QShaderVersion(100)},
        {QShader::GlslShader, QShaderVersion(330)},
        {QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs)},
        {QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs)},
        {QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs)},
    };
    return targets;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Compilation Cache
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

struct BakeCache
{
    static constexpr int kMaxSize = 256;
    using Key = QPair<QByteArray, int>;
    QMutex mutex;
    // QCache provides LRU eviction (touched on object()), unlike a plain QHash
    // whose iteration-order eviction can flush the hot shader. Cost = 1 per
    // entry → bounded at kMaxSize entries. QCache stores by pointer and owns
    // the heap-allocated QShader.
    QCache<Key, QShader> entries{kMaxSize};

    static BakeCache& instance()
    {
        static BakeCache s;
        return s;
    }
};

// QShaderBaker → glslang is not reentrant: concurrent bake() calls crash inside
// QSpirvCompiler::compileToSpirv(). All bakes must serialize on this mutex.
// Held across the bake only — not across cache reads, so already-cached shaders
// still resolve without contention.
QMutex& bakeSerializationMutex()
{
    static QMutex m;
    return m;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// compile()
// ═══════════════════════════════════════════════════════════════════════════════

ShaderCompiler::Result ShaderCompiler::compile(const QByteArray& source, QShader::Stage stage)
{
    Result result;

    if (source.isEmpty()) {
        result.error = QStringLiteral("Empty shader source");
        return result;
    }

    // Check cache
    const BakeCache::Key key(source, static_cast<int>(stage));
    auto& cache = BakeCache::instance();
    {
        QMutexLocker lock(&cache.mutex);
        // object() touches the LRU position so frequently-used shaders stay
        // resident even when the cache nears capacity.
        if (QShader* hit = cache.entries.object(key); hit && hit->isValid()) {
            result.shader = *hit;
            result.success = true;
            return result;
        }
    }

    // Cache miss — bake under the serialization mutex (glslang is not reentrant).
    // Double-check inside the lock so a concurrent caller that beat us to the
    // bake doesn't trigger a second redundant bake.
    QMutexLocker bakeLock(&bakeSerializationMutex());
    {
        QMutexLocker cacheLock(&cache.mutex);
        if (QShader* hit = cache.entries.object(key); hit && hit->isValid()) {
            result.shader = *hit;
            result.success = true;
            return result;
        }
    }

    QShaderBaker baker;
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders(bakeTargets());
    baker.setSourceString(source, stage);
    result.shader = baker.bake();

    if (result.shader.isValid()) {
        result.success = true;
        QMutexLocker cacheLock(&cache.mutex);
        // QCache::insert evicts the LRU entry when over capacity. Heap-allocate
        // because QCache owns its values.
        cache.entries.insert(key, new QShader(result.shader));
    } else {
        result.error = baker.errorMessage();
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// loadAndExpand()
// ═══════════════════════════════════════════════════════════════════════════════

QString ShaderCompiler::loadAndExpand(const QString& path, const QStringList& includePaths, QString* outError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError)
            *outError = QStringLiteral("Failed to open: ") + path;
        return QString();
    }

    const QString raw = QTextStream(&file).readAll();
    const QString currentFileDir = QFileInfo(path).absolutePath();

    // Build include search paths: current file dir first, then caller-provided paths
    QStringList searchPaths;
    searchPaths.append(currentFileDir);
    for (const QString& p : includePaths) {
        if (!p.isEmpty() && !searchPaths.contains(p))
            searchPaths.append(p);
    }

    QString err;
    const QString expanded =
        PhosphorShaders::ShaderIncludeResolver::expandIncludes(raw, currentFileDir, searchPaths, &err);
    if (!err.isEmpty()) {
        if (outError)
            *outError = err;
        return QString();
    }
    return expanded;
}

// ═══════════════════════════════════════════════════════════════════════════════
// compileFromFile()
// ═══════════════════════════════════════════════════════════════════════════════

ShaderCompiler::Result ShaderCompiler::compileFromFile(const QString& path, const QStringList& includePaths)
{
    Result result;

    QString err;
    const QString source = loadAndExpand(path, includePaths, &err);
    if (source.isEmpty()) {
        result.error = err.isEmpty() ? QStringLiteral("Empty shader after include expansion: ") + path : err;
        return result;
    }

    return compile(source.toUtf8(),
                   path.endsWith(QLatin1String(".vert")) ? QShader::VertexStage : QShader::FragmentStage);
}

// ═══════════════════════════════════════════════════════════════════════════════
// clearCache()
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderCompiler::clearCache()
{
    {
        auto& cache = BakeCache::instance();
        QMutexLocker lock(&cache.mutex);
        cache.entries.clear();
    }
    // Also flush the filename+mtime cache in shadernoderhicore.cpp so a
    // single clearCache() call fully invalidates all baked-shader state.
    clearFilenameShaderCache();
}

} // namespace PhosphorRendering
