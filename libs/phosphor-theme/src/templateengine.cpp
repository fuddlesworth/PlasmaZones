// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/TemplateEngine.h>

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QLatin1String>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>
#include <QStringLiteral>
#include <QVariant>

namespace PhosphorTheme {

namespace {

// Map a single-token reference (token name + optional `.field`) to its
// rendered string. Field defaults to "hex" (the most common case for
// CSS / config files).
//
// Returned `ok` is false only when the token is unknown — in which case
// the caller leaves the original `{{...}}` placeholder in place. An
// unknown FIELD on a known token renders the default form rather than
// failing loudly: a typo in a field name is less critical than a
// missing color, and we don't want a renderer crash to break a save.
QString renderToken(const QString& name, const QString& field, const QVariantMap& tokens, bool& ok)
{
    const auto it = tokens.constFind(name);
    if (it == tokens.constEnd()) {
        ok = false;
        return {};
    }
    ok = true;

    const QColor c = it.value().value<QColor>();
    if (!c.isValid()) {
        return {};
    }

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

    // Unknown field — degrade to the default hex form. Logged so the
    // template author can spot the typo without the substitution
    // disappearing silently.
    qWarning().noquote() << "phosphor-theme: unknown template field" << field << "on token" << name
                         << "— falling back to hex";
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
            // Unknown token — keep the placeholder so the failure is
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
        qWarning().noquote() << "phosphor-theme: cannot open template" << templatePath << "—" << in.errorString();
        return false;
    }
    const auto src = QString::fromUtf8(in.readAll());
    in.close();

    const auto rendered = render(src, tokens);

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning().noquote() << "phosphor-theme: cannot write rendered output" << outPath << "—" << out.errorString();
        return false;
    }
    out.write(rendered.toUtf8());
    out.close();
    return true;
}

} // namespace PhosphorTheme
