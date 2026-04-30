// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderContract.h>
#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QDir>
#include <QJsonObject>
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

    int floatSlot = 0; // sub-slot index in [0, 32) — fills customParams1_x..customParams8_w
    for (const auto& param : effect.parameters) {
        const QString& type = param.type;
        // Color parameters would consume customColor<N> rather than a
        // float sub-slot; not currently used by any built-in animation
        // shader. When the first one lands, extend this branch to map
        // color → "customColor<N>" with its own counter. Skipped silently
        // here — surfacing this on every transition begin would just spam
        // the log; loader-time validation is the right level for such a
        // schema-mismatch warning, and there is none today because no
        // built-in pack declares a color-typed animation param.
        if (type == QLatin1String("color")) {
            continue;
        }

        if (floatSlot >= AnimationShaderContract::kMaxParameterSlots) {
            qWarning(lcRegistry) << "translateAnimationParams: effect" << effect.id << "exceeds"
                                 << AnimationShaderContract::kMaxParameterSlots
                                 << "-slot customParams budget; dropping param" << param.id;
            continue;
        }

        const int vecIndex = floatSlot / 4; // 0..7
        const int compIndex = floatSlot % 4; // 0..3 → x/y/z/w
        static const char component[] = {'x', 'y', 'z', 'w'};
        const QString uniformKey = AnimationShaderContract::slotKey(vecIndex, component[compIndex]);

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
// (see `data/animations/_shared/animation_uniforms.glsl`). Runtimes
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

        // Drop fields that are not part of the animation contract on the
        // classic-GL path. KWin manages its own scene-graph transform /
        // opacity; `_appField0` / `_appField1` are pure padding that
        // exists only to keep the std140 alignment in sync with the
        // daemon's BaseUniforms.
        if (decl.contains(QLatin1String("qt_Matrix")) || decl.contains(QLatin1String("qt_Opacity"))
            || decl.contains(QLatin1String("_appField0")) || decl.contains(QLatin1String("_appField1"))) {
            continue;
        }

        output.append(QStringLiteral("uniform %1;").arg(decl));
    }
    return output.join(QLatin1Char('\n')).toUtf8();
}

} // namespace PhosphorAnimationShaders
