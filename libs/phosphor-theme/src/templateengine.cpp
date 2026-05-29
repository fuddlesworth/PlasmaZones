// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/TemplateEngine.h>

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QLatin1String>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSaveFile>
#include <QString>
#include <QVariant>

namespace PhosphorTheme {

namespace {

// Map a single-token reference to its rendered string. The reference
// is a token name plus an optional `.field`. Field defaults to "hex"
// because that is the most common case for CSS and config files.
//
// Returned `ok` is false only when the token is unknown. In that case
// the caller leaves the original `{{...}}` placeholder in place. An
// unknown FIELD on a known token renders the default form rather than
// failing loudly. A typo in a field name is less critical than a
// missing color. A renderer crash mid-save would be worse than a
// fallback render.
QString renderToken(const QString& name, const QString& field, const QVariantMap& tokens, bool& ok)
{
    ok = false;
    const auto it = tokens.constFind(name);
    if (it == tokens.constEnd()) {
        return {};
    }

    const QColor c = it.value().value<QColor>();
    if (!c.isValid()) {
        // Token is in the palette but its value isn't a valid color
        // (PaletteStore::applyPalette normally normalises to QColor; if
        // we still get here a custom IThemeService stored something
        // odd). Leave the placeholder in place via ok=false so the
        // template surfaces the problem instead of injecting an empty
        // string the user has to debug.
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
    qWarning().noquote() << "phosphor-theme: unknown template field" << field << "on token" << name
                         << ", falling back to hex";
    return c.name(QColor::HexRgb).toUpper();
}

} // namespace

QString TemplateEngine::render(const QString& templateSource, const QVariantMap& tokens)
{
    // Capture group 1 = token name (alnum + underscore).
    // Capture group 2 = optional field (alphabetic), without the dot.
    static const QRegularExpression re(
        QStringLiteral(R"(\{\{\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:\.\s*([A-Za-z]+))?\s*\}\})"),
        QRegularExpression::UseUnicodePropertiesOption);

    QString result;
    result.reserve(templateSource.size());

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
            // the user can spot rename mistakes.
            qWarning().noquote() << "phosphor-theme: unknown token in template:" << name;
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
        qWarning().noquote() << "phosphor-theme: cannot open template" << templatePath << ", " << in.errorString();
        return false;
    }
    const auto src = QString::fromUtf8(in.readAll());
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
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning().noquote() << "phosphor-theme: cannot write rendered output" << outPath << ", " << out.errorString();
        return false;
    }
    const qint64 written = out.write(payload);
    if (written != payload.size()) {
        qWarning().noquote() << "phosphor-theme: short write on rendered output" << outPath << ", " << out.errorString();
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        qWarning().noquote() << "phosphor-theme: commit failed on rendered output" << outPath << ", " << out.errorString();
        return false;
    }
    return true;
}

} // namespace PhosphorTheme
