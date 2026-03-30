// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmSandbox.h"
#include "core/logging.h"
#include <QJSEngine>
#include <QJSValue>
#include <QString>

namespace PlasmaZones {

bool hardenSandbox(QJSEngine* engine)
{
    Q_ASSERT(engine);
    if (!engine) {
        return false;
    }

    // Safe evaluate wrapper — checks for errors on all sandbox-hardening calls.
    // Non-critical hardening steps log warnings but do not abort.
    auto safeEval = [engine](const QString& code, const QString& context) {
        QJSValue result = engine->evaluate(code);
        if (result.isError()) {
            // Non-critical: logged at debug level to avoid spamming on engines
            // that lack async syntax (V4 SyntaxError on async function is expected)
            qCDebug(lcAutotile) << "ScriptedAlgorithm: sandbox hardening skipped for" << context << ":"
                                << result.toString();
        }
    };

    // Critical evaluate wrapper — returns false if the hardening step fails.
    // Used for eval/Function lockdown where failure means the sandbox is bypassable.
    auto criticalEval = [engine](const QString& code, const QString& context) -> bool {
        QJSValue result = engine->evaluate(code);
        if (result.isError()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: CRITICAL sandbox hardening failed for" << context << ":"
                                  << result.toString();
            return false;
        }
        return true;
    };

    // NOTE: Built-in helper globals (applyTreeGeometry, lShapeLayout, deckLayout,
    // distributeEvenly) are frozen AFTER injection in ScriptedAlgorithm::loadScript(),
    // not here — freezing before injection would lock them to undefined.

    // DRY helper for the repeated Object.defineProperty pattern used to disable globals.
    // When critical=true, failure aborts the sandbox; otherwise logs a warning and continues.
    auto disableGlobal = [&](const QLatin1String& name, bool critical = false) -> bool {
        const QString js = QStringLiteral(
                               "Object.defineProperty(this, '%1', "
                               "{value: undefined, writable: false, configurable: false})")
                               .arg(name);
        if (critical) {
            return criticalEval(js, QStringLiteral("disable %1").arg(name));
        }
        safeEval(js, QStringLiteral("disable %1").arg(name));
        return true;
    };

    // Disable eval() and Function constructor to prevent dynamic code generation.
    // These are CRITICAL — if any fails, the sandbox cannot prevent arbitrary code execution.
    if (!disableGlobal(QLatin1String("eval"), true)) {
        return false;
    }
    if (!criticalEval(QStringLiteral("Object.defineProperty(Function.prototype, 'constructor', "
                                     "{value: undefined, writable: false, configurable: false});"),
                      QStringLiteral("Function.prototype.constructor lockdown"))) {
        return false;
    }
    // Freeze Function.prototype BEFORE disabling the Function global (which sets
    // Function to undefined, making Function.prototype unreachable afterward).
    // This prevents scripts from restoring the constructor via prototype manipulation.
    if (!criticalEval(QStringLiteral("Object.freeze(Function.prototype);"),
                      QStringLiteral("Function.prototype freeze"))) {
        return false;
    }
    // Disable the Function global to prevent dynamic code generation
    if (!disableGlobal(QLatin1String("Function"), true)) {
        return false;
    }

    // Freeze GeneratorFunction and AsyncFunction constructors to prevent sandbox bypass.
    // GeneratorFunction is CRITICAL — if it fails, generators can escape the sandbox.
    if (!criticalEval(
            QStringLiteral(
                "(function(){"
                "var gf=Object.getPrototypeOf(function*(){}).constructor;"
                "Object.defineProperty(gf.prototype,'constructor',{value:undefined,writable:false,configurable:"
                "false});"
                "})();"),
            QStringLiteral("GeneratorFunction constructor lockdown"))) {
        return false;
    }
    // AsyncFunction lockdown elevated to critical: scripts can reach AsyncFunction via
    // Object.getPrototypeOf(async function(){}).constructor and execute arbitrary code,
    // bypassing the Function constructor lockdown above.
    // A SyntaxError means the engine lacks async support (e.g. V4/ES5), so AsyncFunction
    // is unreachable and no lockdown is needed. Any other error is a real failure.
    {
        QJSValue afResult = engine->evaluate(QStringLiteral(
            "(function(){"
            "var af=Object.getPrototypeOf(async function(){}).constructor;"
            "Object.defineProperty(af.prototype,'constructor',{value:undefined,writable:false,configurable:"
            "false});"
            "})();"));
        if (afResult.isError()) {
            const QString errorName = afResult.property(QStringLiteral("name")).toString();
            if (errorName != QLatin1String("SyntaxError")) {
                qCWarning(lcAutotile) << "ScriptedAlgorithm: CRITICAL sandbox hardening failed for"
                                      << "AsyncFunction constructor lockdown" << ":" << afResult.toString();
                return false;
            }
            // SyntaxError: engine lacks async support, AsyncFunction is unreachable — safe.
        }
    }

    // Freeze built-in prototypes to prevent prototype pollution.
    // Includes String, Number, Boolean, RegExp, Date, Error, Map, Set
    // in addition to Object and Array. If any freeze fails, the sandbox is
    // compromised — caller must abort.
    {
        QJSValue freezeResult =
            engine->evaluate(QStringLiteral("Object.freeze(Object.prototype);"
                                            "Object.freeze(Array.prototype);"
                                            "Object.freeze(String.prototype);"
                                            "Object.freeze(Number.prototype);"
                                            "Object.freeze(Boolean.prototype);"
                                            "Object.freeze(RegExp.prototype);"
                                            "Object.freeze(Date.prototype);"
                                            "Object.freeze(Error.prototype);"
                                            "Object.freeze(Map.prototype);"
                                            "Object.freeze(Set.prototype);"));
        if (freezeResult.isError()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: prototype freeze failed — sandbox compromised:"
                                  << freezeResult.toString();
            return false;
        }
    }

    // Freeze built-in constructors to prevent scripts from shadowing
    // Object.freeze, Object.defineProperty, etc.
    if (!criticalEval(
            QStringLiteral("Object.freeze(Object); Object.freeze(Array); Object.freeze(JSON); Object.freeze(Math);"),
            QStringLiteral("built-in constructor freeze"))) {
        return false;
    }

    // Close Object.constructor -> Function escape route on all major built-in objects
    safeEval(QStringLiteral("(function() {"
                            "  var undef = void 0;"
                            "  [Object, Array, String, Number, Boolean, RegExp, Date, Error,"
                            "   TypeError, RangeError, SyntaxError, ReferenceError, URIError, EvalError,"
                            "   Map, Set, WeakMap, WeakSet, Promise, JSON, Math"
                            "  ].forEach(function(C) {"
                            "    if (C && C.constructor) {"
                            "      try { Object.defineProperty(C, 'constructor', {value: undef, writable: false, "
                            "configurable: false}); } catch(e) {}"
                            "    }"
                            "  });"
                            "})();"),
             QStringLiteral("built-in constructor lockdown"));

    // Disable Proxy, Reflect, WeakRef, and FinalizationRegistry to prevent sandbox bypass
    {
        QJSValue global = engine->globalObject();
        QJSValue freezeObj = engine->evaluate(QStringLiteral("Object.freeze"));
        for (const auto& name : {QLatin1String("Proxy"), QLatin1String("Reflect"), QLatin1String("WeakRef"),
                                 QLatin1String("FinalizationRegistry")}) {
            QJSValue val = global.property(name);
            if (!val.isUndefined()) {
                freezeObj.call({val});
            }
            // Proxy is critical: it can intercept all property access and construct sandbox escapes
            disableGlobal(name, name == QLatin1String("Proxy"));
        }
    }

    // Disable globalThis to prevent sandbox bypass
    disableGlobal(QLatin1String("globalThis"));
    // Disable Symbol (critical — prevents Symbol.toPrimitive type-confusion attacks)
    if (!disableGlobal(QLatin1String("Symbol"), true)) {
        return false;
    }

    // Strip dangerous QJSEngine-provided globals via defineProperty (not deleteProperty)
    // to prevent scripts from re-creating them (e.g., Qt.createQmlObject() escape)
    // Also blocks the global `import` property. The `import()` syntax (dynamic import
    // expression) is not supported by QJSEngine V4 and is thus not a concern.
    {
        for (const auto& name : {QLatin1String("Qt"), QLatin1String("qsTr"), QLatin1String("qsTrId"),
                                 QLatin1String("print"), QLatin1String("console"), QLatin1String("setTimeout"),
                                 QLatin1String("setInterval"), QLatin1String("clearTimeout"),
                                 QLatin1String("clearInterval"), QLatin1String("gc"), QLatin1String("import")}) {
            disableGlobal(name);
        }
    }

    return true;
}

} // namespace PlasmaZones
