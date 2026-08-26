// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The zone/overlay arm of plasmazones-shader-validate — see packvalidators.h.
// Reproduces the zone runtime's GLSL assembly (zone entry scaffold + generated
// p_<id> preamble + include expansion) and bakes every stage through headless
// glslang. Shared compile/report helpers live in packvalidatorcommon.h.
//
// ONE FILE PER AUTHORING MODEL. Each arm mirrors one runtime's assembly order
// and that runtime's own set of silently-coerced metadata fields, so the three
// read as three narratives rather than one file switching families every few
// hundred lines. Split when the combined file passed the hard size ceiling;
// the seam is by family rather than by lint kind because the lint kinds that
// genuinely generalise belong in packvalidatorcommon instead.

#include "packvalidators.h"

#include "packvalidatorcommon.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <rhi/qshader.h>

using PhosphorRendering::ShaderCompiler;
using PhosphorShaders::ShaderRegistry;

namespace PlasmaZones::ShaderValidate {

// Validate one pack directory. Returns the number of errors found.
int validatePack(const QString& packDir, QTextStream& out)
{
    const QString name = QFileInfo(packDir).fileName();

    QString parseErr;
    ShaderRegistry::ShaderInfo info = ShaderRegistry::parsePackMetadata(packDir, &parseErr);
    if (!parseErr.isEmpty()) {
        out << name << "\n  metadata       ERROR\n    " << parseErr << "\n  → 1 error\n\n";
        return 1;
    }

    // Several lints below deliberately read the RAW metadata rather than the
    // parsed ShaderInfo (parsePackMetadata clamps bufferScale, clears missing
    // buffer paths, and only sets vertexShaderPath when the file exists, so a
    // lint reading the parsed struct would silently pass the author error the
    // runtime hid). Read and parse metadata.json ONCE here and share it, instead
    // of reopening the same file per lint block.
    // Checked on both arms: an empty rawRoot silently no-ops EVERY raw-metadata
    // lint below (the declared-vertexShader check, the duplicate parameter id
    // check, the buffer name and bufferScale checks), so the pack would report
    // clean having had those checks skipped rather than passed. parsePackMetadata
    // above rejects unparseable JSON, so the live path here is an open failure,
    // but a lint block that can quietly evaluate against nothing is worth
    // closing whichever way it is reached.
    QJsonObject rawRoot;
    {
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (!metaFile.open(QIODevice::ReadOnly)) {
            out << name << "\n  metadata       ERROR\n    cannot read metadata.json: " << metaFile.errorString()
                << "\n  → 1 error\n\n";
            return 1;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            out << name << "\n  metadata       ERROR\n    "
                << (parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                 : QStringLiteral("not a JSON object"))
                << "\n  → 1 error\n\n";
            return 1;
        }
        rawRoot = doc.object();
    }

    // The `fragmentShader` / `bufferShaders` / `vertexShader` paths come from
    // the user-editable metadata.json. parsePackMetadata already confines them
    // to the pack dir (resolveWithinPack), so this re-check is belt-and-braces
    // defense in depth before each path is opened and fed to glslang — the
    // same guard the animation and surface validators apply. Zone packs have
    // no `builtin:` buffer tokens (that resolver is surface-only), so every
    // path must stay inside the pack. An empty path is left as-is (that stage
    // is simply absent).
    const auto confineToPack = [&packDir](QString& path) {
        return confinePackPathInPlace(packDir, path);
    };
    if (!confineToPack(info.sourcePath)) {
        out << name
            << "\n  metadata       ERROR\n    fragmentShader path escapes the pack directory (path traversal "
               "rejected)\n  → 1 error\n\n";
        return 1;
    }
    for (QString& buf : info.bufferShaderPaths) {
        if (!confineToPack(buf)) {
            out << name
                << "\n  metadata       ERROR\n    bufferShaders path escapes the pack directory (path traversal "
                   "rejected)\n  → 1 error\n\n";
            return 1;
        }
    }
    if (!confineToPack(info.vertexShaderPath)) {
        out << name
            << "\n  metadata       ERROR\n    vertexShader path escapes the pack directory (path traversal rejected)\n "
               " "
               "→ 1 error\n\n";
        return 1;
    }

    out << name << "  (" << info.parameters.size() << " param" << (info.parameters.size() == 1 ? "" : "s") << ", "
        << (info.isMultipass ? "multipass" : "single-pass") << ")\n";

    int errors = 0;

    // ── metadata lints ──
    QStringList lints;
    QHash<QString, QString> claimedLane; // "pool#slot" → first param id, for collision detection
    for (const ShaderRegistry::ParameterInfo& p : info.parameters) {
        if (!kValidParamTypes.contains(p.type)) {
            lints << QStringLiteral("unknown param type '%1' for '%2'").arg(p.type, p.id);
        }
        // An id that isn't a valid GLSL identifier gets no p_ define and no lane
        // (parseShaderMetadata leaves its slot at -1) — surface the real fault, and
        // skip the collision check so two such params don't false-collide on "-1".
        if (!PhosphorShaders::isValidParamId(p.id)) {
            lints
                << QStringLiteral("invalid parameter id '%1' (not a GLSL identifier; skipped, no p_ define)").arg(p.id);
            continue;
        }
        const QString laneKey = poolName(p.type) + QStringLiteral("#") + QString::number(p.slot);
        if (claimedLane.contains(laneKey)) {
            lints << QStringLiteral("slot collision: '%1' and '%2' both map to %3 lane %4")
                         .arg(claimedLane.value(laneKey), p.id, poolName(p.type))
                         .arg(p.slot);
        } else {
            claimedLane.insert(laneKey, p.id);
        }
        // Mirror the pool budgets the runtime binds against: a slot past its
        // pool's ceiling gets no p_ define and no upload, so the shader
        // compiles against an undefined identifier.
        const int budget = p.type == QLatin1String("color") ? PhosphorShaders::CustomColors::kColorCount
            : p.type == QLatin1String("image")              ? PhosphorShaders::kMaxImageSlots
                                                            : PhosphorShaders::CustomParams::kFlatSlotCount;
        if (p.slot >= budget) {
            lints << QStringLiteral("slot %1 past the %2-pool budget of %3 for '%4' (will not bind)")
                         .arg(p.slot)
                         .arg(poolName(p.type))
                         .arg(budget)
                         .arg(p.id);
        }
    }
    // Per-pool count overflow, independent of how slots were assigned. The
    // per-param `slot >= budget` lint above only fires for params that LAND on
    // an out-of-range lane; it misses the case where more params request a pool
    // than it has lanes but they pile onto in-budget slots (explicit collisions,
    // or a future change to the auto-assigner). Count the lane-consuming params
    // per pool (valid id, that pool's type) and flag a pool that is oversubscribed
    // as a whole, so the author sees the real cause rather than only the symptom.
    {
        int scalarCount = 0, colorCount = 0, imageCount = 0;
        for (const ShaderRegistry::ParameterInfo& p : info.parameters) {
            if (!PhosphorShaders::isValidParamId(p.id) || !kValidParamTypes.contains(p.type)) {
                continue;
            }
            if (p.type == QLatin1String("color")) {
                ++colorCount;
            } else if (p.type == QLatin1String("image")) {
                ++imageCount;
            } else {
                ++scalarCount;
            }
        }
        const auto flagPool = [&lints](const QString& pool, int count, int budget) {
            if (count > budget) {
                lints << QStringLiteral(
                             "%1 pool oversubscribed: %2 parameters for %3 lanes (the last %4 will not bind)")
                             .arg(pool)
                             .arg(count)
                             .arg(budget)
                             .arg(count - budget);
            }
        };
        flagPool(poolName(QStringLiteral("float")), scalarCount, PhosphorShaders::CustomParams::kFlatSlotCount);
        flagPool(poolName(QStringLiteral("color")), colorCount, PhosphorShaders::CustomColors::kColorCount);
        flagPool(poolName(QStringLiteral("image")), imageCount, PhosphorShaders::kMaxImageSlots);
    }
    // Buffer-pass + bufferScale lints check the RAW metadata, not the parsed
    // ShaderInfo: parseShaderMetadata clamps bufferScale into [0.125, 1.0] and
    // clears bufferShaderPaths when a declared buffer is missing, so a lint reading
    // the parsed values would silently pass an author error the runtime hid.
    //
    // The GATE is raw for the same reason, and it has to be: parseShaderMetadata
    // sets info.isMultipass = false as its fail-closed response to an
    // unresolvable buffer entry, which is precisely the defect these lints
    // report. Gating on the PARSED flag meant the block was skipped for every
    // pack that needed it and ran only for packs that were already fine.
    //
    // Raw `multipass` alone, and not "declares any bufferShaders": that key is
    // the only thing parseShaderMetadata (shaderregistry_parse.cpp) ever sets
    // isMultipass TRUE from, so reading it here opens the block for the packs the
    // runtime treats as multipass, including the ones whose parse then
    // fail-closed. A pack that lists bufferShaders WITHOUT multipass is inert
    // at runtime — the buffers are never resolved or baked — so linting its
    // buffer list would fail a pack for a declaration nothing acts on.
    if (rawRoot.value(QLatin1String("multipass")).toBool(false)) {
        const QJsonObject& root = rawRoot;

        const QJsonArray declared = root.value(QLatin1String("bufferShaders")).toArray();
        QStringList bufferNames;
        if (declared.isEmpty()) {
            // The implicit fallback keys off the RAW array being absent, the
            // way parseShaderMetadata's bufferShadersDeclared does. Keying it
            // off the non-empty SUBSET instead made `"bufferShaders": [""]`
            // lint a buffer.frag the runtime never looks at, while saying
            // nothing about the empty entry that is the actual defect.
            bufferNames << root.value(QLatin1String("bufferShader")).toString(QStringLiteral("buffer.frag"));
        } else {
            for (const QJsonValue& v : declared) {
                bufferNames << v.toString();
            }
        }
        for (const QString& bufName : bufferNames) {
            if (bufName.isEmpty()) {
                // resolveWithinPack returns empty for an empty name before it
                // reaches the traversal guard, and the caller treats an empty
                // resolve as fail-closed: it clears the WHOLE bufferShaderPaths
                // list and turns multipass off for the entire pack. Silent
                // without this lint, and the warning the runtime does log
                // misattributes it to a path escape.
                lints << QStringLiteral(
                    "empty bufferShaders entry (resolves empty at load, which drops the whole bufferShaders "
                    "list and disables multipass for the pack)");
                continue;
            }
            const auto confined = confinedPackPath(packDir, bufName);
            if (!confined) {
                lints << QStringLiteral(
                             "multipass buffer shader path escapes the pack directory: %1 (drops the "
                             "whole bufferShaders list and disables multipass for the pack)")
                             .arg(bufName);
            } else if (!QFile::exists(*confined)) {
                lints << QStringLiteral(
                             "multipass buffer shader missing: %1 (drops the whole bufferShaders list "
                             "and disables multipass for the pack)")
                             .arg(bufName);
            }
        }

        // The runtime caps buffer passes at 4 (parseShaderMetadata's qMin) and
        // drops the surplus with only a journal warning — exactly the
        // "runtime hid the author error" class this block lints for.
        // Compare the RAW declared array against the cap, not the non-empty
        // subset: parseShaderMetadata iterates qMin(rawSize, kMaxBufferPasses)
        // and only then skips empties, so ["", "a", "b", "c", "d"] silently
        // loses "d".
        if (declared.size() > PhosphorShaders::kMaxBufferPasses) {
            lints << QStringLiteral("too many buffer shaders: %1 declared, cap is %2 (surplus dropped at load)")
                         .arg(static_cast<int>(declared.size()))
                         .arg(PhosphorShaders::kMaxBufferPasses);
        }

        const double rawScale = root.value(QLatin1String("bufferScale")).toDouble(1.0);
        if (rawScale < PhosphorShaders::kMinBufferScale || rawScale > PhosphorShaders::kMaxBufferScale) {
            lints << QStringLiteral("bufferScale out of range [%1, %2]: %3 (clamped at load)")
                         .arg(PhosphorShaders::kMinBufferScale)
                         .arg(PhosphorShaders::kMaxBufferScale)
                         .arg(rawScale);
        }

        // Wrap/filter vocabulary and alignment, the lints the animation and
        // surface arms already carry. This arm needs them MOST: normalizeWrapMode
        // and normalizeFilterMode map an unrecognised token to clamp/linear with
        // no warning at any layer, and parseShaderMetadata pads a short array
        // with the single-value default and trims a long one, also silently. The
        // sibling runtimes at least log.
        const auto lintTokens = [&lints](const QJsonArray& arr, const QString& field, bool wrap) {
            for (const QJsonValue& v : arr) {
                const QString tok = v.toString();
                const bool ok =
                    wrap ? PhosphorShaders::isValidWrapToken(tok) : PhosphorShaders::isValidFilterToken(tok);
                if (!tok.isEmpty() && !ok) {
                    lints << QStringLiteral("%1 value '%2' not in vocabulary (coerced at load)").arg(field, tok);
                }
            }
        };
        const QJsonArray wrapsArr = root.value(QLatin1String("bufferWraps")).toArray();
        const QJsonArray filtersArr = root.value(QLatin1String("bufferFilters")).toArray();
        lintTokens(wrapsArr, QStringLiteral("bufferWraps"), true);
        lintTokens(filtersArr, QStringLiteral("bufferFilters"), false);
        const auto lintLength = [&lints, &declared](const QJsonArray& arr, const QString& field) {
            if (!arr.isEmpty() && arr.size() != declared.size()) {
                lints << QStringLiteral(
                             "%1 has %2 entries for %3 buffer shaders (aligned positionally; "
                             "surplus dropped and missing entries fall back at load)")
                             .arg(field)
                             .arg(static_cast<int>(arr.size()))
                             .arg(static_cast<int>(declared.size()));
            }
        };
        lintLength(wrapsArr, QStringLiteral("bufferWraps"));
        lintLength(filtersArr, QStringLiteral("bufferFilters"));
        const QString singleWrap = root.value(QLatin1String("bufferWrap")).toString();
        if (!singleWrap.isEmpty() && !PhosphorShaders::isValidWrapToken(singleWrap)) {
            lints << QStringLiteral("bufferWrap value '%1' not in vocabulary (coerced at load)").arg(singleWrap);
        }
        const QString singleFilter = root.value(QLatin1String("bufferFilter")).toString();
        if (!singleFilter.isEmpty() && !PhosphorShaders::isValidFilterToken(singleFilter)) {
            lints << QStringLiteral("bufferFilter value '%1' not in vocabulary (coerced at load)").arg(singleFilter);
        }
    }
    if (!QFile::exists(info.sourcePath)) {
        lints << QStringLiteral("fragment shader missing: %1").arg(QFileInfo(info.sourcePath).fileName());
    }
    // A declared-but-absent vertexShader silently falls back to the shared
    // zone.vert below, hiding the author's typo. Read the RAW metadata (shared
    // rawRoot above): unlike the animation and surface parsers,
    // ShaderRegistry::parsePackMetadata only assigns vertexShaderPath when the
    // file EXISTS, so linting the parsed struct could never fire.
    {
        const QString declaredVert = rawRoot.value(QLatin1String("vertexShader")).toString();
        if (!declaredVert.isEmpty() && !QFile::exists(QDir(packDir).filePath(declaredVert))) {
            lints << QStringLiteral("vertex shader missing: %1").arg(declaredVert);
        }
        // A custom-named vertexShader is silently ignored by the zone
        // runtime, which only ever loads a file named `zone.vert` (see the
        // vertex-resolution note below). Warn so the author is not misled by
        // a green validator into thinking their custom vertex stage runs.
        if (!declaredVert.isEmpty() && QFileInfo(declaredVert).fileName() != QLatin1String("zone.vert")) {
            lints << QStringLiteral(
                         "vertexShader '%1' is ignored at load: the zone runtime only uses a file named "
                         "zone.vert (sibling, else shared). Rename it to zone.vert or drop the declaration.")
                         .arg(declaredVert);
        }
        // Duplicate ids must be linted from the RAW array: the parser
        // dedupes (first declaration wins), so the parsed struct can
        // never show the author their repeated id.
        QSet<QString> rawIds;
        const QJsonArray rawParams = rawRoot.value(QLatin1String("parameters")).toArray();
        for (const QJsonValue& v : rawParams) {
            const QString rawId = v.toObject().value(QLatin1String("id")).toString();
            if (rawId.isEmpty()) {
                continue;
            }
            if (rawIds.contains(rawId)) {
                lints << QStringLiteral("duplicate parameter id '%1' (first declaration wins at load)").arg(rawId);
            } else {
                rawIds.insert(rawId);
            }
        }
    }

    if (lints.isEmpty()) {
        out << "  metadata       OK\n";
    } else {
        out << "  metadata       ERROR\n";
        for (const QString& l : lints) {
            out << "    " << l << "\n";
            ++errors;
        }
    }

    // ── stage compiles (reproduce the runtime assembly) ──
    const QString packsRoot = QFileInfo(packDir).absolutePath();
    const QStringList includePaths = {packsRoot + QStringLiteral("/shared"), packsRoot};
    const QString preamble = ShaderRegistry::paramPreamble(info);

    if (QFile::exists(info.sourcePath)) {
        // Label by the actual fragment filename — parsePackMetadata honours a
        // custom `fragmentShader` field (default effect.frag), so the stage label
        // tracks the real file rather than assuming effect.frag.
        errors += compileStage(out, QFileInfo(info.sourcePath).fileName(), info.sourcePath, QShader::FragmentStage,
                               includePaths, /*useScaffold=*/true, preamble, info);
    }
    // Only multipass packs bake buffer passes — parseShaderMetadata clears
    // buffer state for single-pass packs and only keeps an implicit
    // buffer.frag that exists on disk, and the runtime (bakeBufferShaders)
    // ignores buffer paths unless isMultipass, so the validator gates
    // identically.
    if (info.isMultipass) {
        for (const QString& buf : info.bufferShaderPaths) {
            if (QFile::exists(buf)) {
                errors += compileStage(out, QFileInfo(buf).fileName(), buf, QShader::FragmentStage, includePaths,
                                       /*useScaffold=*/false, QString(), info);
            }
        }
    }
    // Vertex: resolve EXACTLY as the zone runtime does (resolveZoneVertexPath in
    // zoneshadernoderhi.h) rather than honouring `info.vertexShaderPath`. The
    // zone runtime — both ZoneShaderItem and the daemon warm bake — only ever
    // looks for a file literally named `zone.vert` (the pack's sibling, else the
    // shared copy) and NEVER compiles a custom-named `vertexShader` declaration,
    // unlike the animation and surface runtimes. Baking `info.vertexShaderPath`
    // here would compile a stage the runtime never touches, so a pack with a
    // broken custom vert would pass CI while its real (shared) vertex stage went
    // unchecked. So resolve the sibling zone.vert, else the shared zone.vert.
    QString vertPath = QFileInfo(info.sourcePath).absolutePath() + QStringLiteral("/zone.vert");
    if (!QFile::exists(vertPath)) {
        vertPath = packsRoot + QStringLiteral("/shared/zone.vert");
    }
    if (QFile::exists(vertPath)) {
        errors += compileStage(out, QFileInfo(vertPath).fileName(), vertPath, QShader::VertexStage, includePaths,
                               /*useScaffold=*/false, QString(), info);
    }

    if (errors == 0) {
        out << "  → OK\n\n";
    } else {
        out << "  → " << errors << (errors == 1 ? " error\n\n" : " errors\n\n");
    }
    return errors;
}

} // namespace PlasmaZones::ShaderValidate
