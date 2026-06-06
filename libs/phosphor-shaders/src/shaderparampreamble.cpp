// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderParamPreamble.h>

#include <PhosphorShaders/CustomParamsKey.h>

#include <QRegularExpression>
#include <QSet>

namespace PhosphorShaders {

namespace {

/// Overlay/zone image params bind to `uTexture0..3`. Animation packs never
/// use the Image pool (their textures are a separate top-level list bound to
/// `iChannel1..3`), so this cap only governs the zone path.
constexpr int kMaxImageSlots = 4;

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

QString spliceAfterVersion(const QString& source, const QString& block)
{
    if (block.isEmpty()) {
        return source;
    }

    static const QRegularExpression versionRe(QStringLiteral("^[ \\t]*#version\\b[^\\n]*$"),
                                              QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = versionRe.match(source);
    if (!m.hasMatch()) {
        // No #version directive — not valid GLSL, but don't silently drop the
        // block; prepend it (no #line fixup is meaningful without a version).
        return block + source;
    }

    // 1-based line number of the #version line, so we can renumber the original
    // line that follows it (versionLineNo + 1) after inserting the block.
    const int versionEnd = m.capturedEnd(0);
    const int versionLineNo = QStringView(source).left(versionEnd).count(QLatin1Char('\n')) + 1;

    // Insert just past the #version line's terminating newline.
    int insertPos = versionEnd;
    if (insertPos < source.size() && source.at(insertPos) == QLatin1Char('\n')) {
        insertPos += 1;
    }

    QString out = source.left(insertPos);
    if (!out.endsWith(QLatin1Char('\n'))) {
        out += QLatin1Char('\n');
    }
    out += block; // newline-terminated by buildParamPreamble
    out += QStringLiteral("#line %1 0\n").arg(versionLineNo + 1);
    out += QStringView(source).mid(insertPos);
    return out;
}

} // namespace PhosphorShaders
