// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/SchemaValidator.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")

/// Reject a path string that contains any `..` segment after lexical
/// cleaning. Used as a sourceDir-independent traversal guard for
/// in-memory effects where there is no on-disk anchor to bound an
/// absolute path against.
///
/// Returns true when the path is safe (no `..` segments) and false
/// when it contains traversal segments. Empty input returns true (the
/// caller decides whether to skip empty paths separately).
///
/// Phosphor is Linux/Wayland only; lexical comparison is case-
/// sensitive which matches Linux filesystem semantics. Splits on
/// BOTH `/` and `\` as defence-in-depth against Windows-style
/// traversal strings (`..\evil`) that QDir::cleanPath leaves untouched
/// on POSIX hosts, cheap belt-and-braces for a security boundary.
///
/// After QDir::cleanPath, intermediate `..` segments are collapsed
/// (e.g. `/foo/../etc` becomes `/etc`); only LEADING `..` segments
/// (which would escape an unanchored relative path) are preserved
/// and rejected. This is intentional, the in-memory override branch
/// already trusts absolute paths to the caller's discretion (see
/// validateTexturePathWithinEffectDir docstring), and a path that
/// cleanPath has reduced to absolute is no different from one the
/// caller supplied as absolute directly.
bool pathHasNoTraversalSegments(const QString& rawPath)
{
    if (rawPath.isEmpty())
        return true;
    const QString cleaned = QDir::cleanPath(rawPath);
    const QStringList segments = cleaned.split(QRegularExpression(QStringLiteral("[/\\\\]")), Qt::SkipEmptyParts);
    for (const QString& seg : segments) {
        if (seg == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

/// Validate a (possibly relative) texture path against an effect dir.
/// Returns the validated absolute path on success, or std::nullopt
/// when the path resolves outside the effect dir under canonical-vs-
/// canonical or lexical-vs-lexical comparison (i.e. `..`-traversal or
/// symlink-escape from a relative path).
///
/// Confinement only applies to relative paths: absolute paths are
/// trusted to the caller's discretion (settings UI / D-Bus pickers
/// can legitimately point at user-chosen files outside the effect
/// dir). Missing target files are NOT rejected here — parseEffect
/// tolerates missing texture files for the same reason it tolerates
/// missing frag/vert (live-reload during development; QImage-load
/// gracefully reports failure at use-time). The runtime override
/// path applies an additional `pathHasNoTraversalSegments` check at
/// the in-memory-effect call site for defence-in-depth.
///
/// Shared between the scan-time texture-list resolver in `parseEffect`
/// and the runtime override resolver in `translateAnimationParams` so
/// both paths apply the same defence against `..`-traversal and
/// symlink-escape. Callers handle the nullopt case to either skip the
/// slot (overrides) or clear the slot in lockstep with its wrap field
/// (parseEffect).
///
/// Trust model: the caller-provided `effect` (used to derive
/// `effectDir`) is internally trusted because it was either parsed
/// from a controlled-source metadata.json or constructed in-process.
/// The runtime `friendlyParams` map at the call site, however, is
/// treated as untrusted input — it can carry user-supplied strings
/// from settings UI, D-Bus, or shader-profile JSON, so the same
/// confinement check applies uniformly.
///
/// `effectId` is for the warning message only — diagnostic hygiene so
/// pack authors see which effect's metadata or runtime override was
/// rejected. Pass an empty string for runtime overrides where the
/// effect id is implicit in the call context.
std::optional<QString> validateTexturePathWithinEffectDir(const QString& rawPath, const QString& effectDir,
                                                          const QString& effectId)
{
    if (rawPath.isEmpty() || effectDir.isEmpty())
        return std::nullopt;
    const QDir dir(effectDir);
    const QFileInfo origInfo(rawPath);
    const bool wasRelative = !origInfo.isAbsolute();
    QString resolved = rawPath;
    if (wasRelative) {
        resolved = dir.filePath(resolved);
    }
    const QString lexicalRoot = QDir::cleanPath(dir.absolutePath()) + QLatin1Char('/');
    const QString canonicalRootResolved = QFileInfo(dir.absolutePath()).canonicalFilePath();
    const QString canonicalRoot =
        canonicalRootResolved.isEmpty() ? QString() : canonicalRootResolved + QLatin1Char('/');
    const QString canonicalSymResolved = QFileInfo(resolved).canonicalFilePath();
    // Pick comparison domain: prefer canonical (catches symlink
    // escapes) when BOTH sides resolved, otherwise fall back to
    // lexical for both. Never mix the two.
    const bool useCanonical = !canonicalSymResolved.isEmpty() && !canonicalRootResolved.isEmpty();
    const QString canonical = useCanonical ? canonicalSymResolved : QDir::cleanPath(resolved);
    const QString comparisonRoot = useCanonical ? canonicalRoot : lexicalRoot;
    if (wasRelative && !canonical.startsWith(comparisonRoot)) {
        qCWarning(lcRegistry).noquote() << "Animation effect" << effectId << "texture path" << rawPath << "resolves to"
                                        << canonical << "outside source dir" << comparisonRoot
                                        << "— rejected (path traversal guard)";
        return std::nullopt;
    }
    return canonical;
}

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
    // Validate metadata.json (identity + parameter contract) before building it,
    // so a malformed pack is skipped. Compiled once on first parse, fail-closed.
    static const PhosphorFsLoader::SchemaValidator validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/phosphoranimation/schemas/animation-metadata.schema.json"), lcRegistry());
    if (const auto errors = validator.validate(root)) {
        qCWarning(lcRegistry) << "Skipping animation pack failing schema validation:" << effectDir;
        for (const auto& err : *errors) {
            qCWarning(lcRegistry).nospace()
                << "  " << (err.path.isEmpty() ? QStringLiteral("(root)") : err.path) << ": " << err.message;
        }
        return std::nullopt;
    }

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
        // Use canonicalFilePath() to follow symlinks — `QDir::cleanPath`
        // is purely lexical and does NOT resolve symbolic links, so a
        // pack author could ship `effectDir/innocent.png` as a symlink
        // pointing to `/etc/passwd` and the lexical-only check would
        // pass it (target lives under canonicalRoot lexically). The
        // QImage load downstream then follows the symlink and reads
        // the linked file. Defence-in-depth even though packs are
        // nominally trusted: matches CLAUDE.md's "Sanitize file paths
        // to prevent directory traversal" rule and protects against a
        // future pack-distribution channel that accepts third-party
        // uploads.
        //
        // Validation lives in `validateTexturePathWithinEffectDir` so
        // the runtime override path in `translateAnimationParams`
        // applies the IDENTICAL canonical / lexical traversal checks
        // — symmetric defence whether the texture path arrived via
        // metadata.json or via a friendlyParams override at use time.
        for (auto& tex : e.textures) {
            if (tex.path.isEmpty())
                continue;
            const auto validated = validateTexturePathWithinEffectDir(tex.path, effectDir, e.id);
            if (!validated) {
                // Clear BOTH path and wrap so the slot is internally
                // coherent — `translateAnimationParams` skips empty-path
                // slots, and a future `toJson` round-trip would drop
                // the empty entry rather than smuggling a dead wrap
                // through.
                tex.path.clear();
                tex.wrap.clear();
                continue;
            }
            tex.path = *validated;
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

/// Per-payload watch list — frag + vert + buffer shader files AND
/// declared user-texture paths. The strategy already adds the
/// metadata.json itself; this callback covers everything else. Preview
/// is informational only (no live-reload need on a static thumbnail)
/// and is excluded.
///
/// Why textures are watched: `m_textureCache` (kwin-effect side) and
/// `m_userTextureImages` (daemon ShaderEffect side) both key on
/// absolute path. A bitmap content change with no path change would
/// otherwise serve the stale GPU upload for the rest of the session
/// — the registry's `effectsChanged` is the canonical invalidation
/// signal both consumers consume, so feeding texture mtimes into the
/// strategy's fingerprint propagates the reload uniformly.
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
    for (const auto& tex : e.textures) {
        if (!tex.path.isEmpty()) {
            paths.append(tex.path);
        }
    }
    return paths;
}

// Per-entry content signature: path|size|mtime of the pack's metadata.json
// plus every file effectWatchPaths returns (frag/vert/buffer shaders +
// declared textures). Drives MetadataPackLoader's per-entry reconcile so an
// edited pack re-registers with fresh data while unedited siblings stay put;
// the loader's coarse onCommitted hook re-emits effectsChanged on any
// committed rescan regardless.
void effectContentSignature(QCryptographicHash& hasher, const AnimationShaderEffect& e)
{
    const auto mixFile = [&hasher](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo fi(path);
        hasher.addData(path.toUtf8());
        hasher.addData(QByteArray::number(fi.size()));
        hasher.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
    };
    if (!e.sourceDir.isEmpty()) {
        mixFile(e.sourceDir + QStringLiteral("/metadata.json"));
    }
    const QStringList watched = effectWatchPaths(e);
    for (const QString& p : watched) {
        mixFile(p);
    }
    // isUser is set from the user-path classification, NOT from file content —
    // it can flip (setUserPath after addSearchPaths) with no file change, so
    // mix it in or the reconcile would keep the stale-classification entry.
    hasher.addData(e.isUserEffect ? "u" : "s");
}

} // namespace

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_loader(std::make_unique<PhosphorRegistry::MetadataPackLoader<AnimationPack>>(
          &m_registry,
          // Parser: reuse the existing metadata→AnimationShaderEffect parse,
          // then wrap the result in an AnimationPack for the registry.
          [](const QString& subdir, const QJsonObject& root, bool isUser) -> std::shared_ptr<AnimationPack> {
              std::optional<AnimationShaderEffect> e = parseEffect(subdir, root, isUser);
              return e ? std::make_shared<AnimationPack>(std::move(*e)) : nullptr;
          },
          lcRegistry()))
{
    m_loader->setPerEntryWatchPaths([](const AnimationPack& p) {
        return effectWatchPaths(p.effect());
    });
    m_loader->setSignatureContrib([](QCryptographicHash& hasher, const AnimationPack& p) {
        effectContentSignature(hasher, p.effect());
    });
    m_loader->setOnCommitted([this]() {
        Q_EMIT effectsChanged();
    });
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

// ── Search paths (forwarded to the loader) ───────────────────────────────

void AnimationShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    m_loader->addSearchPath(path, liveReload);
}

void AnimationShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                             PhosphorFsLoader::RegistrationOrder order)
{
    m_loader->addSearchPaths(paths, liveReload, order);
}

QStringList AnimationShaderRegistry::searchPaths() const
{
    return m_loader->searchPaths();
}

void AnimationShaderRegistry::setUserPath(const QString& path)
{
    m_loader->setUserPath(path);
}

void AnimationShaderRegistry::refresh()
{
    m_loader->refresh();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    // Registry iteration is insertion order; sort by id for alphabetical
    // output (the legacy strategy returned sorted).
    QList<AnimationShaderEffect> result;
    result.reserve(m_registry.size());
    m_registry.forEach([&result](const std::shared_ptr<AnimationPack>& pack) {
        result.append(pack->effect());
    });
    std::sort(result.begin(), result.end(), [](const AnimationShaderEffect& a, const AnimationShaderEffect& b) {
        return a.id < b.id;
    });
    return result;
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    const auto pack = m_registry.factory(id);
    return pack ? pack->effect() : AnimationShaderEffect{};
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_registry.factory(id) != nullptr;
}

QStringList AnimationShaderRegistry::effectIds() const
{
    QStringList ids = m_registry.ids();
    std::sort(ids.begin(), ids.end());
    return ids;
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
    QStringList droppedColorParams;
    QStringList droppedFloatParams;
    for (const auto& param : effect.parameters) {
        // Skip ids the p_<id> preamble (buildParamPreamble via paramPreamble)
        // rejects, so this upload-lane numbering stays identical to the define
        // numbering — a rejected param consumes no lane on either side.
        if (!PhosphorShaders::isValidParamId(param.id)) {
            continue;
        }
        const QString& type = param.type;
        if (type == QLatin1String("color")) {
            if (colorSlot >= AnimationShaderContract::kMaxCustomColors) {
                droppedColorParams.append(param.id);
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
                // Type-id (NOT canConvert<QColor>) so a QString variant
                // takes the QString branch below. QString DOES
                // canConvert<QColor> — it returns an INVALID QColor for
                // any string, swallowing the variant before the proper
                // QColor(QString) constructor branch runs. The metatype
                // check pins this branch to actual QColor variants.
                if (v.metaType().id() == QMetaType::QColor) {
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
            droppedFloatParams.append(param.id);
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

    // One summary warning per overflow class instead of one per param —
    // an effect that pushes the slot budget hard would otherwise spam
    // the journal with N near-identical lines that all carry the same
    // remediation.
    if (!droppedColorParams.isEmpty()) {
        qCWarning(lcRegistry).noquote() << "translateAnimationParams: effect" << effect.id << "exceeds"
                                        << AnimationShaderContract::kMaxCustomColors
                                        << "-slot customColors budget; dropped" << droppedColorParams.size()
                                        << "color params:" << droppedColorParams.join(QLatin1String(", "));
    }
    if (!droppedFloatParams.isEmpty()) {
        qCWarning(lcRegistry).noquote() << "translateAnimationParams: effect" << effect.id << "exceeds"
                                        << AnimationShaderContract::kMaxParameterSlots
                                        << "-slot customParams budget; dropped" << droppedFloatParams.size()
                                        << "params:" << droppedFloatParams.join(QLatin1String(", "));
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
    // overrides from `friendlyParams` may arrive relative — settings UI
    // / D-Bus callers don't all know the effect's sourceDir. Resolve
    // and traversal-check relative override paths against
    // `effect.sourceDir` via the SAME helper parseEffect uses, so the
    // downstream cache (`m_textureCache` on kwin / `m_userTextureImages`
    // on daemon) keys on a stable absolute path regardless of which
    // input shape the caller used AND a malicious caller can't bypass
    // the scan-time guard by passing `../../../etc/passwd` through a
    // friendlyParams override. Empty / null paths skip the emit so a
    // setShaderParams consumer's "missing key = no change" semantic
    // doesn't accidentally clear a slot the caller intended to leave
    // alone. Wrap-only entries are gated on a non-empty companion
    // path so a consumer can't end up with a wrap mode applied to an
    // unbound sampler.
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
            const QString candidate = pathOverride->toString();
            if (candidate.isEmpty()) {
                // Empty-string override = explicit clear of the slot.
                // The pack-default wrap (if any) is intentionally
                // dropped because wrap is meaningless without a bound
                // texture — emitting a wrap-only key would attach a
                // wrap mode to an unbound sampler and violate the
                // downstream contract. wrap emit is suppressed via the
                // path-empty skip below; explicit clear here would be
                // redundant and silently overwritten by a later
                // uTextureN_wrap override in the same params map.
                path = candidate;
            } else if (effect.sourceDir.isEmpty()) {
                // Degenerate case — pack came from an in-memory
                // factory with no on-disk anchor. There is no
                // sourceDir to bound the path against, but the
                // override map is still untrusted input — reject any
                // candidate containing `..` segments so a malicious
                // caller can't smuggle a traversal path through the
                // in-memory branch.
                if (!pathHasNoTraversalSegments(candidate)) {
                    qCWarning(lcRegistry).noquote()
                        << "Animation effect" << effect.id << "runtime override texture path" << candidate
                        << "rejected (path traversal guard)";
                    path.clear();
                    wrap.clear();
                } else if (QFileInfo(candidate).isRelative()) {
                    // Relative paths in the in-memory branch are caller-
                    // resolved (no sourceDir means no anchor for us to
                    // resolve against). Accept the candidate verbatim
                    // and emit a debug log so a settings-UI tester
                    // notices when an in-memory effect's relative
                    // override flows through unchanged. Production
                    // packs should always have a sourceDir — this branch
                    // is exercised by test fixtures and future scripted-
                    // shader hooks.
                    qCDebug(lcRegistry).noquote()
                        << "Animation effect" << effect.id
                        << "in-memory override texture path is relative:" << candidate
                        << "— passed through caller-resolved (no sourceDir to anchor against)";
                    path = candidate;
                } else {
                    path = candidate;
                }
            } else {
                const auto validated = validateTexturePathWithinEffectDir(candidate, effect.sourceDir, effect.id);
                if (!validated) {
                    // Rejected override clears BOTH path and wrap for
                    // this slot — same coherence rule parseEffect
                    // applies. Skip the slot entirely so the unbound
                    // sampler doesn't end up with a wrap-only emit.
                    path.clear();
                    wrap.clear();
                } else {
                    path = *validated;
                }
            }
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

QString AnimationShaderRegistry::paramPreamble(const AnimationShaderEffect& effect)
{
    // Mirror translateAnimationParams' allocation exactly: color params take
    // the customColors pool, everything else the customParams scalar pool,
    // both auto-numbered in declaration order. buildParamPreamble advances the
    // two pools independently, so the generated macro for each param resolves
    // to the same UBO lane translateAnimationParams uploads its value to.
    QList<PhosphorShaders::PreambleParam> params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        PhosphorShaders::PreambleParam entry;
        entry.id = p.id;
        entry.pool = (p.type == QLatin1String("color")) ? PhosphorShaders::PreambleParam::Pool::Color
                                                        : PhosphorShaders::PreambleParam::Pool::Scalar;
        entry.explicitSlot = -1; // animation packs always auto-slot by declaration order
        params.append(entry);
    }
    return PhosphorShaders::buildParamPreamble(params);
}

QString AnimationShaderRegistry::animationEntryPrologue()
{
    // `#version` first; the animation-uniforms include declares the UBO (both
    // runtime branches) plus the T1.5 direction helpers (legProgress /
    // p_reversed) and surfaceColor; then the vertex texcoord in and the
    // fragColor out an entry-only pack no longer declares by hand.
    return QStringLiteral(
        "#version 450\n"
        "#include <animation_uniforms.glsl>\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 0) out vec4 fragColor;\n");
}

QList<PhosphorShaders::EntryCandidate> AnimationShaderRegistry::animationEntryCandidates()
{
    // Symmetric: one function, `t` is raw iTime (the runtime still flips it on
    // reverse legs, so the shader auto-mirrors with no direction code).
    static const QString transitionMain = QStringLiteral(
        "void main() {\n"
        "    fragColor = pTransition(vTexCoord, iTime);\n"
        "}\n");
    // Asymmetric: the harness un-flips iTime (legProgress → forward 0→1) and
    // dispatches by direction, so the author never touches iIsReversed/iTime
    // and the `== 1` footgun is gone.
    static const QString inOutMain = QStringLiteral(
        "void main() {\n"
        "    float p_t = legProgress();\n"
        "    fragColor = p_reversed ? pOut(vTexCoord, p_t) : pIn(vTexCoord, p_t);\n"
        "}\n");

    PhosphorShaders::EntryCandidate transition;
    transition.functionName = QStringLiteral("pTransition");
    transition.generatedMain = transitionMain;

    PhosphorShaders::EntryCandidate inOut;
    inOut.functionName = QStringLiteral("pIn");
    inOut.generatedMain = inOutMain;
    inOut.alsoRequires = {QStringLiteral("pOut")};

    return {transition, inOut};
}

} // namespace PhosphorAnimationShaders
