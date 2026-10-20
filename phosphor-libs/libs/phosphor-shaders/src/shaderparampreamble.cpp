// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderParamPreamble.h>

#include <PhosphorShaders/CustomParamsKey.h>

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QStringView>

namespace PhosphorShaders {

namespace {

QString imageAccessor(int slot)
{
    if (slot < 0 || slot >= kMaxImageSlots) {
        return {};
    }
    return QStringLiteral("uTexture") + QString::number(slot);
}

} // namespace

/// A parameter id must be a valid GLSL identifier *body* — the `p_` prefix
/// guarantees a valid leading character, so a leading digit in the id is fine,
/// but anything outside `[A-Za-z0-9_]` (or an empty id) would produce a broken
/// `#define` token and is rejected.
bool isValidParamId(const QString& id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/// Ids the shared animation header already defines as `p_<id>`, which a pack must
/// not shadow. VALIDATOR LINT ONLY: buildParamPreamble below does NOT skip these,
/// and must not start to on its own. Its lane numbering has to stay byte-identical
/// to the upload numbering in each family's translate*Params, and those skip on
/// !isValidParamId alone, so skipping on one side desyncs every later lane, which
/// is worse than the compile error it would prevent. Closing it properly means
/// skipping in AnimationShaderRegistry::paramPreamble (before it calls
/// buildParamPreamble) and translateAnimationParams together. buildParamPreamble
/// itself serves every family, and only the animation header reserves these.
bool isReservedAnimationParamId(const QString& id)
{
    // Kept in step with the `#define p_...` lines in
    // plasmazones/data/animations/shared/animation_uniforms.glsl by hand: that header is data,
    // not a compiled input, so nothing can assert the pairing. A grep for
    // "^#define p_" across plasmazones/data/*/shared is what enumerates it.
    return id == QLatin1String("reversed");
}

QString buildParamPreamble(const QList<PreambleParam>& params)
{
    if (params.isEmpty()) {
        return {};
    }

    // Two-pass slot assignment mirroring parseShaderMetadata's auto-slot: reserve
    // every explicit slot first, then fill omitted ones into the next free lane of
    // their pool in declaration order, skipping reserved slots — so the generated
    // p_<id> lane numbering stays byte-identical to the registry's upload numbering
    // even for a pack that mixes explicit and auto slots in one pool.
    QSet<int> usedScalar, usedColor, usedImage;
    for (const PreambleParam& p : params) {
        if (!isValidParamId(p.id) || p.explicitSlot < 0) {
            continue;
        }
        (p.pool == PreambleParam::Pool::Color       ? usedColor
             : p.pool == PreambleParam::Pool::Image ? usedImage
                                                    : usedScalar)
            .insert(p.explicitSlot);
    }
    int scalarNext = 0;
    int colorNext = 0;
    int imageNext = 0;
    const auto nextFree = [](QSet<int>& used, int& next) {
        while (used.contains(next)) {
            ++next;
        }
        const int slot = next;
        used.insert(next);
        ++next;
        return slot;
    };

    QString out = QStringLiteral("// ---- generated parameter accessors (do not edit) ----\n");
    for (const PreambleParam& p : params) {
        if (!isValidParamId(p.id)) {
            out += QStringLiteral("// p: skipped a parameter with an invalid identifier\n");
            continue;
        }

        QString accessor;
        switch (p.pool) {
        case PreambleParam::Pool::Scalar: {
            const int slot = p.explicitSlot >= 0 ? p.explicitSlot : nextFree(usedScalar, scalarNext);
            accessor = CustomParams::glslAccessor(slot);
            break;
        }
        case PreambleParam::Pool::Color: {
            const int slot = p.explicitSlot >= 0 ? p.explicitSlot : nextFree(usedColor, colorNext);
            accessor = CustomColors::glslAccessor(slot);
            break;
        }
        case PreambleParam::Pool::Image: {
            const int slot = p.explicitSlot >= 0 ? p.explicitSlot : nextFree(usedImage, imageNext);
            accessor = imageAccessor(slot);
            break;
        }
        }

        if (accessor.isEmpty()) {
            out += QStringLiteral("// p: skipped \"") + p.id + QStringLiteral("\" (slot out of range)\n");
            continue;
        }
        out += QStringLiteral("#define p_") + p.id + QLatin1Char(' ') + accessor + QLatin1Char('\n');
    }
    out += QStringLiteral("// -----------------------------------------------------\n");
    return out;
}

QString kwinDefineBlock(const QString& eol)
{
    return QStringLiteral("#ifndef GL_ES") + eol + QStringLiteral("#extension GL_ARB_explicit_attrib_location : enable")
        + eol + QStringLiteral("#extension GL_ARB_separate_shader_objects : enable") + eol + QStringLiteral("#endif")
        + eol + QStringLiteral("#define PLASMAZONES_KWIN") + eol;
}

QString stripVaryingLocations(const QString& source, ShaderStage stage)
{
    // The qualifier, then any interpolation or auxiliary qualifiers, then the
    // storage qualifier that makes the declaration a varying. The lookahead
    // keeps those qualifiers and the `in`/`out` in place, and may cross a
    // newline, since a qualifier on a line of its own is legal GLSL. The
    // whitespace the match itself consumes is blanks only, never \s, so a
    // match cannot swallow a newline and shift the line numbers a compile
    // error reports.
    static const QRegularExpression vertexOut(QStringLiteral(
        R"(\blayout[ \t]*\([ \t]*location[ \t]*=[ \t]*\d+[ \t]*\)[ \t]*(?=\s*(?:(?:flat|smooth|noperspective|centroid|sample|invariant|precise)\s+)*out\b))"));
    static const QRegularExpression fragmentIn(QStringLiteral(
        R"(\blayout[ \t]*\([ \t]*location[ \t]*=[ \t]*\d+[ \t]*\)[ \t]*(?=\s*(?:(?:flat|smooth|noperspective|centroid|sample|invariant|precise)\s+)*in\b))"));
    QString out = source;
    out.remove(stage == ShaderStage::Vertex ? vertexOut : fragmentIn);
    return out;
}

namespace {

/// KWin's own top-level rewrite: @p prologue, then @p source with every line
/// that starts with `#version` blanked. KWin tests the raw line with
/// startsWith, untrimmed, and splits on '\n' alone, so a CRLF source keeps its
/// '\r' on every line it passes through, and a blanked line keeps its newline.
QString kwinRewrite(const QString& prologue, const QString& source)
{
    QString out = prologue;
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (!lines.at(i).startsWith(QLatin1String("#version"))) {
            out += lines.at(i);
        }
        if (i + 1 < lines.size()) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace

QString kwinGlesDialect(const QString& source)
{
    return kwinRewrite(QStringLiteral("#version 300 es\n"
                                      "#extension GL_OES_EGL_image_external : require\n"
                                      "precision highp float;\n"
                                      "precision highp sampler2D;\n"
                                      "precision highp sampler3D;\n"),
                       source);
}

QString kwinDesktopDialect(const QString& source)
{
    return kwinRewrite(QStringLiteral("#version 140\n"), source);
}

QString kwinTopDownTexcoordsDefine(const QString& eol)
{
    return QStringLiteral("#define PZ_KWIN_OFFSCREEN_TEXCOORDS_TOP_DOWN 1") + eol;
}

QString lineDirective(int line, int sourceString, const QString& eol)
{
    const QString name = QStringLiteral("PZ_LINE_%1_%2").arg(QString::number(sourceString), QString::number(line));
    return QStringLiteral("#if !defined(GL_ES) && __VERSION__ < 330") + eol + QStringLiteral("#define ") + name
        + QLatin1Char(' ') + QString::number(line - 1) + eol + QStringLiteral("#else") + eol
        + QStringLiteral("#define ") + name + QLatin1Char(' ') + QString::number(line) + eol + QStringLiteral("#endif")
        + eol + QStringLiteral("#line ") + name + QLatin1Char(' ') + QString::number(sourceString) + eol;
}

QString kwinCompositorSource(const QString& source, ShaderStage stage, const QString& extraDefines,
                             bool* synthesizedVersion)
{
    QString working = source;
    if (working.startsWith(QChar(0xFEFF))) {
        working.remove(0, 1);
    }
    const QString eol = working.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QString extra = extraDefines;
    if (eol.size() == 2) {
        extra.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        extra.replace(QLatin1Char('\n'), eol);
    }
    const QString block = kwinDefineBlock(eol) + extra;
    const bool hasVersion = versionDirectiveEnd(working) >= 0;
    if (synthesizedVersion) {
        *synthesizedVersion = !hasVersion;
    }
    // A missing directive is synthesized rather than prepended around: GLSL
    // requires `#version` first, and the compositor's core and ES contexts
    // reject a source without one. The re-anchor keeps the author's first line
    // numbered 1, since the directive is not theirs.
    const QString finished = hasVersion
        ? spliceAfterVersion(working, block)
        : QStringLiteral("#version 450") + eol + block + lineDirective(1, 0, eol) + working;
    return stripVaryingLocations(finished, stage);
}

int versionDirectiveEnd(const QString& source)
{
    // The BOM is not a Unicode whitespace category, so trimmed() would leave
    // it in front of `#version` and the first line would never match. Skip it
    // here rather than asking every caller to strip it first.
    int searchFrom = source.startsWith(QChar(0xFEFF)) ? 1 : 0;
    bool inBlockComment = false;
    while (searchFrom < source.size()) {
        const int lineEnd = source.indexOf(QLatin1Char('\n'), searchFrom);
        const int lineStop = lineEnd < 0 ? source.size() : lineEnd;
        QStringView line = QStringView(source).mid(searchFrom, lineStop - searchFrom);
        // A block comment opened on an earlier line swallows this one up to
        // its `*/`; a `#version` inside it is comment text, not a directive.
        if (inBlockComment) {
            const int closeIdx = line.indexOf(QLatin1String("*/"));
            if (closeIdx < 0) {
                if (lineEnd < 0) {
                    break;
                }
                searchFrom = lineEnd + 1;
                continue;
            }
            line = line.mid(closeIdx + 2);
            inBlockComment = false;
        }
        // Drop line comments and same-line block comments before testing the
        // prefix, so `// #version 300 es` and `/* #version */` never match.
        QString stripped;
        stripped.reserve(line.size());
        for (int i = 0; i < line.size();) {
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('/')) {
                break;
            }
            if (i + 1 < line.size() && line[i] == QLatin1Char('/') && line[i + 1] == QLatin1Char('*')) {
                const int closeIdx = line.indexOf(QLatin1String("*/"), i + 2);
                if (closeIdx < 0) {
                    inBlockComment = true;
                    break;
                }
                i = closeIdx + 2;
                continue;
            }
            stripped.append(line[i]);
            ++i;
        }
        // trimmed() also drops a CRLF source's trailing '\r'.
        if (stripped.trimmed().startsWith(QLatin1String("#version"))) {
            // A directive line that leaves a block comment open would put
            // every splice after it inside that comment. Refuse it as a
            // directive rather than hand back an insertion point there.
            return inBlockComment ? -1 : lineStop;
        }
        if (lineEnd < 0) {
            break;
        }
        searchFrom = lineEnd + 1;
    }
    return -1;
}

QString spliceAfterVersion(const QString& source, const QString& block)
{
    // Strip a leading BOM once, before the empty-block early return so the
    // result never carries one either way. This splice runs BEFORE the
    // compositor's kwinCompositorSource on the pointer, surface and animation
    // paths, and a preamble left in front of BOM+#version would make that step
    // see no directive and synthesize a second `#version`, which fails to
    // compile.
    QString working = source;
    if (working.startsWith(QChar(0xFEFF))) {
        working.remove(0, 1);
    }
    if (block.isEmpty()) {
        return working;
    }
    // The newline and re-anchor emitted below follow the source's own
    // line-ending convention. The block is spliced as the caller built it.
    const QString eol = working.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");

    const int versionEnd = versionDirectiveEnd(working);
    if (versionEnd < 0) {
        // No #version directive — not valid GLSL, but don't silently drop the
        // block; prepend it (no #line fixup is meaningful without a version).
        return block + working;
    }

    // 1-based line number of the #version line, so we can renumber the original
    // line that follows it (versionLineNo + 1) after inserting the block.
    const int versionLineNo = QStringView(working).left(versionEnd).count(QLatin1Char('\n')) + 1;

    // Insert just past the #version line's terminating newline; versionEnd IS
    // that newline unless the directive ends the file.
    const int insertPos = versionEnd < working.size() ? versionEnd + 1 : versionEnd;

    QString out = working.left(insertPos);
    if (!out.endsWith(QLatin1Char('\n'))) {
        out += eol;
    }
    out += block; // newline-terminated by buildParamPreamble
    out += lineDirective(versionLineNo + 1, 0, eol);
    out += QStringView(working).mid(insertPos);
    return out;
}

} // namespace PhosphorShaders
