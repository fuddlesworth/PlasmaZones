// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderParamPreamble.h>

#include <PhosphorShaders/CustomParamsKey.h>

#include <QSet>
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
    return QStringLiteral("#extension GL_ARB_explicit_attrib_location : enable") + eol
        + QStringLiteral("#extension GL_ARB_separate_shader_objects : enable") + eol
        + QStringLiteral("#define PLASMAZONES_KWIN") + eol;
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
            return lineStop;
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
    if (block.isEmpty()) {
        return source;
    }

    // Strip a leading BOM once. This splice runs BEFORE the compositor's
    // injectKwinDefineAfterVersion on the pointer, surface and animation
    // paths, and that step strips a BOM of its own; a preamble left in front
    // of BOM+#version would make it see no directive and synthesize a second
    // `#version`, which fails to compile.
    QString working = source;
    if (working.startsWith(QChar(0xFEFF))) {
        working.remove(0, 1);
    }
    // Emit the source's own line-ending convention, as the compositor's inject
    // step does, so a CRLF pack does not end up mixed.
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
    out += QStringLiteral("#line %1 0").arg(versionLineNo + 1) + eol;
    out += QStringView(working).mid(insertPos);
    return out;
}

} // namespace PhosphorShaders
