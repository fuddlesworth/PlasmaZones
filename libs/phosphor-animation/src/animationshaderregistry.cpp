// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStringList>

#include <optional>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")

/// Parse one already-validated metadata.json root into an
/// AnimationShaderEffect. The strategy already ran the file-existence,
/// size-cap, and JSON-object-root checks before invoking us; we own only
/// the schema-specific bits — directory-relative path resolution and
/// `isUserEffect` stamping. The strategy itself rejects empty-id
/// payloads with a warning, so we don't double-check here.
///
/// Note — intentional asymmetry with `PhosphorShaders::ShaderRegistry::parseShader`:
/// that parser rejects payloads whose `sourcePath` doesn't exist on disk,
/// whereas this one tolerates missing frag / vert. AnimationShaderEffect
/// payloads are consumed by the kwin effect's transition pipeline which
/// validates path existence at use-time and gracefully falls back; the
/// registry is the catalog, not the gate. Watch-set auto-fingerprinting
/// in the strategy still mixes a "missing" sentinel for absent files,
/// so a frag materialising on disk shifts the signature and fires
/// `effectsChanged`.
///
/// Caveat: the watch-set auto-fingerprint only covers paths that
/// `effectWatchPaths` actually returns (see below) — and that callback
/// only contributes the resolved frag / vert paths when the metadata
/// declared them in the first place. A pack whose `metadata.json`
/// **omits** the field entirely will not have a "missing" watch entry
/// to flip when a `effect.frag` later appears on disk; the change is
/// invisible to the strategy until something else triggers a rescan
/// (e.g. an edit to the `metadata.json` itself, which is always
/// watched). In practice an effect whose metadata never named its
/// frag is unreachable through the normal kwin pipeline, so the
/// missed wakeup is academic — but flagged here so a future schema
/// change that defaults the field can pin the contract.
std::optional<AnimationShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    AnimationShaderEffect e = AnimationShaderEffect::fromJson(root);
    e.sourceDir = effectDir;
    e.isUserEffect = isUserDir;

    // Surface texture-list drops (cap-overflow + empty-path) at scan
    // time. fromJson silently drops these to keep the in-memory struct
    // clean, but pack authors writing a 4-texture metadata.json or
    // accidentally leaving a `"path": ""` deserve a journal entry —
    // mirrors the existing buffer-shader missing-file warning further
    // below.
    {
        const QJsonArray declared = root.value(QLatin1String("textures")).toArray();
        if (declared.size() > AnimationShaderContract::kMaxUserTextureSlots) {
            qCWarning(lcRegistry).noquote()
                << "Animation effect" << e.id << "declares" << declared.size() << "textures; cap is"
                << AnimationShaderContract::kMaxUserTextureSlots
                << "(canonical UBO budget) — surplus entries silently dropped at parse time";
        }
        int emptyPathDrops = 0;
        for (const QJsonValue& v : declared) {
            if (v.toObject().value(QLatin1String("path")).toString().isEmpty()) {
                ++emptyPathDrops;
            }
        }
        if (emptyPathDrops > 0) {
            qCWarning(lcRegistry).noquote() << "Animation effect" << e.id << "has" << emptyPathDrops
                                            << "texture entry/entries with empty `path` — dropped at parse time";
        }
    }

    // Resolve directory-relative paths via QDir::filePath, which returns
    // absolute inputs unchanged — keeps schema tolerance symmetric with
    // PhosphorShaders::ShaderRegistry's parser. Naive string concat
    // would mangle absolute paths from a metadata.json into invalid
    // double-rooted forms.
    const QDir dir(effectDir);
    if (!e.fragmentShaderPath.isEmpty()) {
        e.fragmentShaderPath = dir.filePath(e.fragmentShaderPath);
    }
    if (!e.vertexShaderPath.isEmpty()) {
        e.vertexShaderPath = dir.filePath(e.vertexShaderPath);
    }
    if (!e.previewPath.isEmpty()) {
        e.previewPath = dir.filePath(e.previewPath);
    }

    // Resolve user-texture paths to absolute form once at scan time —
    // mirrors fragmentShaderPath handling. This avoids per-leg
    // QFileInfo + QDir::filePath calls in `translateAnimationParams`
    // (which fires on every transition install + can be hot during
    // back-to-back lifecycle events). Sanitise against `..`-segment
    // traversal: any TextureSlot whose canonical resolved path falls
    // outside the effect's sourceDir is rejected with a warning. Pack
    // metadata is shipped trusted but `untrusted-input` discipline is
    // cheap here and matches CLAUDE.md's "Sanitize file paths to
    // prevent directory traversal" rule.
    {
        const QString canonicalRoot = QDir::cleanPath(dir.absolutePath()) + QLatin1Char('/');
        for (auto& tex : e.textures) {
            if (tex.path.isEmpty())
                continue;
            QString resolved = tex.path;
            if (!QFileInfo(resolved).isAbsolute()) {
                resolved = dir.filePath(resolved);
            }
            const QString canonical = QDir::cleanPath(resolved);
            // Allow absolute paths that the author wrote intentionally
            // (e.g. /usr/share/plasmazones/...). Restrict relative paths
            // to within sourceDir — those came from metadata.json and a
            // `..`-escape there indicates either a pack-author bug or a
            // malicious shipped pack.
            const bool wasRelative = !QFileInfo(tex.path).isAbsolute();
            if (wasRelative && !canonical.startsWith(canonicalRoot)) {
                qCWarning(lcRegistry).noquote()
                    << "Animation effect" << e.id << "texture path" << tex.path << "resolves to" << canonical
                    << "outside source dir" << canonicalRoot << "— rejected (path traversal guard)";
                // Clear BOTH path and wrap so the slot is internally
                // coherent — `translateAnimationParams` skips empty-path
                // slots, and a future `toJson` round-trip would drop
                // the empty entry rather than smuggling a dead wrap
                // through.
                tex.path.clear();
                tex.wrap.clear();
                continue;
            }
            tex.path = canonical;
        }
    }

    // Resolve buffer shader paths (relative to effect dir, like
    // fragment/vertex). Multipass is fail-closed on any missing buffer:
    // `bufferShaderPaths` is positionally aligned with `bufferWraps`
    // and `bufferFilters` (per-buffer overrides), and silently
    // compacting a missing entry would shift downstream wrap/filter
    // overrides onto the wrong buffer with no surface signal to the
    // author. Disable multipass entirely instead so the author sees the
    // full pipeline degrade to single-pass — they will notice and fix
    // their `metadata.json`. The single-pass fallback is a documented
    // graceful-degradation contract; silent index corruption is not.
    if (e.isMultipass) {
        if (e.bufferShaderPaths.isEmpty()) {
            // `multipass: true` with no declared buffer shaders is
            // meaningless — there's nothing to run as a buffer pass.
            // Normalize to single-pass so downstream consumers and
            // diagnostics see a coherent state.
            e.isMultipass = false;
        } else {
            QStringList resolved;
            QStringList missing;
            for (const QString& bufPath : e.bufferShaderPaths) {
                const QString abs = dir.filePath(bufPath);
                if (QFile::exists(abs)) {
                    resolved.append(abs);
                } else {
                    missing.append(abs);
                }
            }
            if (missing.isEmpty()) {
                e.bufferShaderPaths = resolved;
            } else {
                qCWarning(lcRegistry).noquote()
                    << "Animation effect" << e.id << "is missing" << missing.size() << "of"
                    << e.bufferShaderPaths.size()
                    << "declared buffer shader(s); disabling multipass and falling back to single-pass. Missing files:"
                    << missing.join(QLatin1String(", "));
                e.isMultipass = false;
                e.bufferShaderPaths.clear();
                // Per-buffer overrides are positionally aligned with
                // bufferShaderPaths; with paths cleared, the overrides are
                // orphaned data that would still survive toJson round-trip
                // and operator== comparison. Clear them in lockstep so the
                // disabled-multipass struct is internally coherent.
                e.bufferWraps.clear();
                e.bufferFilters.clear();
            }
        }
    }

    return e;
}

/// Per-payload watch list — frag + vert shader file paths. The strategy
/// already adds the metadata.json itself; this callback is for everything
/// else. Preview is informational only (no live-reload need on a static
/// thumbnail) and is excluded.
QStringList effectWatchPaths(const AnimationShaderEffect& e)
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
    return paths;
}

// No `setSignatureContrib` is wired below — the strategy auto-fingerprints
// every distinct file `effectWatchPaths` returns (path|size|mtime|), which
// covers the resolved frag and vertex shader paths in both the
// "search-path reorder swaps the winning copy" and "in-place content edit"
// scenarios. A bespoke `SignatureContrib` would be redundant.

} // namespace

std::unique_ptr<PhosphorFsLoader::IScanStrategy>
AnimationShaderRegistry::buildScanStrategy(AnimationShaderRegistry* self)
{
    auto strategy = std::make_unique<ScanStrategy>(parseEffect, [self]() {
        Q_EMIT self->effectsChanged();
    });
    strategy->setPerEntryWatchPaths(effectWatchPaths);
    strategy->setLoggingCategory(lcRegistry());
    return strategy;
}

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : MetadataPackRegistryBase(lcRegistry(), buildScanStrategy(this), parent)
    , m_typedStrategy(static_cast<ScanStrategy*>(strategy()))
{
    // The static_cast above is safe by construction (`buildScanStrategy`
    // is the only path that populates the base's strategy slot, and it
    // always produces a `ScanStrategy`). Pin that invariant in debug
    // builds via dynamic_cast so a future refactor that diverts the
    // strategy slot fails loudly instead of silently UB-ing on lookup.
    Q_ASSERT_X(dynamic_cast<ScanStrategy*>(strategy()) != nullptr, "AnimationShaderRegistry",
               "buildScanStrategy must return a MetadataPackScanStrategy<AnimationShaderEffect>");
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

void AnimationShaderRegistry::onUserPathChanged(const QString& path)
{
    m_typedStrategy->setUserPath(path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    // Strategy returns a sorted-by-id snapshot — single source of truth
    // for QHash-randomisation-stable output.
    return m_typedStrategy->packs();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_typedStrategy->pack(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_typedStrategy->contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    return m_typedStrategy->packIds();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parameter translation
// ═══════════════════════════════════════════════════════════════════════════════
//
// Mirrors `PhosphorShaders::ShaderRegistry::translateParamsToUniforms`
// in shape but uses metadata declaration order to assign slots, since
// `AnimationShaderEffect::ParameterInfo` does not carry an explicit
// `slot` field (animation effects historically used named default-block
// uniforms and never needed slot allocation). Both runtime execution
// sites consume the slot-keyed map this returns and write the values
// into `customParams[N]` of the canonical animation UBO.

QVariantMap AnimationShaderRegistry::translateAnimationParams(const AnimationShaderEffect& effect,
                                                              const QVariantMap& friendlyParams)
{
    QVariantMap result;
    if (!effect.isValid()) {
        return result;
    }

    int floatSlot = 0;
    int colorSlot = 0;
    for (const auto& param : effect.parameters) {
        const QString& type = param.type;
        if (type == QLatin1String("color")) {
            if (colorSlot >= AnimationShaderContract::kMaxCustomColors) {
                qCWarning(lcRegistry) << "translateAnimationParams: effect" << effect.id << "exceeds"
                                      << AnimationShaderContract::kMaxCustomColors
                                      << "-slot customColors budget; dropping color param" << param.id;
                continue;
            }
            const QString uniformKey = AnimationShaderContract::colorKey(colorSlot);
            // Coerce the source variant to a QColor at the registry
            // boundary so the consumer (kwin-effect's setUniform site,
            // ShaderEffect::setShaderParams) never has to defend against
            // a string-shaped colour leaking through. Accepted shapes:
            // already-a-QColor (QML/settings-UI path); QString in any
            // form QColor's constructor parses (`"#rgb"`, `"#rrggbb"`,
            // `"#aarrggbb"` with alpha FIRST per Qt's convention,
            // higher-bit-depth `"#rrrgggbbb"` / `"#rrrrggggbbbb"`, SVG
            // colour names like `"red"`, the `"transparent"` keyword);
            // anything else falls back to the declared default, then
            // transparent. CSS-style `"#rrggbbaa"` (alpha LAST, 9
            // chars) is NOT accepted: any 9-char hex string is
            // ambiguous between Qt and CSS encodings (same bytes,
            // different channel meaning), so a rewrite would silently
            // corrupt configs that already use Qt's order. Settings UI
            // / config writers emit Qt-form (alpha first) explicitly.
            const auto coerce = [](const QVariant& v) -> QColor {
                if (v.canConvert<QColor>()) {
                    const QColor c = v.value<QColor>();
                    if (c.isValid())
                        return c;
                }
                if (v.canConvert<QString>()) {
                    const QColor c(v.toString());
                    if (c.isValid())
                        return c;
                }
                return {};
            };
            QColor resolved;
            const auto it = friendlyParams.constFind(param.id);
            if (it != friendlyParams.constEnd())
                resolved = coerce(*it);
            if (!resolved.isValid() && param.defaultValue.isValid() && !param.defaultValue.isNull())
                resolved = coerce(param.defaultValue);
            if (!resolved.isValid())
                resolved = QColor(Qt::transparent);
            result[uniformKey] = resolved;
            ++colorSlot;
            continue;
        }

        if (floatSlot >= AnimationShaderContract::kMaxParameterSlots) {
            qCWarning(lcRegistry) << "translateAnimationParams: effect" << effect.id << "exceeds"
                                  << AnimationShaderContract::kMaxParameterSlots
                                  << "-slot customParams budget; dropping param" << param.id;
            continue;
        }

        const QString uniformKey = AnimationShaderContract::slotKey(floatSlot);

        // Resolve the value: friendly map > declared default > 0.0.
        QVariant value;
        const auto it = friendlyParams.constFind(param.id);
        if (it != friendlyParams.constEnd()) {
            value = *it;
        } else if (param.defaultValue.isValid() && !param.defaultValue.isNull()) {
            value = param.defaultValue;
        } else {
            value = 0.0;
        }

        // Coerce booleans to 0/1 floats so the UBO slot is always
        // numeric. Floats and ints both round-trip through QVariant::toFloat
        // in the consumer; no extra coercion needed here.
        if (type == QLatin1String("bool")) {
            value = value.toBool() ? 1.0f : 0.0f;
        }

        result[uniformKey] = value;
        ++floatSlot;
    }

    // ── User textures (uTexture1..3, uTexture1_wrap, ...) ──────────────
    //
    // Pack defaults come from `effect.textures` (the metadata schema);
    // runtime overrides come from `friendlyParams[uTextureN]` /
    // `friendlyParams[uTextureN_wrap]` so callers (settings UI, daemon
    // overlay-service shader-profile resolution) can swap a packaged
    // texture without touching the pack on disk.
    //
    // Slot offset: the canonical animation contract reserves
    // `uTexture0` for the redirected window/surface. `effect.textures[0]`
    // therefore maps to `uTexture1` (runtime slot 1 / SRB binding 8) and
    // so on. friendlyParams may use the GLSL slot name (`uTexture1` ..
    // `uTexture3`) verbatim — that's the same convention overlay zones
    // use, which keeps the override format identical across categories.
    //
    // Pack-default paths in `effect.textures` are already absolute
    // (resolved + traversal-checked at parseEffect scan time). Runtime
    // overrides from `friendlyParams` are emitted verbatim — the
    // override path comes from the settings UI / D-Bus and the consumer
    // (ShaderEffect or kwin-effect) is responsible for whatever
    // resolution it wants. Empty / null paths skip the emit so a
    // setShaderParams consumer's "missing key = no change" semantic
    // doesn't accidentally clear a slot the caller intended to leave
    // alone. Wrap-only entries are gated on a non-empty companion path
    // so a consumer can't end up with a wrap mode applied to an unbound
    // sampler.
    for (int slot = 0; slot < AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
        const int glslSlot = slot + 1; // uTexture0 is reserved for the surface
        const QString pathKey = QStringLiteral("uTexture%1").arg(glslSlot);
        const QString wrapKey = QStringLiteral("uTexture%1_wrap").arg(glslSlot);

        QString path;
        QString wrap;
        if (slot < effect.textures.size()) {
            path = effect.textures[slot].path;
            wrap = effect.textures[slot].wrap;
        }
        const auto pathOverride = friendlyParams.constFind(pathKey);
        if (pathOverride != friendlyParams.constEnd()) {
            path = pathOverride->toString();
        }
        const auto wrapOverride = friendlyParams.constFind(wrapKey);
        if (wrapOverride != friendlyParams.constEnd()) {
            wrap = wrapOverride->toString();
        }

        if (path.isEmpty()) {
            continue; // no texture for this slot — skip both keys
        }
        result[pathKey] = path;
        if (!wrap.isEmpty()) {
            result[wrapKey] = wrap;
        }
    }

    return result;
}

QVariantMap AnimationShaderRegistry::translateAnimationParams(const QString& effectId,
                                                              const QVariantMap& friendlyParams) const
{
    return translateAnimationParams(effect(effectId), friendlyParams);
}

} // namespace PhosphorAnimationShaders
