// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include "client.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Phosphorctl {

namespace {

// Parse one --arg value=... value into a QJsonValue. JSON-typed
// values use their natural shape (123, 1.5, true, "hello", [1,2],
// {"k":1}); anything that doesn't parse as JSON degrades to a
// string. Lets the user write `--arg count=42` AND
// `--arg payload={"k":1}` AND `--arg name=nate` without quoting
// hell. Caveats: the bare token `null` becomes a JSON null (use
// `"null"` to force a string); an unterminated `[1,2` falls back
// to the literal string.
QJsonValue parseArgValue(const QString& raw)
{
    const QByteArray bytes = raw.toUtf8();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error == QJsonParseError::NoError) {
        if (doc.isArray()) {
            return doc.array();
        }
        if (doc.isObject()) {
            return doc.object();
        }
    }
    // Try as a bare value (number / true / false / null) via the
    // single-value JSON parsing trick: wrap in an array.
    const QJsonDocument arrDoc = QJsonDocument::fromJson(QByteArray("[") + bytes + QByteArray("]"));
    if (!arrDoc.isNull() && arrDoc.isArray() && arrDoc.array().size() == 1) {
        return arrDoc.array().first();
    }
    // Fallback: treat as a raw string.
    return raw;
}

// What `call` waits for an answer by default, and the longest --timeout it
// takes (a day, well inside an int of milliseconds).
constexpr int DefaultTimeoutMs = 2000;
constexpr int MaxTimeoutSeconds = 24 * 60 * 60;

// Parse a --timeout value in seconds, fractions allowed, into milliseconds.
// 0 means no limit. Nothing for a value that is not a number in range.
std::optional<int> parseTimeoutMs(const QString& raw)
{
    bool ok = false;
    const double seconds = raw.toDouble(&ok);
    if (!ok || !std::isfinite(seconds) || seconds < 0 || seconds > MaxTimeoutSeconds) {
        return std::nullopt;
    }
    if (seconds == 0) {
        return 0;
    }
    // A positive value never rounds down to "no limit".
    return std::max(1, static_cast<int>(std::lround(seconds * 1000)));
}

} // namespace

int runCall(QStringList args, QString socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString socketErr;
    const QString lateSocket = stripSocketFlag(args, &socketErr);
    if (!socketErr.isEmpty()) {
        err << "phosphorctl call: " << socketErr << "\n";
        return 1;
    }
    if (!lateSocket.isEmpty()) {
        socketPath = lateSocket;
    }

    if (args.isEmpty()) {
        err << "phosphorctl call: expects <target>.<fn> [--arg name=value ...] [--timeout SECONDS]\n";
        return 1;
    }
    const QString targetDotFn = args.first();
    const int dotIdx = targetDotFn.indexOf(QLatin1Char('.'));
    if (dotIdx <= 0 || dotIdx == targetDotFn.size() - 1) {
        err << "phosphorctl call: first argument must be in the form <target>.<fn>\n";
        return 1;
    }
    const QString target = targetDotFn.left(dotIdx);
    const QString fn = targetDotFn.mid(dotIdx + 1);

    // Remaining args are --arg name=value flags, in the order they're
    // passed to the function, and an optional --timeout.
    QJsonArray jsonArgs;
    int timeoutMs = DefaultTimeoutMs;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--timeout") || a.startsWith(QLatin1String("--timeout="))) {
            QString value;
            if (a == QLatin1String("--timeout")) {
                if (i + 1 >= args.size()) {
                    err << "phosphorctl call: --timeout requires a number of seconds\n";
                    return 1;
                }
                value = args.at(++i);
            } else {
                value = a.mid(QStringLiteral("--timeout=").size());
            }
            const std::optional<int> parsed = parseTimeoutMs(value);
            if (!parsed) {
                err << "phosphorctl call: --timeout expects seconds from 0 to " << MaxTimeoutSeconds
                    << " (0 waits without a limit), got '" << sanitiseForSingleLine(value) << "'\n";
                return 1;
            }
            timeoutMs = *parsed;
            continue;
        }
        QString pair;
        if (a == QLatin1String("--arg") || a == QLatin1String("-a")) {
            if (i + 1 >= args.size()) {
                err << "phosphorctl call: --arg requires name=value\n";
                return 1;
            }
            pair = args.at(++i);
        } else if (a.startsWith(QLatin1String("--arg="))) {
            pair = a.mid(QStringLiteral("--arg=").size());
        } else {
            err << "phosphorctl call: unexpected argument '" << a << "' (use --arg name=value or --timeout SECONDS)\n";
            return 1;
        }
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            err << "phosphorctl call: --arg expects name=value, got '" << pair << "'\n";
            return 1;
        }
        // `name` is metadata for the user; the wire only carries
        // positional args (this matches Qt's invokeMethod model).
        // Append the value in declaration order.
        jsonArgs.append(parseArgValue(pair.mid(eq + 1)));
    }

    Client client;
    if (!client.connectTo(socketPath)) {
        err << "phosphorctl call: " << client.errorMessage() << "\n";
        return 2;
    }

    QJsonObject req;
    req.insert(QLatin1String(PhosphorIpc::Field::Type), QLatin1String(PhosphorIpc::RequestType::Call));
    req.insert(QLatin1String(PhosphorIpc::Field::Id), 1);
    req.insert(QLatin1String(PhosphorIpc::Field::Target), target);
    req.insert(QLatin1String(PhosphorIpc::Field::Fn), fn);
    req.insert(QLatin1String(PhosphorIpc::Field::Args), jsonArgs);

    // Events the target sends to this call go to stderr as they arrive, one
    // compact JSON line each, so that stdout carries only the result.
    const auto resp = client.request(req, timeoutMs, [&err](const QJsonObject& event) {
        err << QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)) << "\n";
        err.flush();
    });
    if (!resp.has_value()) {
        err << "phosphorctl call: " << client.errorMessage() << "\n";
        return 2;
    }
    if (resp->value(QLatin1String(PhosphorIpc::Field::Type)).toString()
        == QLatin1String(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl call: "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Message)).toString());
        const QJsonValue detail = resp->value(QLatin1String(PhosphorIpc::Field::Detail));
        if (detail.isObject() && !detail.toObject().isEmpty()) {
            // Detail payload is structured JSON the server may have
            // attached (e.g. arg-index for INVALID_ARG). Emit it as
            // a parenthesised JSON blob so scripts can grep it.
            // QJsonDocument::toJson(Compact) appends '\n', which
            // would split the error line mid-paren; trim it so the
            // closing ')' lands on the same line as the JSON blob.
            err << " (detail: "
                << QString::fromUtf8(QJsonDocument(detail.toObject()).toJson(QJsonDocument::Compact).trimmed()) << ")";
        }
        err << "\n";
        return 3;
    }

    const QJsonValue result = resp->value(QLatin1String(PhosphorIpc::Field::Result));
    // Pretty-print object / array results; emit primitive results
    // raw (no surrounding quotes for strings, easier to pipe).
    // Object / array paths route through QJsonDocument::toJson
    // which already escapes control bytes per JSON spec; raw string
    // results bypass JSON serialisation and need explicit
    // sanitisation against terminal escape injection before reaching
    // stdout.
    if (result.isObject()) {
        // QJsonDocument::toJson(Indented) already appends a trailing
        // '\n', so the output is line-terminated without an explicit
        // "\n" suffix (Compact mode is the one that omits the
        // trailing newline; we use Indented here).
        out << QString::fromUtf8(QJsonDocument(result.toObject()).toJson(QJsonDocument::Indented));
    } else if (result.isArray()) {
        out << QString::fromUtf8(QJsonDocument(result.toArray()).toJson(QJsonDocument::Indented));
    } else if (result.isString()) {
        out << sanitiseForTerminal(result.toString()) << "\n";
    } else if (result.isNull()) {
        // void return, print nothing.
    } else if (result.isBool()) {
        out << (result.toBool() ? QStringLiteral("true") : QStringLiteral("false")) << "\n";
    } else if (result.isDouble()) {
        // Avoid trailing .0 on integer-valued doubles. Guard against
        // out-of-range doubles before casting to qint64: a result
        // beyond +-2^63 would be UB on cast. isfinite() and the
        // range check together cover NaN, +/-inf, and overflow.
        // INT64_MIN is exact as a double (single-bit mantissa at
        // -2^63); using numeric_limits dodges the "integer literal
        // too large to be represented" warning some compilers emit
        // for `-9223372036854775808` even when followed by `.0`.
        // Matches the equivalent boundary computation in
        // phosphor-libs/libs/phosphor-ipc/src/ipcprotocol.cpp::parseIntegralJsonNumber.
        const double d = result.toDouble();
        constexpr double Int64Min = static_cast<double>(std::numeric_limits<qint64>::min());
        constexpr double Int64Max = 9223372036854775808.0; // 2^63, one past max
        if (std::isfinite(d) && d >= Int64Min && d < Int64Max && d == static_cast<double>(static_cast<qint64>(d))) {
            out << static_cast<qint64>(d) << "\n";
        } else {
            out << d << "\n";
        }
    }
    return 0;
}

} // namespace Phosphorctl
