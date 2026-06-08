// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include "internal.h"

#include <QByteArray>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <mutex>

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

// ═══════════════════════════════════════════════════════════════════════════════
// Persistent (on-disk) bake cache
// ═══════════════════════════════════════════════════════════════════════════════
//
// The in-memory QCache above is lost on every process exit, so each daemon
// start re-bakes every shader from scratch (QShaderBaker → glslang/SPIR-V is the
// single largest allocation source at startup). A serialized QShader survives
// across runs, so we additionally persist bake results to a content-addressed
// disk cache: subsequent launches load SPIR-V/GLSL straight from disk and skip
// glslang entirely.
//
// Key = SHA-256 of (stage byte ‖ fully-expanded source). Because the source
// passed to compile() already has every #include inlined, an edit to a shader
// OR any of its includes changes the digest, so the cache is self-invalidating —
// no mtime tracking or explicit eviction on edit needed. Orphaned blobs from
// old source versions are bounded by a one-shot prune (see below).

constexpr int kMaxDiskCacheEntries = 512;

// Cache directory, resolved + created once. Empty string = disk cache disabled
// (unset/unwritable cache location, or the opt-out env var).
QString diskCacheDir()
{
    static const QString dir = [] {
        if (qEnvironmentVariableIsSet("PHOSPHOR_DISABLE_SHADER_DISK_CACHE")) {
            return QString();
        }
        const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (base.isEmpty()) {
            return QString();
        }
        // The QShader serialized format is Qt-version-bound. fromSerialized()
        // already rejects mismatches (→ treated as a miss + re-bake), but
        // versioning the directory by Qt version + a local schema tag keeps
        // stale blobs from a prior Qt from piling up indefinitely.
        const QString d =
            base + QLatin1String("/phosphor-shadercache/qt") + QLatin1String(QT_VERSION_STR) + QLatin1String("-v1");
        if (!QDir().mkpath(d)) {
            qCWarning(lcShaderNode) << "shader disk cache: cannot create directory" << d << "— disk cache disabled";
            return QString();
        }
        return d;
    }();
    return dir;
}

QString diskCachePath(const QByteArray& source, int stage)
{
    const QString dir = diskCacheDir();
    if (dir.isEmpty()) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const char stageByte = static_cast<char>(stage);
    hash.addData(QByteArrayView(&stageByte, 1));
    hash.addData(source);
    return dir + QLatin1Char('/') + QString::fromLatin1(hash.result().toHex()) + QLatin1String(".qsb");
}

// Bound disk growth: orphaned blobs only accrue when shaders are edited, so a
// single prune per process (on first write) is enough. Deletes oldest-by-mtime
// down to ~90% of the cap when exceeded.
void pruneDiskCacheOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        const QString dir = diskCacheDir();
        if (dir.isEmpty()) {
            return;
        }
        QFileInfoList blobs = QDir(dir).entryInfoList({QStringLiteral("*.qsb")}, QDir::Files, QDir::Time);
        if (blobs.size() <= kMaxDiskCacheEntries) {
            return;
        }
        // entryInfoList(QDir::Time) is newest-first; delete from the tail.
        const int keep = kMaxDiskCacheEntries * 9 / 10;
        for (int i = keep; i < blobs.size(); ++i) {
            QFile::remove(blobs.at(i).absoluteFilePath());
        }
        qCInfo(lcShaderNode) << "shader disk cache: pruned" << (blobs.size() - keep) << "stale entries";
    });
}

// Returns a valid QShader on hit, an invalid one on miss / corrupt / version
// mismatch (all treated identically: re-bake).
QShader readDiskCache(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

void writeDiskCache(const QString& path, const QShader& shader)
{
    if (path.isEmpty()) {
        return;
    }
    pruneDiskCacheOnce();
    // QSaveFile writes to a temp file and atomically renames on commit(), so a
    // concurrent reader (or another PlasmaZones process baking the same key)
    // never observes a partial blob; identical keys produce identical bytes, so
    // last-writer-wins is harmless.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    f.write(shader.serialized());
    f.commit();
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

    // Persistent-cache check before the (expensive) bake: a serialized QShader
    // from a previous run skips glslang entirely. Populate the in-memory cache
    // on a disk hit so repeat lookups this run stay hot.
    const QString diskPath = diskCachePath(source, static_cast<int>(stage));
    if (QShader fromDisk = readDiskCache(diskPath); fromDisk.isValid()) {
        result.shader = fromDisk;
        result.success = true;
        QMutexLocker cacheLock(&cache.mutex);
        cache.entries.insert(key, new QShader(fromDisk));
        return result;
    }

    QShaderBaker baker;
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders(bakeTargets());
    baker.setSourceString(source, stage);
    result.shader = baker.bake();

    if (result.shader.isValid()) {
        result.success = true;
        {
            QMutexLocker cacheLock(&cache.mutex);
            // QCache::insert evicts the LRU entry when over capacity. Heap-allocate
            // because QCache owns its values.
            cache.entries.insert(key, new QShader(result.shader));
        }
        // Persist for future runs. Done after the in-memory insert and outside
        // the cache mutex (the bake mutex still serializes writers); a failed
        // write just means a future re-bake, never a wrong result.
        writeDiskCache(diskPath, result.shader);
    } else {
        result.error = baker.errorMessage();
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// loadAndExpand()
// ═══════════════════════════════════════════════════════════════════════════════

QString ShaderCompiler::loadAndExpand(const QString& path, const QStringList& includePaths, QString* outError,
                                      QStringList* outIncludedPaths)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError)
            *outError = QStringLiteral("Failed to open: ") + path;
        return QString();
    }

    const QString raw = QTextStream(&file).readAll();
    const QString currentFileDir = QFileInfo(path).absolutePath();
    return expandSource(raw, currentFileDir, includePaths, outError, outIncludedPaths);
}

QString ShaderCompiler::expandSource(const QString& source, const QString& currentFileDir,
                                     const QStringList& includePaths, QString* outError, QStringList* outIncludedPaths)
{
    // Build include search paths: current file dir first, then caller-provided paths
    QStringList searchPaths;
    searchPaths.append(currentFileDir);
    for (const QString& p : includePaths) {
        if (!p.isEmpty() && !searchPaths.contains(p))
            searchPaths.append(p);
    }

    QString err;
    const QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, currentFileDir, searchPaths,
                                                                                    &err, outIncludedPaths);
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
