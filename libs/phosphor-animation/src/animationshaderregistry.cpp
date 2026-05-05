// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
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
    if (!effect.isValid() || effect.parameters.isEmpty()) {
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

    return result;
}

QVariantMap AnimationShaderRegistry::translateAnimationParams(const QString& effectId,
                                                              const QVariantMap& friendlyParams) const
{
    return translateAnimationParams(effect(effectId), friendlyParams);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Canonical UBO → default-block uniform rewrite
// ═══════════════════════════════════════════════════════════════════════════════
//
// Animation shaders ship as `#version 450` GLSL with a canonical
// `layout(std140, binding = 0) uniform AnimationUniforms { ... };` block
// (see `data/animations/shared/animation_uniforms.glsl`). Runtimes
// without UBO-binding support — `KWin::GLShader`'s `setUniform(loc, val)`
// API addresses default-block uniforms only — need the source rewritten
// before compile. Daemon Qt-RHI / SPIR-V keeps the canonical form and
// uploads the UBO directly, so it doesn't call this helper.
//
// The rewriter is deliberately line-based and pinned to the canonical
// layout (open-brace on the same line as `uniform NAME`, close-brace
// `};` on its own line). This is exactly what
// `animation_uniforms.glsl` produces; an author writing a divergent
// declaration would slip past it. The bake test
// (`tests/unit/ui/test_animation_shader_bake.cpp`) catches GLSL
// regressions on the daemon side; the registry test suite
// (`tests/test_animationshaderregistry.cpp`) pins the rewriter
// behaviour for the kwin side.

QByteArray AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock(const QString& expandedShaderSource)
{
    QStringList output;
    const QStringList lines = expandedShaderSource.split(QLatin1Char('\n'));
    bool inUboBlock = false;
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!inUboBlock) {
            // Detect `layout(std140, binding = N) uniform NAME {` (any
            // whitespace tolerated). We only handle the same-line open
            // brace; the canonical animation_uniforms.glsl writes it
            // that way, and an author deviating would be caught by the
            // bake test on the daemon side anyway.
            const bool isUboOpen = trimmed.startsWith(QLatin1String("layout(std140"))
                && trimmed.contains(QLatin1String("uniform")) && trimmed.endsWith(QLatin1Char('{'));
            if (isUboOpen) {
                inUboBlock = true;
                continue; // drop the opening line
            }
            output.append(line);
            continue;
        }
        // Inside the UBO block. Strip a trailing `// comment` from the
        // closing brace as well as field declarations so authors can
        // annotate the canonical header without breaking the rewriter.
        QString workingLine = line;
        const int commentIdx = workingLine.indexOf(QLatin1String("//"));
        if (commentIdx >= 0) {
            workingLine = workingLine.left(commentIdx);
        }
        const QString workingTrimmed = workingLine.trimmed();
        if (workingTrimmed == QLatin1String("};")) {
            inUboBlock = false;
            continue; // drop the closing line
        }
        // Skip lines that are pure-comment / blank — they have no field
        // declaration to translate.
        if (workingTrimmed.isEmpty() || !workingTrimmed.endsWith(QLatin1Char(';'))) {
            continue;
        }
        QString decl = workingTrimmed;
        decl.chop(1); // remove trailing ';'

        // Extract the field name (the last whitespace-separated token,
        // stripped of any `[N]` array suffix). A canonical declaration
        // like `vec4 customParams[8]` yields `customParams`; `mat4
        // qt_Matrix` yields `qt_Matrix`. Token-level matching is
        // future-proof against a contract that adds e.g. `iFlipBufferYAdjusted`
        // — substring matching would silently nuke that too. The
        // `lastIndexOf(' ')` split also tolerates `highp vec4 ...`-style
        // declarations: only the final token (the identifier) is matched.
        const int lastSpace = decl.lastIndexOf(QLatin1Char(' '));
        QString fieldName = (lastSpace >= 0) ? decl.mid(lastSpace + 1) : decl;
        const int bracketIdx = fieldName.indexOf(QLatin1Char('['));
        if (bracketIdx >= 0) {
            fieldName = fieldName.left(bracketIdx);
        }

        // Drop fields that are not part of the animation contract on the
        // classic-GL path. KWin manages its own scene-graph transform /
        // opacity; `_appField0/1` are daemon-side escape-hatch padding;
        // `iFlipBufferY` (daemon-only, always 1) is stripped. All other
        // fields emit as default-block uniforms. The canonical
        // animation_uniforms.glsl deliberately carries no explicit
        // `_pad*` declarations — std140's natural vec4-alignment of the
        // next array fills the gaps (see the layout comment in that
        // file) — so no `_pad*` strip entry is needed here.
        if (fieldName == QLatin1String("qt_Matrix") || fieldName == QLatin1String("qt_Opacity")
            || fieldName == QLatin1String("_appField0") || fieldName == QLatin1String("_appField1")
            || fieldName == QLatin1String("iFlipBufferY")) {
            continue;
        }

        output.append(QStringLiteral("uniform %1;").arg(decl));
    }
    if (inUboBlock) {
        qCWarning(lcRegistry) << "rewriteCanonicalUboToDefaultBlock: UBO block was never closed with '};'"
                                 " — returning original source unchanged";
        return expandedShaderSource.toUtf8();
    }

    // Strip `layout(binding = N)` decorations from sampler uniform
    // declarations on the classic-GL path. The daemon-side RHI pipeline
    // uses explicit binding-points (`iChannel0` lives at SRB binding 7
    // in the canonical contract) but KWin's `OffscreenData::paint`
    // binds the redirected window texture to the default active unit
    // (`m_texture->bind()` with no `glActiveTexture` call → TEXTURE0).
    // With `#version 450`, `layout(binding = 7)` pins the sampler to
    // unit 7 at link time and KWin's bind to unit 0 is invisible to
    // the shader — the texture sample returns transparent and the
    // transition is a see-through no-op.
    //
    // Dropping the decoration alone leaves the sampler at GL's default
    // (unit 0), which matches KWin's bind. The daemon-side RHI path
    // never sees this rewrite (only the kwin-effect calls this helper),
    // so the canonical binding-point contract on `iChannel0` /
    // BaseUniforms is preserved on that runtime.
    QString result = output.join(QLatin1Char('\n'));
    static const QRegularExpression kSamplerBindingDecoration(
        QStringLiteral(R"(layout\s*\(\s*binding\s*=\s*\d+\s*\)\s*(uniform\s+sampler))"));
    result.replace(kSamplerBindingDecoration, QStringLiteral("\\1"));
    return result.toUtf8();
}

} // namespace PhosphorAnimationShaders
