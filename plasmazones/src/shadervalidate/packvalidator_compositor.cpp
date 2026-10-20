// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The compositor bake every validator arm shares, split out of
// packvalidatorcommon.cpp by concern: assembling a pack stage the way the KWin
// effect does, compiling it through glslang in both of KWin's dialects, and
// linking each fragment against the vertex stage the compositor pairs it with.
// Declared in packvalidatorcommon.h, which says what each arm covers.

#include "packvalidatorcommon.h"

#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace PlasmaZones::ShaderValidate {

QString glslangValidatorPath()
{
    // BOTH upstream spellings, in the order a distro is likely to carry them.
    // glslang renamed its binary to plain `glslang` and kept `glslangValidator`
    // as a compatibility symlink that upstream documents as deprecated, so a
    // lookup pinned to either name alone is a time bomb: the old name breaks
    // wherever the symlink has been dropped, and the new one is missing on
    // anything older. Both resolve to the same program and take the same
    // arguments.
    //
    // Resolved once: a run validates up to a few hundred packs and the PATH
    // walk is the same answer every time.
    static const QString path = [] {
        for (const QString& name : {QStringLiteral("glslangValidator"), QStringLiteral("glslang")}) {
            const QString found = QStandardPaths::findExecutable(name);
            if (!found.isEmpty()) {
                return found;
            }
        }
        return QString();
    }();
    return path;
}

namespace {

/// glslang's `-S` token for @p stage.
QString stageToken(PhosphorShaders::ShaderStage stage)
{
    return stage == PhosphorShaders::ShaderStage::Vertex ? QStringLiteral("vert") : QStringLiteral("frag");
}

/// One glslang invocation over files staged in a private temp dir.
struct GlslangRun
{
    enum class Status {
        Ok,
        /// glslang ran and rejected the input. Its diagnostics are in @c output.
        Rejected,
        /// glslang could not judge the input at all: the staging failed, it
        /// would not start, it timed out or it crashed. @c output says which.
        /// No other arm can do better, so callers stop on this.
        ToolFailure,
    };
    Status status = Status::ToolFailure;
    QString output;
    QString stdoutText;
    QStringList stagedPaths;
};

/// Stage @p files (name → source) in a fresh temp dir and run @p toolPath with
/// @p flags followed by their paths.
GlslangRun runGlslang(const QString& toolPath, const QStringList& flags,
                      const QList<std::pair<QString, QString>>& files)
{
    GlslangRun run;
    // A per-call QTemporaryDir keeps concurrent invocations from colliding and
    // takes the files with it on scope exit.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        run.output = QStringLiteral("cannot create a temporary directory for the compositor bake");
        return run;
    }
    QStringList args = flags;
    for (const auto& [name, source] : files) {
        const QString path = tmp.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            run.output = QStringLiteral("cannot write ") + path;
            return run;
        }
        // Checked: a short write stages a TRUNCATED shader, and glslang would
        // then report a syntax error the author's file does not contain.
        const QByteArray encoded = source.toUtf8();
        if (file.write(encoded) != encoded.size()) {
            run.output = QStringLiteral("could not stage the shader for compilation: ") + file.errorString();
            return run;
        }
        args << path;
        run.stagedPaths << path;
    }

    QProcess proc;
    proc.start(toolPath, args);
    // Well under the 120s ctest timeout on the shader_validate_* gates, so a
    // wedged invocation names the pack it wedged on instead of ctest killing
    // the whole run with nothing to point at.
    if (!proc.waitForFinished(15000)) {
        // A binary that cannot be executed at all (a broken symlink, no
        // execute bit, the wrong ELF class) fails here too, and reporting
        // that as a timeout sends the reader after the wrong problem.
        if (proc.error() == QProcess::FailedToStart) {
            run.output = QStringLiteral("could not run ") + toolPath + QStringLiteral(": ") + proc.errorString();
            return run;
        }
        proc.kill();
        proc.waitForFinished(1000);
        run.output = toolPath + QStringLiteral(" timed out");
        return run;
    }
    if (proc.exitStatus() == QProcess::CrashExit) {
        run.output = toolPath + QStringLiteral(" crashed: ") + proc.errorString();
        return run;
    }
    run.stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
    run.output = run.stdoutText;
    if (!run.output.isEmpty() && !run.output.endsWith(QLatin1Char('\n'))) {
        run.output += QLatin1Char('\n');
    }
    run.output += QString::fromUtf8(proc.readAllStandardError());
    run.status = proc.exitCode() == 0 ? GlslangRun::Status::Ok : GlslangRun::Status::Rejected;
    return run;
}

/// Who a block of glslang output belongs to, for naming its source strings.
struct DiagnosticOwner
{
    QString label;
    QStringList sourcePaths;
};

/// The diagnostic lines of @p run, with the staged file names glslang echoes
/// dropped and each source-string id named after its file. @p owners holds
/// one entry per staged file, in staging order: glslang echoes a file's name
/// ahead of its diagnostics, so each block is named after the file it follows,
/// and output before any echo belongs to the first.
QStringList diagnosticLines(const GlslangRun& run, const QList<DiagnosticOwner>& owners)
{
    QStringList lines;
    qsizetype current = 0;
    for (const QString& line : run.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        // glslang echoes each source file name on a line of its own. That is a
        // staging path under a temp dir, which is noise at best and a
        // different string on every run at worst (it would make the report
        // unstable for anything diffing it). Both the bare name and the full
        // path are matched, since which one is echoed follows the argument.
        const auto echoed =
            std::find_if(run.stagedPaths.cbegin(), run.stagedPaths.cend(), [&trimmed](const QString& p) {
                return trimmed == p || trimmed == QFileInfo(p).fileName();
            });
        if (echoed != run.stagedPaths.cend()) {
            current = std::distance(run.stagedPaths.cbegin(), echoed);
            continue;
        }
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("SPIR-V is not generated"))) {
            continue;
        }
        const DiagnosticOwner& owner = owners.at(qBound(qsizetype(0), current, owners.size() - 1));
        lines << nameDiagnosticSource(trimmed, owner.label, owner.sourcePaths);
    }
    return lines;
}

QStringList diagnosticLines(const GlslangRun& run, const QString& label, const QStringList& sourcePaths)
{
    return diagnosticLines(run, {DiagnosticOwner{label, sourcePaths}});
}

/// Print the once-per-run explanation of a missing glslang under @p label.
void reportMissingTool(QTextStream& out, const QString& label)
{
    // Hard failure rather than a skip. The point of these gates is that a pack
    // cannot reach a release with the branch it ships on uncompiled, and a
    // quiet "no tool, no coverage" degrade is how bundled packs once went
    // unchecked. Explained once per run; every stage still counts the error,
    // so a tree of a hundred packs fails a hundred times but says why once.
    static bool explained = false;
    out << "  " << padLabel(label) << "ERROR (compositor, both dialects)\n";
    if (!explained) {
        explained = true;
        out << "    neither glslangValidator nor glslang found on PATH. One of them is required to compile packs "
               "in both of KWin's compositor dialects (install the glslang package)\n";
    }
}

/// @p stage finished for the desktop arm: KWin 6.7 on a desktop context.
QString desktopSource(const CompositorStage& stage)
{
    return PhosphorShaders::kwinDesktopDialect(PhosphorShaders::kwinCompositorSource(stage.source, stage.stage));
}

/// @p stage finished for the GLES arm: KWin 6.8, with what 6.8 adds.
QString glesSource(const CompositorStage& stage)
{
    return PhosphorShaders::kwinGlesDialect(
        PhosphorShaders::kwinCompositorSource(stage.source, stage.stage, stage.glesExtraDefines));
}

/// @p stage finished as KWin 6.7 builds it on a GLES context: the same
/// dialect as 6.8 without what only 6.8 adds.
QString glesSource67(const CompositorStage& stage)
{
    return PhosphorShaders::kwinGlesDialect(PhosphorShaders::kwinCompositorSource(stage.source, stage.stage));
}

/// The varyings and late `#extension` lines of one preprocessed stage.
struct StageInterface
{
    bool ok = false;
    QString failure;
    QHash<QString, QString> outputs; // name → type
    QList<std::pair<QString, QString>> inputs; // name, type, declaration order
    /// Inputs the stage refers to beyond their declaration. GLSL rejects an
    /// input with no matching output only when the stage uses it, and Mesa
    /// and NVIDIA both link an unused one.
    QSet<QString> usedInputs;
    /// Extensions enabled after the stage's first declaration, which Mesa
    /// rejects in either dialect. On GLES, KWin's own precision lines count,
    /// and NVIDIA rejects those too.
    QStringList lateExtensions;
};

/// Preprocess @p source as a @p stage and read off its global `in` / `out`
/// declarations and the `#extension` lines that follow a declaration. Cached
/// by source, failures included, so a family's built-in vertex stage, which
/// every pack without its own links against, is preprocessed once per run.
StageInterface stageInterface(const QString& toolPath, PhosphorShaders::ShaderStage stage, const QString& source,
                              const QString& label)
{
    static QHash<QString, StageInterface> cache;
    const QString key = stageToken(stage) + QLatin1Char('\n') + source;
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    StageInterface result;
    const GlslangRun run = runGlslang(toolPath, {QStringLiteral("-E"), QStringLiteral("-S"), stageToken(stage)},
                                      {{QStringLiteral("kwin.") + stageToken(stage), source}});
    if (run.status != GlslangRun::Status::Ok) {
        // A stage that does not preprocess failed its own compile already; the
        // caller only links stages that compiled, so this is a tool failure.
        result.failure = run.status == GlslangRun::Status::ToolFailure
            ? run.output.trimmed()
            : diagnosticLines(run, label, {}).join(QLatin1Char('\n'));
        cache.insert(key, result);
        return result;
    }
    // One declaration per line, as the preprocessor prints it: optional layout,
    // interpolation / auxiliary / precision qualifiers, the storage keyword,
    // an optional precision, the type and its declarators. A function
    // parameter (`void f(in vec2 a)`) never starts a line with its `in`, and an
    // interface block (`out Block { … }`) is not matched, which leaves it out
    // of the check rather than misread.
    static const QRegularExpression declaration(
        QStringLiteral(R"(^\s*(?:layout\s*\([^)]*\)\s*)?(?:(?:flat|smooth|noperspective|centroid|sample|invariant|)"
                       R"(precise|highp|mediump|lowp)\s+)*(in|out)\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+)"
                       R"((\w+\s*(?:\[[^\]]*\])?(?:\s*,\s*\w+\s*(?:\[[^\]]*\])?)*)\s*;)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression arraySuffix(QStringLiteral(R"(\s*\[[^\]]*\]\s*$)"));
    for (auto it = declaration.globalMatch(run.stdoutText); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        for (QString name : m.captured(3).split(QLatin1Char(','))) {
            name = name.remove(arraySuffix).trimmed();
            if (name.isEmpty() || name.startsWith(QLatin1String("gl_"))) {
                continue;
            }
            if (m.captured(1) == QLatin1String("out")) {
                result.outputs.insert(name, m.captured(2));
            } else {
                result.inputs.append({name, m.captured(2)});
            }
        }
    }
    // The preprocessed text has no comments left, so any mention of an input
    // past its declaration is a use.
    for (const auto& [name, type] : std::as_const(result.inputs)) {
        const QRegularExpression mention(QStringLiteral("\\b") + QRegularExpression::escape(name)
                                         + QStringLiteral("\\b"));
        int mentions = 0;
        for (auto it = mention.globalMatch(run.stdoutText); it.hasNext() && mentions < 2;) {
            it.next();
            ++mentions;
        }
        if (mentions > 1) {
            result.usedInputs.insert(name);
        }
    }
    // Mesa's rule: an `#extension` after anything that is not a preprocessor
    // directive is "not allowed in the middle of a shader". glslang accepts it
    // anywhere, which is why this is read off by hand.
    static const QRegularExpression extension(QStringLiteral(R"(^\s*#\s*extension\s+(\S+))"));
    bool pastDeclaration = false;
    for (const QString& line : run.stdoutText.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!trimmed.startsWith(QLatin1Char('#'))) {
            pastDeclaration = true;
            continue;
        }
        if (const QRegularExpressionMatch m = extension.match(trimmed); m.hasMatch() && pastDeclaration) {
            result.lateExtensions << m.captured(1);
        }
    }
    result.ok = true;
    cache.insert(key, result);
    return result;
}

} // namespace

QString finalizeColorStub()
{
    return QStringLiteral(
        "vec4 pzFinalizeColor(vec4 c) { return c; }\n"
        "#define PZ_FINALIZE_COLOR(c) pzFinalizeColor(c)\n");
}

std::optional<CompositorStage> assembleCompositorStage(QTextStream& out, const QString& label,
                                                       PhosphorShaders::ShaderStage stage, const QString& raw,
                                                       const QString& path, const QStringList& includePaths,
                                                       const CompositorAssembly& assembly)
{
    const QString assembled = assembly.entryPrologue.isEmpty()
        ? raw
        : PhosphorShaders::assembleEntryPoint(raw, assembly.entryPrologue, assembly.entryCandidates);
    CompositorStage result;
    result.label = label;
    result.stage = stage;
    result.glesExtraDefines = assembly.glesExtraDefines;
    QString err;
    // The resolver the compositor uses, NOT ShaderCompiler::expandSource: the
    // preview's expansion also searches the shader's own directory for an angle
    // include, so a pack-local `<x.glsl>` resolves on the preview and fails
    // where it ships.
    result.source = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
        assembled, QFileInfo(path).absolutePath(), includePaths, &err, nullptr, &result.sourcePaths);
    if (!err.isEmpty() || result.source.isEmpty()) {
        // The resolver returns empty for any failure: a missing file, an
        // unreadable one, a malformed directive, a cycle. Naming the
        // angle-versus-quoted case as THE cause sent an author with a simple
        // typo off changing bracket style, so it is offered as the likely
        // explanation rather than asserted, and the resolver's own message
        // leads.
        out << "  " << padLabel(label) << "ERROR (compositor, both dialects)\n    include expansion failed the way "
            << "the compositor expands it: " << err
            << "\n    (if the file sits beside this one, note that the compositor resolves an angle include only "
            << "against the registry roots, and the quoted form is the pack-local one)\n";
        return std::nullopt;
    }
    // Each spliceAfterVersion lands its block immediately below #version, and
    // kwinCompositorSource splices the define block last, so the define block
    // ends up ABOVE these two, which is what the compositor produces.
    result.source = PhosphorShaders::spliceAfterVersion(result.source, assembly.preamble);
    if (assembly.finalizeColor) {
        result.source = PhosphorShaders::spliceAfterVersion(result.source, finalizeColorStub());
    }
    return result;
}

CompositorStage builtinCompositorStage(const QString& label, PhosphorShaders::ShaderStage stage, const QString& source,
                                       const QString& glesExtraDefines)
{
    CompositorStage result;
    result.label = label;
    result.stage = stage;
    result.source = source;
    result.glesExtraDefines = glesExtraDefines;
    result.builtin = true;
    return result;
}

int reportCompositorCompile(QTextStream& out, const CompositorStage& stage)
{
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        reportMissingTool(out, stage.label);
        return 1;
    }
    const QStringList flags = {QStringLiteral("-S"), stageToken(stage.stage)};
    const QString fileName = QStringLiteral("kwin.") + stageToken(stage.stage);
    // DEFAULT mode on purpose, no -V and no -G. Both of those select a SPIR-V
    // target, which reimposes the very rules this dialect breaks (a -G run
    // rejects the shared headers' default-block uniforms with "non-opaque
    // uniform variables need a layout(location=L)"). Bare glslang validates
    // OpenGL-dialect GLSL and is what matches the compositor's own compile.
    const GlslangRun desktop = runGlslang(tool, flags, {{fileName, desktopSource(stage)}});
    const QStringList desktopLines = diagnosticLines(desktop, stage.label, stage.sourcePaths);
    if (desktop.status == GlslangRun::Status::ToolFailure) {
        // The GLES arm would fail the same way and double the wait, so it is
        // not run at all.
        out << "  " << padLabel(stage.label) << "ERROR (compositor, both dialects)\n    " << desktop.output << "\n";
        return 1;
    }
    if (desktop.status == GlslangRun::Status::Ok) {
        out << "  " << padLabel(stage.label) << "OK (compositor)\n";
    } else {
        out << "  " << padLabel(stage.label) << "ERROR (compositor)\n";
        for (const QString& line : desktopLines) {
            out << "    " << line << "\n";
        }
    }

    // One GLES arm per source the stage is built from on a GLES context: 6.8's,
    // and for a stage 6.8 adds defines to, 6.7's without them. The texcoord
    // define selects which branch of a header compiles, so 6.7's branch would
    // otherwise never be compiled in this dialect at all.
    struct GlesArm
    {
        const char* tag;
        QString source;
    };
    QList<GlesArm> arms = {{"compositor GLES", glesSource(stage)}};
    if (!stage.glesExtraDefines.isEmpty()) {
        arms.append({"compositor GLES 6.7", glesSource67(stage)});
    }
    bool allOk = desktop.status == GlslangRun::Status::Ok;
    QStringList previousLines = desktop.status == GlslangRun::Status::Rejected ? desktopLines : QStringList();
    QString previousTag = QStringLiteral("compositor");
    for (const GlesArm& arm : std::as_const(arms)) {
        const GlslangRun gles = runGlslang(tool, flags, {{fileName, arm.source}});
        const QStringList glesLines = diagnosticLines(gles, stage.label, stage.sourcePaths);
        if (gles.status == GlslangRun::Status::Ok) {
            out << "  " << padLabel(stage.label) << "OK (" << arm.tag << ")\n";
            previousLines.clear();
            continue;
        }
        allOk = false;
        out << "  " << padLabel(stage.label) << "ERROR (" << arm.tag << ")\n";
        if (gles.status == GlslangRun::Status::ToolFailure) {
            out << "    " << gles.output << "\n";
            break;
        }
        if (!previousLines.isEmpty() && glesLines == previousLines) {
            // The usual case for an ordinary error: every dialect rejects the
            // same line for the same reason, and printing it again only
            // buries the one that differs.
            out << "    (the same diagnostics as the " << previousTag << " arm above)\n";
        } else {
            for (const QString& line : glesLines) {
                out << "    " << line << "\n";
            }
            previousLines = glesLines;
            previousTag = QString::fromLatin1(arm.tag);
        }
    }
    return allOk ? 0 : 1;
}

int reportCompositorLink(QTextStream& out, const CompositorStage& vertex, const CompositorStage& fragment)
{
    const QString tool = glslangValidatorPath();
    if (tool.isEmpty()) {
        return 0; // both stages already counted the missing tool
    }
    const QString vertexSource = glesSource(vertex);
    const QString fragmentSource = glesSource(fragment);
    const StageInterface vertexInterface = stageInterface(tool, vertex.stage, vertexSource, vertex.label);
    const StageInterface fragmentInterface = stageInterface(tool, fragment.stage, fragmentSource, fragment.label);

    QStringList problems;
    if (!vertexInterface.ok || !fragmentInterface.ok) {
        problems << (vertexInterface.ok ? fragmentInterface.failure : vertexInterface.failure);
    } else {
        // glslang accepts an `#extension` anywhere, so its placement is checked
        // by hand, in both dialects: Mesa rejects a late one on a desktop
        // context too, where NVIDIA lets it through.
        for (const CompositorStage* owner : {&vertex, &fragment}) {
            const StageInterface& gles = owner == &vertex ? vertexInterface : fragmentInterface;
            const StageInterface desktop = stageInterface(tool, owner->stage, desktopSource(*owner), owner->label);
            for (const QString& ext : std::as_const(desktop.lateExtensions)) {
                problems << QStringLiteral(
                                "%1 enables %2 after a declaration, which Mesa rejects on KWin 6.7's desktop "
                                "context (\"#extension directive is not allowed in the middle of a shader\"). An "
                                "#extension has to come before everything the compositor places above the pack's "
                                "own code")
                                .arg(owner->label, ext);
            }
            for (const QString& ext : std::as_const(gles.lateExtensions)) {
                if (desktop.lateExtensions.contains(ext)) {
                    continue; // the desktop line above already names it
                }
                problems << QStringLiteral(
                                "%1 enables %2 after KWin's precision declarations, which Mesa and NVIDIA reject "
                                "(\"#extension directive is not allowed in the middle of a shader\"). Guard it "
                                "with #ifndef GL_ES")
                                .arg(owner->label, ext);
            }
        }
        // GLSL links varyings by NAME once the locations are stripped, and
        // glslang's own link checks only a name both stages declare, so an
        // input with no output of its name is checked here. Only a USED one
        // fails the link on a real driver.
        QStringList written = vertexInterface.outputs.keys();
        written.sort();
        for (const auto& [name, type] : fragmentInterface.inputs) {
            const auto output = vertexInterface.outputs.constFind(name);
            if (output == vertexInterface.outputs.constEnd()) {
                if (!fragmentInterface.usedInputs.contains(name)) {
                    continue;
                }
                problems << (vertex.builtin
                                 ? QStringLiteral("%1 reads the varying %2, which %3 does not write. That stage "
                                                  "writes only %4, so read that or declare a vertexShader that "
                                                  "writes %2")
                                       .arg(fragment.label, name, vertex.label, written.join(QStringLiteral(", ")))
                                 : QStringLiteral("%1 reads the varying %2, which %3 does not write. The compositor "
                                                  "matches varyings by name, so give the vertex output the same "
                                                  "name")
                                       .arg(fragment.label, name, vertex.label));
            } else if (*output != type) {
                problems << QStringLiteral("%1 reads the varying %2 as %3, but %4 writes it as %5")
                                .arg(fragment.label, name, type, vertex.label, *output);
            }
        }
    }
    if (problems.isEmpty()) {
        const GlslangRun link =
            runGlslang(tool, {QStringLiteral("-l")},
                       {{QStringLiteral("kwin.vert"), vertexSource}, {QStringLiteral("kwin.frag"), fragmentSource}});
        if (link.status == GlslangRun::Status::ToolFailure) {
            problems << link.output;
        } else if (link.status == GlslangRun::Status::Rejected) {
            problems << diagnosticLines(link,
                                        {DiagnosticOwner{vertex.label, vertex.sourcePaths},
                                         DiagnosticOwner{fragment.label, fragment.sourcePaths}});
        }
    }

    if (problems.isEmpty()) {
        out << "  " << padLabel(fragment.label) << "OK (compositor GLES link)\n";
        return 0;
    }
    out << "  " << padLabel(fragment.label) << "ERROR (compositor GLES link)\n    linked against " << vertex.label
        << "\n";
    for (const QString& p : problems) {
        out << "    " << p << "\n";
    }
    return 1;
}

bool readStageSource(QTextStream& out, const QString& label, const QString& path, QString& raw)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        out << "  " << padLabel(label) << "ERROR\n    cannot read " << path << "\n";
        return false;
    }
    raw = QString::fromUtf8(f.readAll());
    if (raw.isEmpty()) {
        // Refused here rather than handed on, so neither the preview arm nor
        // the compositor arms report an empty file as a failed include
        // expansion. The compositor refuses an empty stage before assembly
        // with its own message, and so does this.
        out << "  " << padLabel(label) << "ERROR\n    shader file is empty\n";
        return false;
    }
    return true;
}

std::optional<CompositorStage> bakeCompositorStage(QTextStream& out, int& errors, const QString& label,
                                                   PhosphorShaders::ShaderStage stage, const QString& raw,
                                                   const QString& path, const QStringList& includePaths,
                                                   const CompositorAssembly& assembly)
{
    std::optional<CompositorStage> assembled =
        assembleCompositorStage(out, label, stage, raw, path, includePaths, assembly);
    if (!assembled || reportCompositorCompile(out, *assembled) != 0) {
        ++errors;
        return std::nullopt;
    }
    return assembled;
}

DeclaredVertex bakeDeclaredVertex(QTextStream& out, int& errors, const QString& label, const QString& raw,
                                  const QString& path, const QStringList& includePaths,
                                  const CompositorAssembly& assembly)
{
    DeclaredVertex result;
    result.stage =
        assembleCompositorStage(out, label, PhosphorShaders::ShaderStage::Vertex, raw, path, includePaths, assembly);
    if (!result.stage) {
        ++errors; // an include failure, reported, and the runtime falls back to its built-in stage
    } else if (reportCompositorCompile(out, *result.stage) != 0) {
        ++errors;
        result.stage.reset();
        result.broken = true;
    }
    return result;
}

std::optional<CompositorStage> linkedVertex(const DeclaredVertex& declared, const CompositorStage& builtin)
{
    if (declared.broken) {
        return std::nullopt;
    }
    return declared.stage ? *declared.stage : builtin;
}

} // namespace PlasmaZones::ShaderValidate
