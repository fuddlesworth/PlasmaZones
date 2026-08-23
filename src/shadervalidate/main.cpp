// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plasmazones-shader-validate — offline validator for shader packs (T1.2).
//
// A `luau-analyze` equivalent for GLSL packs: it parses metadata.json with the
// SAME parser the runtime uses, reproduces the runtime's GLSL assembly (entry
// scaffold + generated p_<id> preamble + include expansion), and bakes every
// stage through headless glslang (QShaderBaker, CPU-only — no GPU, no Wayland,
// no compositor). It is the CI gate for the bundled sets and a pre-commit-
// friendly tool for pack authors.
//
// Three authoring models. Each pack's model is DETECTED from the marker header
// in its sibling `shared/` directory (see detectPackModel); --overlay /
// --animation / --surface force one model for every path given, for a pack
// tree that carries no shared/ dir of its own. Detection is the default
// because --overlay used to be, so validating an animation pack without
// remembering the flag reported wrong-validator artifacts as pack errors:
//   • zone/overlay packs (--overlay, the default, data/overlays/*):
//     ShaderRegistry::parsePackMetadata + the zone entry scaffold (pZone/pImage);
//     validates the frag, multipass buffer passes, and the vertex stage on the
//     daemon Qt-RHI path.
//   • animation/transition packs (--animation, data/animations/*):
//     AnimationShaderEffect + the animation entry scaffold (pTransition / pIn+pOut)
//     + paramPreamble; validates effect.frag on the daemon Qt-RHI path.
//     Compositor-only packs are baked out of process through glslang instead,
//     since their kwin classic-GL source is a dialect the SPIR-V target
//     rejects by design (see validateAnimationPack).
//   • surface/decoration packs (--surface, data/surface/*):
//     SurfaceShaderEffect + paramPreamble; validates effect.frag, buffer
//     passes, and the shared vertex stage on the daemon Qt-RHI path — see
//     validateSurfacePack.
//
// Usage:
//   plasmazones-shader-validate [--quiet] [--overlay|--animation|--surface]
//                               [--emit-preamble] [--] <path> [<path> ...]
// where each <path> is either a pack directory (contains metadata.json) or a
// root that holds pack subdirectories. Exits non-zero if any pack has an error.

#include "packvalidatorcommon.h"
#include "packvalidators.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <optional>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorShaders::ShaderIncludeResolver;
using PhosphorShaders::ShaderRegistry;
using PhosphorSurfaceShaders::SurfaceShaderEffect;
using PhosphorSurfaceShaders::SurfaceShaderRegistry;
using namespace PlasmaZones::ShaderValidate;

namespace {

bool isPackDir(const QString& dir)
{
    return QFile::exists(QDir(dir).filePath(QStringLiteral("metadata.json")));
}

// Write the generated `p_<id>` autocomplete sidecar for one pack (T2.2). The
// sidecar (p_generated.glsl) is an editor-only aid: an author #includes it for
// glslls / glsl-language-server autocomplete, and the include resolver skips it
// at load (ShaderIncludeResolver::GeneratedPreambleInclude), so it neither ships
// nor affects the compiled shader. Returns 0 on success, 1 on error.
int emitPreamble(const QString& packDir, PackModel model, bool quiet, QTextStream& out, QTextStream& errStream)
{
    const QString name = QFileInfo(packDir).fileName();
    QString preamble;
    // glslls needs the UBO declarations the p_<id> defines reference, so the
    // sidecar pulls in the right base header per authoring model.
    QString baseHeader;

    if (model == PackModel::Surface) {
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (!metaFile.open(QIODevice::ReadOnly)) {
            errStream << name << ": cannot read metadata.json\n";
            return 1;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        if (!doc.isObject()) {
            errStream << name << ": invalid metadata.json\n";
            return 1;
        }
        SurfaceShaderEffect eff = SurfaceShaderEffect::fromJson(doc.object());
        eff.sourceDir = QDir(packDir).absolutePath();
        if (!eff.isValid()) {
            errStream << name << ": invalid metadata.json (missing required field id / fragmentShader)\n";
            return 1;
        }
        preamble = SurfaceShaderRegistry::paramPreamble(eff);
        baseHeader = QStringLiteral("surface_uniforms.glsl");
    } else if (model == PackModel::Animation) {
        QFile metaFile(QDir(packDir).filePath(QStringLiteral("metadata.json")));
        if (!metaFile.open(QIODevice::ReadOnly)) {
            errStream << name << ": cannot read metadata.json\n";
            return 1;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        if (!doc.isObject()) {
            errStream << name << ": invalid metadata.json\n";
            return 1;
        }
        AnimationShaderEffect eff = AnimationShaderEffect::fromJson(doc.object());
        eff.sourceDir = QDir(packDir).absolutePath();
        // Mirror the validate path: reject metadata missing the required id /
        // fragmentShader fields rather than silently emitting a sidecar from a
        // half-parsed pack. (isValid() checks field presence, not file
        // existence, so emit-preamble-before-writing-the-shader still works.)
        if (!eff.isValid()) {
            errStream << name << ": invalid metadata.json (missing required field id / fragmentShader)\n";
            return 1;
        }
        preamble = AnimationShaderRegistry::paramPreamble(eff);
        baseHeader = QStringLiteral("animation_uniforms.glsl");
    } else {
        QString parseErr;
        const ShaderRegistry::ShaderInfo info = ShaderRegistry::parsePackMetadata(packDir, &parseErr);
        if (!parseErr.isEmpty()) {
            errStream << name << ": " << parseErr << "\n";
            return 1;
        }
        preamble = ShaderRegistry::paramPreamble(info);
        baseHeader = QStringLiteral("common.glsl");
    }

    const QLatin1String sidecarName(ShaderIncludeResolver::GeneratedPreambleInclude);
    QString sidecar;
    QTextStream s(&sidecar);
    s << "// GENERATED by `plasmazones-shader-validate --emit-preamble` — DO NOT EDIT.\n"
      << "//\n"
      << "// Editor-only autocomplete aid for the `p_<id>` named parameters. Add\n"
      << "//     #include \"" << sidecarName << "\"\n"
      << "// near the top of your shader so glslls / glsl-language-server resolves\n"
      << "// p_<id>. The loader skips this include at runtime — the real preamble is\n"
      << "// generated then — so it neither ships nor affects the compiled shader.\n"
      << "// Re-run --emit-preamble after you change the pack's parameters.\n"
      << "#include <" << baseHeader << ">\n"
      << "\n"
      << (preamble.isEmpty() ? QStringLiteral("// (this pack declares no named parameters)\n") : preamble);
    s.flush();

    const QString sidecarPath = QDir(packDir).filePath(sidecarName);
    QFile f(sidecarPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errStream << name << ": cannot write " << sidecarPath << ": " << f.errorString() << "\n";
        return 1;
    }
    // Checked: a short write leaves a TRUNCATED sidecar that the line below
    // would still report as "wrote".
    const QByteArray encoded = sidecar.toUtf8();
    if (f.write(encoded) != encoded.size()) {
        errStream << name << ": short write to " << sidecarPath << ": " << f.errorString() << "\n";
        return 1;
    }
    if (!quiet) {
        out << "wrote " << sidecarPath << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QTextStream out(stdout);
    QTextStream errStream(stderr);

    QStringList args;
    bool quiet = false; // --quiet/-q: print only failing packs (clean pre-commit output)
    // System selection: --overlay vs --animation vs --surface, each FORCING one
    // authoring model for every path given. With none of them the model is
    // detected per pack from its shared/ directory (detectPackModel), which is
    // the default because the old default silently forced `--overlay` and
    // reported wrong-validator artifacts as pack errors. Later flag wins if
    // more than one is given.
    std::optional<PackModel> forcedModel;
    // --emit-preamble: don't validate — write each pack's `p_<id>` autocomplete
    // sidecar (T2.2) for editor tooling.
    bool emitMode = false;
    bool endOfOptions = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        // Everything after a bare `--` is a path, so a pack directory literally
        // named `--surface` (or any other flag spelling) stays reachable.
        if (endOfOptions) {
            args << a;
            continue;
        }
        if (a == QLatin1String("--")) {
            endOfOptions = true;
        } else if (a == QLatin1String("--quiet") || a == QLatin1String("-q")) {
            quiet = true;
        } else if (a == QLatin1String("--animation") || a == QLatin1String("-a")) {
            forcedModel = PackModel::Animation;
        } else if (a == QLatin1String("--surface") || a == QLatin1String("-s")) {
            forcedModel = PackModel::Surface;
        } else if (a == QLatin1String("--overlay") || a == QLatin1String("-o")) {
            forcedModel = PackModel::Overlay;
        } else if (a == QLatin1String("--emit-preamble")) {
            emitMode = true;
        } else {
            args << a;
        }
    }
    if (args.isEmpty()) {
        errStream << "usage: plasmazones-shader-validate [--quiet] [--overlay|--animation|--surface] "
                     "[--emit-preamble] [--] <pack-dir-or-root> [...]\n"
                  << "  (no model flag)   detect each pack's model from its shared/ dir  [default]\n"
                  << "  --overlay         force zone/overlay packs (data/overlays/*)\n"
                  << "  --animation       force transition/animation packs (data/animations/*)\n"
                  << "  --surface         force surface-layer packs (data/surface/*)\n"
                  << "  --emit-preamble   write each pack's p_generated.glsl autocomplete sidecar (no validation)\n";
        return 2;
    }

    // Expand each argument into a list of pack directories: a pack dir is taken as
    // itself; anything else is treated as a root and scanned one level deep.
    QStringList packs;
    for (const QString& arg : args) {
        const QFileInfo fi(arg);
        if (!fi.exists() || !fi.isDir()) {
            errStream << "not a directory: " << arg << "\n";
            return 2;
        }
        if (isPackDir(arg)) {
            packs << QDir(arg).absolutePath();
            continue;
        }
        const QStringList subdirs = QDir(arg).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& sub : subdirs) {
            const QString subPath = QDir(arg).filePath(sub);
            if (isPackDir(subPath)) {
                packs << QDir(subPath).absolutePath();
            }
        }
    }

    if (packs.isEmpty()) {
        errStream << "no shader packs (directories with metadata.json) found under the given path(s)\n";
        return 2;
    }

    // The model each pack is validated (or emitted) as: the forcing flag when
    // one was given, else the pack's own detected family. A pack with no
    // marker falls back to overlay, which is what the tool did unconditionally
    // before, and says so — an undetected pack is the one case where the
    // caller still has to know which flag to pass.
    const auto modelFor = [&forcedModel, &errStream](const QString& pack) {
        if (forcedModel) {
            return *forcedModel;
        }
        if (const std::optional<PackModel> detected = detectPackModel(pack)) {
            return *detected;
        }
        errStream << QFileInfo(pack).fileName()
                  << ": no sibling shared/ marker, validating as an overlay pack. Pass "
                     "--overlay/--animation/--surface to choose\n";
        return PackModel::Overlay;
    };

    // Emit mode: write each pack's autocomplete sidecar and exit (no validation).
    if (emitMode) {
        int failed = 0;
        for (const QString& pack : packs) {
            if (emitPreamble(pack, modelFor(pack), quiet, out, errStream) != 0) {
                ++failed;
            }
        }
        out.flush();
        if (failed > 0) {
            errStream << failed << " of " << packs.size() << " pack(s) failed.\n";
            return 1;
        }
        errStream << "wrote p_generated.glsl for " << packs.size() << " pack(s).\n";
        return 0;
    }

    int totalErrors = 0;
    int failedPacks = 0;
    for (const QString& pack : packs) {
        QString report;
        QTextStream reportStream(&report);
        const PackModel model = modelFor(pack);
        const int e = model == PackModel::Surface ? validateSurfacePack(pack, reportStream)
            : model == PackModel::Animation       ? validateAnimationPack(pack, reportStream)
                                                  : validatePack(pack, reportStream);
        reportStream.flush();
        totalErrors += e;
        if (e > 0) {
            ++failedPacks;
        }
        // In quiet mode only failing packs are printed — the rest is summarized.
        if (!quiet || e > 0) {
            out << report;
        }
    }
    out.flush();

    if (totalErrors == 0) {
        errStream << "All " << packs.size() << " pack(s) OK.\n";
        return 0;
    }
    errStream << failedPacks << " of " << packs.size() << " pack(s) failed (" << totalErrors << " error(s) total).\n";
    return 1;
}
