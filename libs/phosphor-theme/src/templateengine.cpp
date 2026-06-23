// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/TemplateEngine.h>

#include "phosphortheme_logging.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QLatin1String>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSaveFile>
#include <QString>
#include <QVariant>

// Definition of the library-wide logging category declared in
// phosphortheme_logging.h. Lives here (any one TU works) so the
// QLoggingCategory storage exists exactly once.
Q_LOGGING_CATEGORY(lcPhosphorTheme, "phosphor.theme")

namespace PhosphorTheme {

namespace {

// Map a single-token reference to its rendered string. The reference
// is a token name plus an optional `.field`. Field defaults to "hex"
// because that is the most common case for CSS and config files.
//
// Returned `ok` is false only when the token is unknown. In that case
// the caller leaves the original `{{...}}` placeholder in place. An
// unknown FIELD on a known token logs a qCWarning AND renders the
// default hex form — the warning is the loud part (so the template
// author sees the typo in the journal), the rendering is the
// graceful-degradation part (so a save still produces a usable
// output file instead of crashing mid-render). A typo in a field
// name is less critical than a missing color, and a renderer crash
// mid-save would be worse than a fallback render.
QString renderToken(const QString& name, const QString& field, const QVariantMap& tokens, bool& ok)
{
    ok = false;
    const auto it = tokens.constFind(name);
    if (it == tokens.constEnd()) {
        return {};
    }

    const QColor c = it.value().value<QColor>();
    if (!c.isValid()) {
        // Token is in the map but its value isn't a QColor. render()
        // consumes a pre-normalised token map: PaletteStore (via
        // applyPalette / extractValidTokens) converts hex strings and
        // named colors to QColor before storing, so a non-QColor here
        // means the caller passed a map that skipped that normalisation.
        // Leave the placeholder in place via ok=false (graceful
        // degradation) AND warn — symmetric with the unknown-field path
        // below, which also warns + degrades, so the miss is never
        // silent.
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: template token" << name
            << "has a non-color value; render() expects a QColor-normalised token map. Leaving placeholder in place";
        return {};
    }
    ok = true;

    if (field.isEmpty() || field == QLatin1String("hex")) {
        return c.name(QColor::HexRgb).toUpper();
    }
    if (field == QLatin1String("hexa")) {
        return c.name(QColor::HexArgb).toUpper();
    }
    if (field == QLatin1String("r") || field == QLatin1String("red")) {
        return QString::number(c.red());
    }
    if (field == QLatin1String("g") || field == QLatin1String("green")) {
        return QString::number(c.green());
    }
    if (field == QLatin1String("b") || field == QLatin1String("blue")) {
        return QString::number(c.blue());
    }
    if (field == QLatin1String("a") || field == QLatin1String("alpha")) {
        return QString::number(c.alphaF(), 'f', 3);
    }
    if (field == QLatin1String("rgb")) {
        return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
    }
    if (field == QLatin1String("rgba")) {
        return QStringLiteral("%1, %2, %3, %4")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(QString::number(c.alphaF(), 'f', 3));
    }

    // Unknown field: degrade to the default hex form. Logged so the
    // template author can spot the typo without the substitution
    // disappearing silently.
    qCWarning(lcPhosphorTheme).noquote() << "phosphor-theme: unknown template field" << field << "on token" << name
                                         << ", falling back to hex";
    return c.name(QColor::HexRgb).toUpper();
}

} // namespace

QString TemplateEngine::render(const QString& templateSource, const QVariantMap& tokens)
{
    // Capture group 1 = token name (alnum + underscore).
    // Capture group 2 = optional field (alphabetic), without the dot.
    // No UseUnicodePropertiesOption — the character classes are pure
    // ASCII ([A-Za-z_], [A-Za-z0-9_], [A-Za-z]) so unicode-aware word
    // semantics would buy nothing and pay a regex-engine cost.
    static const QRegularExpression re(
        QStringLiteral(R"(\{\{\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:\.\s*([A-Za-z]+))?\s*\}\})"));

    QString result;
    // Pre-reserve to damp the per-append realloc storm. This is a coarse
    // hint, not a precise prediction: output size is driven by how many
    // placeholders the template contains (not knowable without scanning
    // it twice) and how each renders (7-16 chars, e.g. "#RRGGBBAA" or
    // "255, 200, 100, 1.000"). `tokens.size() * 16` is just a cheap
    // proxy for "expect roughly token-count expansions"; reserve only
    // hints capacity, so an over- or under-estimate costs at most a
    // realloc, never correctness.
    result.reserve(templateSource.size() + tokens.size() * 16);

    qsizetype cursor = 0;
    const QStringView source(templateSource);
    auto it = re.globalMatch(templateSource);
    while (it.hasNext()) {
        const auto match = it.next();
        result.append(source.mid(cursor, match.capturedStart() - cursor));

        const auto name = match.captured(1);
        const auto field = match.captured(2);

        bool ok = false;
        const auto rendered = renderToken(name, field, tokens, ok);
        if (ok) {
            result.append(rendered);
        } else {
            // Unknown token, keep the placeholder so the failure is
            // visible in the rendered output. Loud warning to stderr so
            // the user can spot rename mistakes. Routed through the
            // shared lcPhosphorTheme category so a filter that mutes
            // the unknown-field warning above also catches this one.
            qCWarning(lcPhosphorTheme).noquote() << "phosphor-theme: unknown token in template:" << name;
            result.append(match.captured(0));
        }
        cursor = match.capturedEnd();
    }
    result.append(source.mid(cursor));
    return result;
}

bool TemplateEngine::renderFile(const QString& templatePath, const QString& outPath, const QVariantMap& tokens)
{
    QFile in(templatePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: cannot open template" << templatePath << ", " << in.errorString();
        return false;
    }
    const auto raw = in.readAll();
    if (in.error() != QFileDevice::NoError) {
        // readAll() returns whatever it managed to read on a short
        // read (truncated NFS mounts, signal-interrupted reads).
        // Without this check we'd silently render a partial template
        // and atomically commit a half-rendered output, which is
        // exactly the failure mode QSaveFile is supposed to prevent.
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: short read on template" << templatePath << ", " << in.errorString();
        return false;
    }
    const auto src = QString::fromUtf8(raw);
    in.close();

    const auto rendered = render(src, tokens);
    const auto payload = rendered.toUtf8();

    // Atomic write: QSaveFile writes to a temp sibling and renames
    // into place on commit(). A crash mid-write leaves the previous
    // contents intact at outPath rather than a half-written file. This
    // matters because consumers often render into live config paths
    // (e.g. ~/.config/gtk-3.0/gtk.css) where a partial file would
    // break the user's session.
    QSaveFile out(outPath);
    // setDirectWriteFallback(true) covers filesystems where the
    // rename(2) step fails (network mounts that deny cross-link
    // operations, some FUSE setups, /tmp-on-overlayfs in
    // containers). Without it, QSaveFile silently refuses to commit
    // on those mounts and renderFile reports failure even though
    // the user just wants the file written. With the fallback,
    // QSaveFile writes in place if-and-only-if the atomic rename
    // path is unavailable — atomicity is preserved on every
    // filesystem that supports it.
    out.setDirectWriteFallback(true);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: cannot write rendered output" << outPath << ", " << out.errorString();
        return false;
    }
    const qint64 written = out.write(payload);
    if (written != payload.size()) {
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: short write on rendered output" << outPath << ", " << out.errorString();
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        qCWarning(lcPhosphorTheme).noquote()
            << "phosphor-theme: commit failed on rendered output" << outPath << ", " << out.errorString();
        return false;
    }
    return true;
}

} // namespace PhosphorTheme
