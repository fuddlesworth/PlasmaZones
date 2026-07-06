// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

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

namespace PhosphorSurfaceShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphorsurfaceshaders.registry")

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
/// and the runtime override resolver in `translateSurfaceParams` so
/// both paths apply the same defence against `..`-traversal and
/// symlink-escape. Callers handle the nullopt case to either skip the
/// slot (overrides) or clear the slot in lockstep with its wrap field
/// (parseEffect).
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
        qCWarning(lcRegistry).noquote() << "Surface effect" << effectId << "texture path" << rawPath << "resolves to"
                                        << canonical << "outside source dir" << comparisonRoot
                                        << "— rejected (path traversal guard)";
        return std::nullopt;
    }
    return canonical;
}

/// Parse one already-validated metadata.json root into a
/// SurfaceShaderEffect. The strategy already ran the file-existence,
/// size-cap, and JSON-object-root checks before invoking us; we own only
/// the schema-specific bits — directory-relative path resolution and
/// `isUserEffect` stamping. The strategy itself rejects empty-id
/// payloads with a warning, so we don't double-check here.
///
/// Like `AnimationShaderRegistry::parseEffect`, this tolerates missing
/// frag / vert: SurfaceShaderEffect payloads are consumed by the
/// surface-layer pipeline which validates path existence at use-time and
/// gracefully falls back; the registry is the catalog, not the gate.
std::optional<SurfaceShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    SurfaceShaderEffect e = SurfaceShaderEffect::fromJson(root);
    e.sourceDir = effectDir;
    e.isUserEffect = isUserDir;

    // Surface texture-list drops (cap-overflow + empty-path) at scan
    // time. fromJson silently drops these to keep the in-memory struct
    // clean, but pack authors writing a 4-texture metadata.json or
    // accidentally leaving a `"path": ""` deserve a journal entry.
    {
        const QJsonArray declared = root.value(QLatin1String("textures")).toArray();
        if (declared.size() > SurfaceShaderContract::kMaxUserTextureSlots) {
            qCWarning(lcRegistry).noquote()
                << "Surface effect" << e.id << "declares" << declared.size() << "textures; cap is"
                << SurfaceShaderContract::kMaxUserTextureSlots
                << "(canonical texture budget) — surplus entries silently dropped at parse time";
        }
        // Count empties within the first kMaxUserTextureSlots raw entries: an
        // empty path beyond that range was (or would be) dropped as SURPLUS
        // (warned above), and counting it here would misattribute the drop
        // reason. This is an approximation of fromJson's exact walk — fromJson
        // caps on ACCUMULATED non-empty entries, so with early empties it can
        // consume raw entries past this range — accepted for a diagnostic:
        // the loaded result is unaffected either way.
        int emptyPathDrops = 0;
        const int processed = qMin(static_cast<int>(declared.size()), SurfaceShaderContract::kMaxUserTextureSlots);
        for (int i = 0; i < processed; ++i) {
            if (declared.at(i).toObject().value(QLatin1String("path")).toString().isEmpty()) {
                ++emptyPathDrops;
            }
        }
        if (emptyPathDrops > 0) {
            qCWarning(lcRegistry).noquote() << "Surface effect" << e.id << "has" << emptyPathDrops
                                            << "texture entry/entries with empty `path` — dropped at parse time";
        }
    }

    // Resolve directory-relative paths and confine them to the effect dir
    // with the SAME `..`-traversal / symlink-escape guard the texture list
    // uses below (validateTexturePathWithinEffectDir returns absolute inputs
    // unchanged, so schema tolerance stays symmetric with
    // PhosphorShaders::ShaderRegistry's parser, and naive-concat double-root
    // mangling is avoided). A relative frag/vert/preview that escapes the
    // pack dir is cleared, which fail-closes the pack (an empty frag path
    // compiles nothing) rather than reading source from outside the pack.
    const QDir dir(effectDir);
    if (!e.fragmentShaderPath.isEmpty()) {
        const auto validated = validateTexturePathWithinEffectDir(e.fragmentShaderPath, effectDir, e.id);
        e.fragmentShaderPath = validated.value_or(QString());
    }
    if (!e.vertexShaderPath.isEmpty()) {
        const auto validated = validateTexturePathWithinEffectDir(e.vertexShaderPath, effectDir, e.id);
        e.vertexShaderPath = validated.value_or(QString());
    }
    if (!e.previewPath.isEmpty()) {
        const auto validated = validateTexturePathWithinEffectDir(e.previewPath, effectDir, e.id);
        e.previewPath = validated.value_or(QString());
    }

    // Resolve user-texture paths to absolute form once at scan time —
    // mirrors fragmentShaderPath handling. Sanitise against `..`-segment
    // traversal: any TextureSlot whose canonical resolved path falls
    // outside the effect's sourceDir is rejected with a warning. Pack
    // metadata is shipped trusted but `untrusted-input` discipline is
    // cheap here and matches CLAUDE.md's "Sanitize file paths to
    // prevent directory traversal" rule.
    //
    // Validation lives in `validateTexturePathWithinEffectDir` so the
    // runtime override path in `translateSurfaceParams` applies the
    // IDENTICAL canonical / lexical traversal checks — symmetric defence
    // whether the texture path arrived via metadata.json or via a
    // friendlyParams override at use time.
    for (auto& tex : e.textures) {
        if (tex.path.isEmpty())
            continue;
        const auto validated = validateTexturePathWithinEffectDir(tex.path, effectDir, e.id);
        if (!validated) {
            // Clear BOTH path and wrap so the slot is internally
            // coherent — `translateSurfaceParams` skips empty-path
            // slots, and a future `toJson` round-trip would drop the
            // empty entry rather than smuggling a dead wrap through.
            tex.path.clear();
            tex.wrap.clear();
            continue;
        }
        tex.path = *validated;
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
                // Confine buffer shaders to the pack dir with the same guard as
                // frag/vert/textures; a `..`-traversal path (nullopt) or a
                // missing file both funnel to the fail-closed single-pass
                // fallback below rather than reading source outside the pack.
                const auto validated = validateTexturePathWithinEffectDir(bufPath, effectDir, e.id);
                if (validated && QFile::exists(*validated)) {
                    resolved.append(*validated);
                } else {
                    missing.append(validated.value_or(dir.filePath(bufPath)));
                }
            }
            if (missing.isEmpty()) {
                e.bufferShaderPaths = resolved;
            } else {
                qCWarning(lcRegistry).noquote()
                    << "Surface effect" << e.id << "is missing" << missing.size() << "of" << e.bufferShaderPaths.size()
                    << "declared buffer shader(s); disabling multipass and falling back to single-pass. Missing files:"
                    << missing.join(QLatin1String(", "));
                e.isMultipass = false;
                e.bufferShaderPaths.clear();
                // The per-buffer wrap/filter overrides are cleared by the
                // single-pass coherence block below (isMultipass is now
                // false), which is the single owner of that cleanup.
            }
        }
    }
    // A single-pass pack (multipass never declared, declared-but-empty, or
    // fail-closed above) must not carry orphan buffer-only fields: the
    // per-buffer override arrays claim positional alignment with a
    // bufferShaderPaths that is empty, and the scalar buffer-only fields
    // (wrap/filter/feedback/depth/scale) are meaningless without buffer
    // passes. All of them survive toJson and participate in operator==, so
    // clear/reset every one to its default and keep the parsed struct
    // internally coherent regardless of which branch produced single-pass.
    if (!e.isMultipass) {
        // bufferShaderPaths first: a pack that declared `bufferShaders` WITHOUT
        // `multipass: true` never entered the resolve-to-absolute branch above,
        // so it still holds RAW RELATIVE names. Left set, they survive toJson /
        // operator== and feed the file watcher + content signature CWD-relative
        // (bogus) paths. Clearing it is the whole point of this coherence block.
        e.bufferShaderPaths.clear();
        e.bufferWraps.clear();
        e.bufferFilters.clear();
        e.bufferWrap.clear();
        e.bufferFilter.clear();
        e.bufferFeedback = false;
        e.useDepthBuffer = false;
        e.bufferScale = 1.0;
    }

    return e;
}

/// Per-payload watch list — frag + vert + buffer shader files AND
/// declared user-texture paths. The strategy already adds the
/// metadata.json itself; this callback covers everything else. Preview
/// is informational only (no live-reload need on a static thumbnail)
/// and is excluded.
QStringList effectWatchPaths(const SurfaceShaderEffect& e)
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
// plus every file effectWatchPaths returns (frag/vert + declared textures).
// Drives MetadataPackLoader's per-entry reconcile so an edited pack
// re-registers with fresh data while unedited siblings stay put; the
// loader's coarse onCommitted hook re-emits effectsChanged on any
// committed rescan regardless.
void effectContentSignature(QCryptographicHash& hasher, const SurfaceShaderEffect& e)
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

SurfaceShaderRegistry::SurfaceShaderRegistry(QObject* parent)
    : QObject(parent)
    , m_loader(std::make_unique<PhosphorRegistry::MetadataPackLoader<SurfacePack>>(
          &m_registry,
          // Parser: reuse the existing metadata→SurfaceShaderEffect parse,
          // then wrap the result in a SurfacePack for the registry.
          [](const QString& subdir, const QJsonObject& root, bool isUser) -> std::shared_ptr<SurfacePack> {
              std::optional<SurfaceShaderEffect> e = parseEffect(subdir, root, isUser);
              return e ? std::make_shared<SurfacePack>(std::move(*e)) : nullptr;
          },
          lcRegistry()))
{
    m_loader->setPerEntryWatchPaths([](const SurfacePack& p) {
        return effectWatchPaths(p.effect());
    });
    m_loader->setSignatureContrib([](QCryptographicHash& hasher, const SurfacePack& p) {
        effectContentSignature(hasher, p.effect());
    });
    m_loader->setOnCommitted([this]() {
        Q_EMIT effectsChanged();
    });
}

SurfaceShaderRegistry::~SurfaceShaderRegistry() = default;

// ── Search paths (forwarded to the loader) ───────────────────────────────

void SurfaceShaderRegistry::addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload)
{
    m_loader->addSearchPath(path, liveReload);
}

void SurfaceShaderRegistry::addSearchPaths(const QStringList& paths, PhosphorFsLoader::LiveReload liveReload,
                                           PhosphorFsLoader::RegistrationOrder order)
{
    m_loader->addSearchPaths(paths, liveReload, order);
}

QStringList SurfaceShaderRegistry::searchPaths() const
{
    return m_loader->searchPaths();
}

void SurfaceShaderRegistry::setUserPath(const QString& path)
{
    m_loader->setUserPath(path);
}

void SurfaceShaderRegistry::refresh()
{
    m_loader->refresh();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<SurfaceShaderEffect> SurfaceShaderRegistry::availableEffects() const
{
    // Registry iteration is insertion order; sort by id for alphabetical
    // output.
    QList<SurfaceShaderEffect> result;
    result.reserve(m_registry.size());
    m_registry.forEach([&result](const std::shared_ptr<SurfacePack>& pack) {
        result.append(pack->effect());
    });
    std::sort(result.begin(), result.end(), [](const SurfaceShaderEffect& a, const SurfaceShaderEffect& b) {
        return a.id < b.id;
    });
    return result;
}

SurfaceShaderEffect SurfaceShaderRegistry::effect(const QString& id) const
{
    const auto pack = m_registry.factory(id);
    return pack ? pack->effect() : SurfaceShaderEffect{};
}

bool SurfaceShaderRegistry::hasEffect(const QString& id) const
{
    return m_registry.factory(id) != nullptr;
}

QStringList SurfaceShaderRegistry::effectIds() const
{
    QStringList ids = m_registry.ids();
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parameter translation
// ═══════════════════════════════════════════════════════════════════════════════
//
// Mirrors `AnimationShaderRegistry::translateAnimationParams` exactly:
// metadata declaration order assigns slots since
// `SurfaceShaderEffect::ParameterInfo` does not carry an explicit `slot`
// field. Both runtime execution sites consume the slot-keyed map this
// returns and write the values into `customParams[N]` / `customColors[N]`
// of the canonical surface uniform set.

QVariantMap SurfaceShaderRegistry::translateSurfaceParams(const SurfaceShaderEffect& effect,
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
            if (colorSlot >= SurfaceShaderContract::kMaxCustomColors) {
                droppedColorParams.append(param.id);
                continue;
            }
            const QString uniformKey = SurfaceShaderContract::colorKey(colorSlot);
            // Coerce the source variant to a QColor at the registry
            // boundary so the consumer never has to defend against a
            // string-shaped colour leaking through. Accepted shapes:
            // already-a-QColor (QML/settings-UI path); QString in any
            // form QColor's constructor parses (`"#rgb"`, `"#rrggbb"`,
            // `"#aarrggbb"` with alpha FIRST per Qt's convention, SVG
            // colour names, the `"transparent"` keyword); anything else
            // falls back to the declared default, then transparent.
            // CSS-style `"#rrggbbaa"` (alpha LAST, 9 chars) is NOT
            // accepted: any 9-char hex string is ambiguous between Qt
            // and CSS encodings, so a rewrite would silently corrupt
            // configs that already use Qt's order.
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

        if (floatSlot >= SurfaceShaderContract::kMaxParameterSlots) {
            droppedFloatParams.append(param.id);
            continue;
        }

        const QString uniformKey = SurfaceShaderContract::slotKey(floatSlot);

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
        qCWarning(lcRegistry).noquote() << "translateSurfaceParams: effect" << effect.id << "exceeds"
                                        << SurfaceShaderContract::kMaxCustomColors
                                        << "-slot customColors budget; dropped" << droppedColorParams.size()
                                        << "color params:" << droppedColorParams.join(QLatin1String(", "));
    }
    if (!droppedFloatParams.isEmpty()) {
        qCWarning(lcRegistry).noquote() << "translateSurfaceParams: effect" << effect.id << "exceeds"
                                        << SurfaceShaderContract::kMaxParameterSlots
                                        << "-slot customParams budget; dropped" << droppedFloatParams.size()
                                        << "params:" << droppedFloatParams.join(QLatin1String(", "));
    }

    // ── User textures (uTexture1..3, uTexture1_wrap, ...) ──────────────
    //
    // Pack defaults come from `effect.textures` (the metadata schema);
    // runtime overrides come from `friendlyParams[uTextureN]` /
    // `friendlyParams[uTextureN_wrap]` so callers (settings UI, daemon
    // surface-layer resolution) can swap a packaged texture without
    // touching the pack on disk.
    //
    // Slot offset: the canonical surface contract reserves `uTexture0`
    // for the captured surface. `effect.textures[0]` therefore maps to
    // `uTexture1` and so on. friendlyParams may use the GLSL slot name
    // (`uTexture1` .. `uTexture3`) verbatim — that's the same convention
    // overlay zones and animation packs use, which keeps the override
    // format identical across categories.
    //
    // Pack-default paths in `effect.textures` are already absolute
    // (resolved + traversal-checked at parseEffect scan time). Runtime
    // overrides from `friendlyParams` may arrive relative — resolve and
    // traversal-check them against `effect.sourceDir` via the SAME helper
    // parseEffect uses, so the downstream cache keys on a stable absolute
    // path regardless of input shape AND a malicious caller can't bypass
    // the scan-time guard by passing `../../../etc/passwd` through a
    // friendlyParams override. Empty / null paths skip the emit so a
    // consumer's "missing key = no change" semantic doesn't accidentally
    // clear a slot the caller intended to leave alone. Wrap-only entries
    // are gated on a non-empty companion path so a consumer can't end up
    // with a wrap mode applied to an unbound sampler.
    for (int slot = 0; slot < SurfaceShaderContract::kMaxUserTextureSlots; ++slot) {
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
                // Empty-string override = explicit clear of the slot. Drop the
                // pack-default wrap too (meaningless without a bound texture),
                // matching the both-or-neither coherence rule the rejection
                // branches below apply; the empty path also skips emit at the
                // path.isEmpty() guard.
                path = candidate;
                wrap.clear();
            } else if (effect.sourceDir.isEmpty()) {
                // Degenerate case — pack came from an in-memory factory
                // with no on-disk anchor. There is no sourceDir to bound
                // the path against, but the override map is still
                // untrusted input — reject any candidate containing `..`
                // segments so a malicious caller can't smuggle a
                // traversal path through the in-memory branch.
                if (!pathHasNoTraversalSegments(candidate)) {
                    qCWarning(lcRegistry).noquote() << "Surface effect" << effect.id << "runtime override texture path"
                                                    << candidate << "rejected (path traversal guard)";
                    path.clear();
                    wrap.clear();
                } else if (QFileInfo(candidate).isRelative()) {
                    // Relative paths in the in-memory branch are caller-
                    // resolved (no sourceDir means no anchor for us to
                    // resolve against). Accept the candidate verbatim and
                    // emit a debug log so a settings-UI tester notices
                    // when an in-memory effect's relative override flows
                    // through unchanged.
                    qCDebug(lcRegistry).noquote()
                        << "Surface effect" << effect.id << "in-memory override texture path is relative:" << candidate
                        << "— passed through caller-resolved (no sourceDir to anchor against)";
                    path = candidate;
                } else {
                    // Absolute path in the in-memory branch — no sourceDir to
                    // bound against, so accepted verbatim per the caller-trusted
                    // policy. Log it (symmetric with the relative branch above)
                    // so a settings-UI tester can see absolute overrides flowing
                    // through unchecked.
                    qCDebug(lcRegistry).noquote()
                        << "Surface effect" << effect.id << "in-memory override texture path is absolute:" << candidate
                        << "— passed through caller-trusted (no sourceDir to bound against)";
                    path = candidate;
                }
            } else {
                const auto validated = validateTexturePathWithinEffectDir(candidate, effect.sourceDir, effect.id);
                if (!validated) {
                    // Rejected override clears BOTH path and wrap for this
                    // slot — same coherence rule parseEffect applies. Skip
                    // the slot entirely so the unbound sampler doesn't end
                    // up with a wrap-only emit.
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
            // Mirror fromJson's wrap-vocabulary guard so a runtime override
            // can't smuggle an unvalidated wrap past the metadata-path check.
            // An empty value clears to clamp; any non-{clamp,repeat,mirror}
            // token is rejected and the wrap clears (clamp) rather than emitting
            // garbage downstream.
            if (candidateWrap.isEmpty() || candidateWrap == QLatin1String("clamp")
                || candidateWrap == QLatin1String("repeat") || candidateWrap == QLatin1String("mirror")) {
                wrap = candidateWrap;
            } else {
                qCWarning(lcRegistry) << "Surface effect" << effect.id << "runtime override wrap value" << candidateWrap
                                      << "rejected (not clamp/repeat/mirror) — falling back to clamp";
                wrap.clear();
            }
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

QVariantMap SurfaceShaderRegistry::translateSurfaceParams(const QString& effectId,
                                                          const QVariantMap& friendlyParams) const
{
    return translateSurfaceParams(effect(effectId), friendlyParams);
}

QString SurfaceShaderRegistry::paramPreamble(const SurfaceShaderEffect& effect)
{
    // Mirror translateSurfaceParams' allocation exactly: color params take
    // the customColors pool, everything else the customParams scalar pool,
    // both auto-numbered in declaration order. buildParamPreamble advances the
    // two pools independently, so the generated macro for each param resolves
    // to the same UBO lane translateSurfaceParams uploads its value to.
    QList<PhosphorShaders::PreambleParam> params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        PhosphorShaders::PreambleParam entry;
        entry.id = p.id;
        entry.pool = (p.type == QLatin1String("color")) ? PhosphorShaders::PreambleParam::Pool::Color
                                                        : PhosphorShaders::PreambleParam::Pool::Scalar;
        entry.explicitSlot = -1; // surface packs always auto-slot by declaration order
        params.append(entry);
    }
    return PhosphorShaders::buildParamPreamble(params);
}

QString SurfaceShaderRegistry::surfaceEntryPrologue()
{
    // `#version` first (GLSL requires it); surface_lib.glsl pulls in the uniform
    // contract plus the shared geometry / composite / focus helpers a pSurface
    // body relies on; then the vertex texcoord in and the fragColor out an
    // entry-only pack no longer declares by hand. A pack needing the noise or
    // colour modules still `#include`s those in its own body (appended after
    // this prologue, before include expansion).
    return QStringLiteral(
        "#version 450\n"
        "#include <surface_lib.glsl>\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 0) out vec4 fragColor;\n");
}

QList<PhosphorShaders::EntryCandidate> SurfaceShaderRegistry::surfaceEntryCandidates()
{
    // pSurface: the whole-fragment entry. The body returns the final
    // premultiplied colour for the fragment at `uv` (the vertex texcoord); the
    // generated main() just writes it to fragColor. Single candidate — surface
    // packs have no per-element loop (that is the overlay category's pZone).
    static const QString surfaceMain = QStringLiteral(
        "void main() {\n"
        "    fragColor = pSurface(vTexCoord);\n"
        "}\n");
    return {PhosphorShaders::EntryCandidate{QStringLiteral("pSurface"), surfaceMain}};
}

} // namespace PhosphorSurfaceShaders
