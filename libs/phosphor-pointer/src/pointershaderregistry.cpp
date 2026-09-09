// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerShaderRegistry.h>

#include <PhosphorPointer/PointerShaderContract.h>

#include <PhosphorFsLoader/PackPathGuard.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <optional>

namespace PhosphorPointerShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphorpointershaders.registry")

/// True when @p rawPath carries no `..` segment after lexical cleaning.
/// Splits on both `/` and `\` as defence in depth (same as the surface
/// registry's guard for in-memory effects).
bool pathHasNoTraversalSegments(const QString& rawPath)
{
    if (rawPath.isEmpty()) {
        return true;
    }
    const QString cleaned = QDir::cleanPath(rawPath);
    const QStringList segments = cleaned.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts);
    return std::none_of(segments.cbegin(), segments.cend(), [](const QString& seg) {
        return seg == QLatin1String("..");
    });
}

std::optional<PointerShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    // fromJson owns the schema, the path resolution and the traversal
    // guard; the strategy already ran the file-existence, size-cap and
    // JSON-root checks.
    return PointerShaderEffect::fromJson(root, effectDir, isUserDir);
}

/// Files the loader watches per pack beside its metadata.json.
QStringList effectWatchPaths(const PointerShaderEffect& e)
{
    QStringList paths;
    if (!e.fragmentShaderPath.isEmpty()) {
        paths.append(e.fragmentShaderPath);
    }
    if (!e.vertexShaderPath.isEmpty()) {
        paths.append(e.vertexShaderPath);
    }
    for (const QString& bufPath : e.bufferShaderPaths) {
        if (!bufPath.isEmpty()) {
            paths.append(bufPath);
        }
    }
    for (const auto& tex : e.textures) {
        if (!tex.path.isEmpty()) {
            paths.append(tex.path);
        }
    }
    return paths;
}

/// Per-entry content signature: path|size|mtime of metadata.json and every
/// watched file, plus the user classification (which can flip with no file
/// change when setUserPath lands after addSearchPaths).
void effectContentSignature(QCryptographicHash& hasher, const PointerShaderEffect& e)
{
    const auto mixFile = [&hasher](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo fi(path);
        hasher.addData(path.toUtf8());
        if (fi.exists()) {
            hasher.addData(QByteArray::number(fi.size()));
            hasher.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
        } else {
            hasher.addData(QByteArrayView("missing"));
        }
    };
    if (!e.sourceDir.isEmpty()) {
        mixFile(e.sourceDir + QStringLiteral("/metadata.json"));
    }
    const QStringList watched = effectWatchPaths(e);
    for (const QString& p : watched) {
        mixFile(p);
    }
    hasher.addData(e.isUserEffect ? QByteArrayLiteral("u") : QByteArrayLiteral("s"));
}

QColor coerceColor(const QVariant& v)
{
    // Type-id, not canConvert<QColor>: a QString variant would otherwise be
    // swallowed as an invalid QColor before the string branch runs.
    if (v.metaType().id() == QMetaType::QColor) {
        const QColor c = v.value<QColor>();
        if (c.isValid()) {
            return c;
        }
    }
    if (v.canConvert<QString>()) {
        const QColor c(v.toString());
        if (c.isValid()) {
            return c;
        }
    }
    return {};
}

} // namespace

PointerShaderRegistry::PointerShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_loader(std::make_unique<PhosphorRegistry::MetadataPackLoader<PointerPack>>(
          &m_registry,
          [](const QString& subdir, const QJsonObject& root, bool isUser) -> std::shared_ptr<PointerPack> {
              std::optional<PointerShaderEffect> e = parseEffect(subdir, root, isUser);
              return e ? std::make_shared<PointerPack>(std::move(*e)) : nullptr;
          },
          lcRegistry()))
{
    m_loader->setPerEntryWatchPaths([](const PointerPack& p) {
        return effectWatchPaths(p.effect());
    });
    m_loader->setSignatureContrib([](QCryptographicHash& hasher, const PointerPack& p) {
        effectContentSignature(hasher, p.effect());
    });
    m_loader->setOnCommitted([this]() {
        Q_EMIT effectsChanged();
    });
}

PointerShaderRegistry::~PointerShaderRegistry() = default;

void PointerShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    m_loader->addSearchPath(path, liveReload);
}

void PointerShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                           PhosphorFsLoader::RegistrationOrder order)
{
    m_loader->addSearchPaths(paths, liveReload, order);
}

QStringList PointerShaderRegistry::searchPaths() const
{
    return m_loader->searchPaths();
}

void PointerShaderRegistry::setUserPath(const QString& path)
{
    m_loader->setUserPath(path);
}

void PointerShaderRegistry::refresh()
{
    m_loader->refresh();
}

QList<PointerShaderEffect> PointerShaderRegistry::availableEffects() const
{
    QList<PointerShaderEffect> result;
    result.reserve(m_registry.size());
    m_registry.forEach([&result](const std::shared_ptr<PointerPack>& pack) {
        result.append(pack->effect());
    });
    std::sort(result.begin(), result.end(), [](const PointerShaderEffect& a, const PointerShaderEffect& b) {
        return a.id < b.id;
    });
    return result;
}

PointerShaderEffect PointerShaderRegistry::effect(const QString& id) const
{
    const auto pack = m_registry.factory(id);
    return pack ? pack->effect() : PointerShaderEffect{};
}

bool PointerShaderRegistry::hasEffect(const QString& id) const
{
    return m_registry.factory(id) != nullptr;
}

QStringList PointerShaderRegistry::effectIds() const
{
    QStringList ids = m_registry.ids();
    std::sort(ids.begin(), ids.end());
    return ids;
}

QVariantMap PointerShaderRegistry::translatePointerParams(const PointerShaderEffect& effect,
                                                          const QVariantMap& friendlyParams)
{
    QVariantMap result;
    if (!effect.isValid()) {
        return result;
    }

    int floatSlot = 0;
    int colorSlot = 0;
    QStringList droppedColorParams;
    QStringList droppedFloatParams;
    for (const auto& param : effect.parameters) {
        // Ids the preamble rejects consume no lane on either side.
        if (!PhosphorShaders::isValidParamId(param.id)) {
            continue;
        }
        if (param.type == QLatin1String("image")) {
            // Image parameters are texture-slot bindings, not UBO lanes.
            continue;
        }
        if (param.type == QLatin1String("color")) {
            if (colorSlot >= PointerShaderContract::kMaxCustomColors) {
                droppedColorParams.append(param.id);
                continue;
            }
            QColor resolved;
            const auto it = friendlyParams.constFind(param.id);
            if (it != friendlyParams.constEnd()) {
                resolved = coerceColor(*it);
            }
            if (!resolved.isValid() && param.defaultValue.isValid() && !param.defaultValue.isNull()) {
                resolved = coerceColor(param.defaultValue);
            }
            if (!resolved.isValid()) {
                resolved = QColor(Qt::transparent);
            }
            result[PointerShaderContract::colorKey(colorSlot)] = resolved;
            ++colorSlot;
            continue;
        }

        if (floatSlot >= PointerShaderContract::kMaxParameterSlots) {
            droppedFloatParams.append(param.id);
            continue;
        }
        QVariant value;
        const auto it = friendlyParams.constFind(param.id);
        if (it != friendlyParams.constEnd()) {
            value = *it;
        } else if (param.defaultValue.isValid() && !param.defaultValue.isNull()) {
            value = param.defaultValue;
        } else {
            value = 0.0;
        }
        if (param.type == QLatin1String("bool")) {
            value = value.toBool() ? 1.0f : 0.0f;
        }
        result[PointerShaderContract::paramKey(floatSlot)] = value;
        ++floatSlot;
    }

    if (!droppedColorParams.isEmpty()) {
        qCWarning(lcRegistry).noquote() << "translatePointerParams: effect" << effect.id << "exceeds"
                                        << PointerShaderContract::kMaxCustomColors
                                        << "-slot customColors budget; dropped"
                                        << droppedColorParams.join(QLatin1String(", "));
    }
    if (!droppedFloatParams.isEmpty()) {
        qCWarning(lcRegistry).noquote() << "translatePointerParams: effect" << effect.id << "exceeds"
                                        << PointerShaderContract::kMaxParameterSlots
                                        << "-slot customParams budget; dropped"
                                        << droppedFloatParams.join(QLatin1String(", "));
    }

    // User textures: pack defaults from effect.textures (already confined at
    // load time when the pack is on disk), runtime overrides through
    // `uTexture<N>` / `uTexture<N>_wrap` with the same guard.
    for (int slot = 0; slot < PointerShaderContract::kMaxUserTextureSlots; ++slot) {
        const int glslSlot = slot + 1;
        const QString pathKey = QStringLiteral("uTexture%1").arg(glslSlot);
        const QString wrapKey = QStringLiteral("uTexture%1_wrap").arg(glslSlot);

        QString path;
        QString wrap;
        if (slot < effect.textures.size()) {
            path = effect.textures[slot].path;
            wrap = effect.textures[slot].wrap;
            if (effect.sourceDir.isEmpty() && !path.isEmpty() && !pathHasNoTraversalSegments(path)) {
                qCWarning(lcRegistry).noquote() << "Pointer effect" << effect.id << "in-memory default texture path"
                                                << path << "rejected (path traversal guard)";
                path.clear();
                wrap.clear();
            }
        }
        const auto pathOverride = friendlyParams.constFind(pathKey);
        if (pathOverride != friendlyParams.constEnd()) {
            const QString candidate = pathOverride->toString();
            if (candidate.isEmpty()) {
                path.clear();
                wrap.clear();
            } else if (effect.sourceDir.isEmpty()) {
                if (!pathHasNoTraversalSegments(candidate)) {
                    qCWarning(lcRegistry).noquote() << "Pointer effect" << effect.id << "runtime override texture path"
                                                    << candidate << "rejected (path traversal guard)";
                    path.clear();
                    wrap.clear();
                } else {
                    path = candidate;
                }
            } else {
                const auto validated = PhosphorFsLoader::resolveWithinDirectory(
                    candidate, effect.sourceDir, PhosphorFsLoader::AbsolutePathPolicy::Trust);
                if (!validated) {
                    qCWarning(lcRegistry).noquote()
                        << "Pointer effect" << effect.id << "runtime override texture path" << candidate
                        << "resolves outside" << effect.sourceDir << "— rejected (path traversal guard)";
                    path.clear();
                    wrap.clear();
                } else {
                    path = *validated;
                }
            }
        }
        const auto wrapOverride = friendlyParams.constFind(wrapKey);
        if (wrapOverride != friendlyParams.constEnd()) {
            const QString candidateWrap = wrapOverride->toString();
            if (candidateWrap.isEmpty() || PointerShaderContract::isValidWrapToken(candidateWrap)) {
                wrap = candidateWrap;
            } else {
                qCWarning(lcRegistry) << "Pointer effect" << effect.id << "runtime override wrap value" << candidateWrap
                                      << "rejected (not clamp/repeat/mirror)";
                wrap.clear();
            }
        }
        if (path.isEmpty()) {
            continue;
        }
        result[pathKey] = path;
        if (!wrap.isEmpty()) {
            result[wrapKey] = wrap;
        }
    }

    return result;
}

QVariantMap PointerShaderRegistry::translatePointerParams(const QString& effectId,
                                                          const QVariantMap& friendlyParams) const
{
    return translatePointerParams(effect(effectId), friendlyParams);
}

QString PointerShaderRegistry::paramPreamble(const PointerShaderEffect& effect)
{
    QList<PhosphorShaders::PreambleParam> params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        if (p.type == QLatin1String("image")) {
            continue;
        }
        PhosphorShaders::PreambleParam entry;
        entry.id = p.id;
        entry.pool = (p.type == QLatin1String("color")) ? PhosphorShaders::PreambleParam::Pool::Color
                                                        : PhosphorShaders::PreambleParam::Pool::Scalar;
        entry.explicitSlot = -1;
        params.append(entry);
    }
    return PhosphorShaders::buildParamPreamble(params);
}

QString PointerShaderRegistry::pointerEntryPrologue()
{
    return QStringLiteral(
        "#version 450\n"
        "#include <pointer_lib.glsl>\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 0) out vec4 fragColor;\n");
}

QList<PhosphorShaders::EntryCandidate> PointerShaderRegistry::pointerEntryCandidates()
{
    // The write goes through PZ_FINALIZE_COLOR, which pointer_uniforms.glsl
    // defaults to identity and the compositor's main pass overrides with the
    // sRGB-to-output conversion (see the hook's note in that header).
    static const QString pointerMain = QStringLiteral(
        "void main() {\n"
        "    fragColor = PZ_FINALIZE_COLOR(pPointer(vTexCoord));\n"
        "}\n");
    return {PhosphorShaders::EntryCandidate{QStringLiteral("pPointer"), pointerMain}};
}

QStringList PointerShaderRegistry::includePathsFor(const QString& packDir)
{
    if (packDir.isEmpty()) {
        return {};
    }
    // The pack's own neighbourhood first, and that part stays purely lexical,
    // deliberately: these two directories are a property of where the pack
    // SITS, so the answer must not depend on what is on disk at the moment of
    // the call. A pack removed between the scan and a recompile has to fail as
    // a missing pack, not as a missing include, which is the confusing shape a
    // filesystem walk gave it here before.
    const QString cleaned = QDir::cleanPath(packDir);
    const int lastSlash = cleaned.lastIndexOf(QLatin1Char('/'));
    if (lastSlash <= 0) {
        return {};
    }
    const QString packRoot = cleaned.left(lastSlash);
    QStringList paths{packRoot + QStringLiteral("/shared"), packRoot};

    // Then the installed shared helpers, because a pack outside the bundled
    // tree has no sibling shared/ to find them in. The entry prologue always
    // emits `#include <pointer_lib.glsl>`, so without this EVERY user pack
    // under ~/.local/share/plasmazones/pointer fails include expansion — it
    // renders in the compositor, which builds its search list from the
    // registry's roots, and then shows a blank preview and fails the
    // validator, both of which come through here.
    //
    // Appending roots does not reintroduce the walk the note above rules out:
    // no file is probed, and the pack's own directories still come first, so a
    // pack shipping its own shared/ is still served from it.
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& dir : dataDirs) {
        const QString shared = dir + QStringLiteral("/plasmazones/pointer/shared");
        if (!paths.contains(shared)) {
            paths.append(shared);
        }
    }
    return paths;
}

} // namespace PhosphorPointerShaders
