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
#include <memory>
#include <utility>
#include <vector>

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

/// One glslang invocation to make: its flags and the files (name → source)
/// staged for it, whose paths follow the flags.
struct GlslangJob
{
    QStringList flags;
    QList<std::pair<QString, QString>> files;
};

/// Run every job in @p jobs at once, each in its own temp dir, and return
/// their results in order. A stage's arms and the interfaces its link reads
/// are independent invocations, so running them together keeps the
/// shader_validate_* gates well inside their ctest timeout. Output goes to
/// files rather than pipes, so a job is never held up behind a full pipe
/// while the one before it is waited on.
QList<GlslangRun> runGlslang(const QString& toolPath, const QList<GlslangJob>& jobs)
{
    struct Pending
    {
        std::unique_ptr<QTemporaryDir> dir;
        std::unique_ptr<QProcess> proc;
    };
    QList<GlslangRun> runs(jobs.size());
    std::vector<Pending> pending(jobs.size());
    for (qsizetype n = 0; n < jobs.size(); ++n) {
        GlslangRun& run = runs[n];
        Pending& p = pending[n];
        // A per-job QTemporaryDir keeps concurrent invocations from colliding
        // and takes the files with it on scope exit.
        p.dir = std::make_unique<QTemporaryDir>();
        if (!p.dir->isValid()) {
            run.output = QStringLiteral("cannot create a temporary directory for the compositor bake");
            continue;
        }
        QStringList args = jobs.at(n).flags;
        bool staged = true;
        for (const auto& [name, source] : jobs.at(n).files) {
            const QString path = p.dir->filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                run.output = QStringLiteral("cannot write ") + path;
                staged = false;
                break;
            }
            // Checked: a short write stages a TRUNCATED shader, and glslang
            // would then report a syntax error the author's file does not
            // contain.
            const QByteArray encoded = source.toUtf8();
            if (file.write(encoded) != encoded.size()) {
                run.output = QStringLiteral("could not stage the shader for compilation: ") + file.errorString();
                staged = false;
                break;
            }
            args << path;
            run.stagedPaths << path;
        }
        if (!staged) {
            continue;
        }
        p.proc = std::make_unique<QProcess>();
        p.proc->setStandardOutputFile(p.dir->filePath(QStringLiteral("stdout.txt")));
        p.proc->setStandardErrorFile(p.dir->filePath(QStringLiteral("stderr.txt")));
        p.proc->start(toolPath, args);
    }
    const auto readBack = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    for (qsizetype n = 0; n < jobs.size(); ++n) {
        GlslangRun& run = runs[n];
        Pending& p = pending[n];
        if (!p.proc) {
            continue; // staging failed, already described
        }
        // Well under the 120s ctest timeout on the shader_validate_* gates, so
        // a wedged invocation names the pack it wedged on instead of ctest
        // killing the whole run with nothing to point at.
        if (!p.proc->waitForFinished(15000)) {
            // A binary that cannot be executed at all (a broken symlink, no
            // execute bit, the wrong ELF class) fails here too, and reporting
            // that as a timeout sends the reader after the wrong problem.
            if (p.proc->error() == QProcess::FailedToStart) {
                run.output = QStringLiteral("could not run ") + toolPath + QStringLiteral(": ") + p.proc->errorString();
                continue;
            }
            p.proc->kill();
            p.proc->waitForFinished(1000);
            run.output = toolPath + QStringLiteral(" timed out");
            continue;
        }
        if (p.proc->exitStatus() == QProcess::CrashExit) {
            run.output = toolPath + QStringLiteral(" crashed: ") + p.proc->errorString();
            continue;
        }
        run.stdoutText = readBack(p.dir->filePath(QStringLiteral("stdout.txt")));
        run.output = run.stdoutText;
        if (!run.output.isEmpty() && !run.output.endsWith(QLatin1Char('\n'))) {
            run.output += QLatin1Char('\n');
        }
        run.output += readBack(p.dir->filePath(QStringLiteral("stderr.txt")));
        run.status = p.proc->exitCode() == 0 ? GlslangRun::Status::Ok : GlslangRun::Status::Rejected;
    }
    return runs;
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

/// Whether the identifier at @p at in @p text is a use rather than a member
/// access (`.name`) or a declarator (`vec2 name`, a parameter or a local).
bool isUseOf(const QString& text, qsizetype at)
{
    qsizetype i = at - 1;
    while (i >= 0 && text.at(i).isSpace()) {
        --i;
    }
    if (i < 0) {
        return true;
    }
    if (text.at(i) == QLatin1Char('.')) {
        return false;
    }
    if (!text.at(i).isLetterOrNumber() && text.at(i) != QLatin1Char('_')) {
        return true;
    }
    qsizetype start = i;
    while (start > 0 && (text.at(start - 1).isLetterOrNumber() || text.at(start - 1) == QLatin1Char('_'))) {
        --start;
    }
    // Two identifiers in a row is a type and its declarator everywhere in
    // GLSL except after `return`.
    return QStringView(text).mid(start, i - start + 1) == QLatin1String("return");
}

/// One stage to preprocess for its interface.
struct InterfaceRequest
{
    PhosphorShaders::ShaderStage stage;
    QString source;
    QString label;

    /// The cache key: the label is part of it because a failure's text names
    /// the stage.
    QString key() const
    {
        return stageToken(stage) + QLatin1Char('\n') + label + QLatin1Char('\n') + source;
    }

    GlslangJob job() const
    {
        return {{QStringLiteral("-E"), QStringLiteral("-S"), stageToken(stage)},
                {{QStringLiteral("kwin.") + stageToken(stage), source}}};
    }
};

/// Interfaces already read, failures included, so a family's built-in vertex
/// stage, which every pack without its own links against, is preprocessed
/// once per dialect per run.
QHash<QString, StageInterface>& interfaceCache()
{
    static QHash<QString, StageInterface> cache;
    return cache;
}

/// Read off the global `in` / `out` declarations of the preprocessed stage in
/// @p run, and the `#extension` lines that follow a declaration.
StageInterface parseStageInterface(const GlslangRun& run, const QString& label)
{
    StageInterface result;
    if (run.status != GlslangRun::Status::Ok) {
        // A pack stage that does not preprocess failed its own compile already,
        // and is not linked. A built-in one is compiled by the tests rather
        // than here, so its failure is reported in full.
        result.failure = run.status == GlslangRun::Status::ToolFailure
            ? run.output.trimmed()
            : diagnosticLines(run, label, {}).join(QLatin1Char('\n'));
        return result;
    }
    // A declaration as the preprocessor prints it, at the start of a line or
    // after the `;` of another: optional layout, interpolation / auxiliary /
    // precision qualifiers, the storage keyword, an optional precision, the
    // type (an array size may sit on it) and its declarators. A function
    // parameter (`void f(in vec2 a)`) is never preceded by either, and an
    // interface block (`out Block { … }`) is not matched, which leaves it out
    // of the check rather than misread.
    static const QRegularExpression declaration(
        QStringLiteral(R"((?:^|(?<=;))\s*(?:layout\s*\([^)]*\)\s*)?(?:(?:flat|smooth|noperspective|centroid|sample|)"
                       R"(invariant|precise|highp|mediump|lowp)\s+)*(in|out)\s+(?:(?:highp|mediump|lowp)\s+)?)"
                       R"((\w+(?:\s*\[[^\]]*\])?)\s+(\w+\s*(?:\[[^\]]*\])?(?:\s*,\s*\w+\s*(?:\[[^\]]*\])?)*)\s*;)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression arraySuffix(QStringLiteral(R"(\s*\[[^\]]*\]\s*$)"));
    QList<std::pair<qsizetype, qsizetype>> declarationSpans;
    for (auto it = declaration.globalMatch(run.stdoutText); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        declarationSpans.append({m.capturedStart(0), m.capturedEnd(0)});
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
    // The preprocessed text has no comments left, so a mention of an input
    // outside the in/out declarations is a use, unless it is a member access
    // or declares something else of the same name. A same-named parameter or
    // local that is then read still counts, since telling it from the input
    // takes scoping a regex does not have. That can only fail a pack whose
    // unmatched input is also unused, which is rare.
    const auto inDeclaration = [&declarationSpans](qsizetype at) {
        return std::any_of(declarationSpans.cbegin(), declarationSpans.cend(), [at](const auto& span) {
            return at >= span.first && at < span.second;
        });
    };
    for (const auto& [name, type] : std::as_const(result.inputs)) {
        const QRegularExpression mention(QStringLiteral("\\b") + QRegularExpression::escape(name)
                                         + QStringLiteral("\\b"));
        for (auto it = mention.globalMatch(run.stdoutText); it.hasNext();) {
            const qsizetype at = it.next().capturedStart(0);
            if (!inDeclaration(at) && isUseOf(run.stdoutText, at)) {
                result.usedInputs.insert(name);
                break;
            }
        }
    }
    // Mesa's rule: an `#extension` after anything that is not a preprocessor
    // directive is "not allowed in the middle of a shader", and a `#pragma`
    // counts as such a statement there. glslang accepts it anywhere, which is
    // why this is read off by hand.
    static const QRegularExpression extension(QStringLiteral(R"(^\s*#\s*extension\s+(\S+))"));
    static const QRegularExpression pragma(QStringLiteral(R"(^\s*#\s*pragma\b)"));
    bool pastDeclaration = false;
    for (const QString& line : run.stdoutText.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!trimmed.startsWith(QLatin1Char('#')) || pragma.match(trimmed).hasMatch()) {
            pastDeclaration = true;
            continue;
        }
        if (const QRegularExpressionMatch m = extension.match(trimmed); m.hasMatch() && pastDeclaration) {
            result.lateExtensions << m.captured(1);
        }
    }
    result.ok = true;
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
    // The scaffold's prologue and the colour block both land above the pack's
    // first line, and each holds declarations.
    result.compositorDeclarationsFirst = assembled != raw || assembly.finalizeColor;
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
                                       const QString& glesExtraDefines, CompositorStage::Origin origin)
{
    CompositorStage result;
    result.label = label;
    result.stage = stage;
    result.source = source;
    result.glesExtraDefines = glesExtraDefines;
    result.origin = origin;
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
    // Every arm in one batch: they are independent, and a glslang that cannot
    // run fails them all alike, which is reported once below.
    struct GlesArm
    {
        const char* tag;
        QString source;
    };
    // One GLES arm per source the stage is built from on a GLES context: 6.8's,
    // and for a stage 6.8 adds defines to, 6.7's without them. The texcoord
    // define selects which branch of a header compiles, so 6.7's branch would
    // otherwise never be compiled in this dialect at all.
    QList<GlesArm> arms = {{"compositor GLES", glesSource(stage)}};
    if (!stage.glesExtraDefines.isEmpty()) {
        arms.append({"compositor GLES 6.7", glesSource67(stage)});
    }
    QList<GlslangJob> jobs = {GlslangJob{flags, {{fileName, desktopSource(stage)}}}};
    for (const GlesArm& arm : std::as_const(arms)) {
        jobs.append(GlslangJob{flags, {{fileName, arm.source}}});
    }
    const QList<GlslangRun> runs = runGlslang(tool, jobs);
    const GlslangRun& desktop = runs.constFirst();
    const QStringList desktopLines = diagnosticLines(desktop, stage.label, stage.sourcePaths);
    if (desktop.status == GlslangRun::Status::ToolFailure) {
        // The GLES arms failed the same way, so it is said once.
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

    bool allOk = desktop.status == GlslangRun::Status::Ok;
    QStringList previousLines = desktop.status == GlslangRun::Status::Rejected ? desktopLines : QStringList();
    QString previousTag = QStringLiteral("compositor");
    for (qsizetype n = 0; n < arms.size(); ++n) {
        const GlesArm& arm = arms.at(n);
        const GlslangRun& gles = runs.at(n + 1);
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
    // Both dialects, since a header may declare or read varyings, or enable
    // an extension, under `#ifdef GL_ES` / `#ifndef GL_ES`.
    struct DialectPair
    {
        StageInterface vertex;
        StageInterface fragment;
    };
    // The interfaces not read yet and glslang's own link go in one batch.
    // The link is wasted when a check below already failed, which is rare.
    const QList<InterfaceRequest> requests = {
        {vertex.stage, vertexSource, vertex.label},
        {fragment.stage, fragmentSource, fragment.label},
        {vertex.stage, desktopSource(vertex), vertex.label},
        {fragment.stage, desktopSource(fragment), fragment.label},
    };
    QList<StageInterface> interfaces(requests.size());
    QList<GlslangJob> jobs;
    QList<qsizetype> missing;
    for (qsizetype n = 0; n < requests.size(); ++n) {
        if (const auto it = interfaceCache().constFind(requests.at(n).key()); it != interfaceCache().constEnd()) {
            interfaces[n] = *it;
        } else {
            jobs.append(requests.at(n).job());
            missing.append(n);
        }
    }
    jobs.append(
        GlslangJob{{QStringLiteral("-l")},
                   {{QStringLiteral("kwin.vert"), vertexSource}, {QStringLiteral("kwin.frag"), fragmentSource}}});
    const QList<GlslangRun> runs = runGlslang(tool, jobs);
    for (qsizetype k = 0; k < missing.size(); ++k) {
        const InterfaceRequest& request = requests.at(missing.at(k));
        interfaces[missing.at(k)] = parseStageInterface(runs.at(k), request.label);
        interfaceCache().insert(request.key(), interfaces.at(missing.at(k)));
    }
    const GlslangRun& link = runs.constLast();
    const DialectPair gles{interfaces.at(0), interfaces.at(1)};
    const DialectPair desktop{interfaces.at(2), interfaces.at(3)};

    QStringList problems;
    const auto addProblem = [&problems](const QString& problem) {
        if (!problems.contains(problem)) {
            problems << problem;
        }
    };
    for (const StageInterface* stage : {&gles.vertex, &gles.fragment, &desktop.vertex, &desktop.fragment}) {
        if (!stage->ok) {
            addProblem(stage->failure);
        }
    }
    if (problems.isEmpty()) {
        // glslang accepts an `#extension` anywhere, so its placement is checked
        // by hand, in both dialects: Mesa rejects a late one on a desktop
        // context too, where NVIDIA lets it through.
        for (const CompositorStage* owner : {&vertex, &fragment}) {
            const StageInterface& onGles = owner == &vertex ? gles.vertex : gles.fragment;
            const StageInterface& onDesktop = owner == &vertex ? desktop.vertex : desktop.fragment;
            for (const QString& ext : onDesktop.lateExtensions) {
                addProblem(QStringLiteral("%1 enables %2 after a declaration, which Mesa rejects on KWin 6.7's "
                                          "desktop context (\"#extension directive is not allowed in the middle of "
                                          "a shader\"). %3")
                               .arg(owner->label, ext,
                                    owner->compositorDeclarationsFirst
                                        ? QStringLiteral("The compositor places declarations of its own above this "
                                                         "stage's code, so the stage cannot enable an extension on "
                                                         "Mesa at all")
                                        : QStringLiteral("Move it directly under #version, above every declaration "
                                                         "and every #include that brings declarations in")));
            }
            for (const QString& ext : onGles.lateExtensions) {
                if (onDesktop.lateExtensions.contains(ext)) {
                    continue; // the desktop line above already names it
                }
                addProblem(QStringLiteral("%1 enables %2 after KWin's precision declarations, which Mesa and NVIDIA "
                                          "reject (\"#extension directive is not allowed in the middle of a "
                                          "shader\"). Nothing can come before those on a GLES context, so guard it "
                                          "with #ifndef GL_ES")
                               .arg(owner->label, ext));
            }
        }
        // GLSL links varyings by NAME once the locations are stripped, and
        // glslang's own link checks only a name both stages declare, so an
        // input with no output of its name is checked here. Only a USED one
        // fails the link on a real driver.
        for (const DialectPair* pair : {&gles, &desktop}) {
            QStringList written = pair->vertex.outputs.keys();
            written.sort();
            for (const auto& [name, type] : pair->fragment.inputs) {
                const auto output = pair->vertex.outputs.constFind(name);
                if (output != pair->vertex.outputs.constEnd()) {
                    if (*output != type) {
                        addProblem(QStringLiteral("%1 reads the varying %2 as %3, but %4 writes it as %5")
                                       .arg(fragment.label, name, type, vertex.label, *output));
                    }
                    continue;
                }
                if (!pair->fragment.usedInputs.contains(name)) {
                    continue;
                }
                const QString lead = QStringLiteral("%1 reads the varying %2, which %3 does not write.")
                                         .arg(fragment.label, name, vertex.label);
                switch (vertex.origin) {
                case CompositorStage::Origin::Pack:
                    addProblem(lead
                               + QStringLiteral(" The compositor matches varyings by name, so give the vertex "
                                                "output the same name"));
                    break;
                case CompositorStage::Origin::Builtin:
                    addProblem(lead
                               + QStringLiteral(" That stage writes only %1, so read that or declare a "
                                                "vertexShader that writes %2")
                                     .arg(written.join(QStringLiteral(", ")), name));
                    break;
                case CompositorStage::Origin::FixedBuiltin:
                    addProblem(lead
                               + QStringLiteral(" This pass always runs with that stage, which writes only %1, so "
                                                "read only that")
                                     .arg(written.join(QStringLiteral(", "))));
                    break;
                }
            }
        }
    }
    if (problems.isEmpty()) {
        if (link.status == GlslangRun::Status::ToolFailure) {
            problems << link.output;
        } else if (link.status == GlslangRun::Status::Rejected) {
            problems << diagnosticLines(link,
                                        {DiagnosticOwner{vertex.label, vertex.sourcePaths},
                                         DiagnosticOwner{fragment.label, fragment.sourcePaths}});
        }
    }

    if (problems.isEmpty()) {
        out << "  " << padLabel(fragment.label) << "OK (compositor link)\n";
        return 0;
    }
    out << "  " << padLabel(fragment.label) << "ERROR (compositor link)\n    linked against " << vertex.label << "\n";
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
