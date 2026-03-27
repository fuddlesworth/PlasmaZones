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
    // H2: Safe evaluate wrapper — checks for errors on all sandbox-hardening calls.
    // Non-critical hardening steps log warnings but do not abort.
    auto safeEval = [engine](const QString& code, const QString& context) {
        QJSValue result = engine->evaluate(code);
        if (result.isError()) {
            qCWarning(lcAutotile) << "ScriptedAlgorithm: sandbox hardening failed for" << context << ":"
                                  << result.toString();
        }
    };

    // H2: Critical evaluate wrapper — returns false if the hardening step fails.
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

    // m-8: DRY helper for the repeated Object.defineProperty pattern used to disable globals.
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

    // H2: Disable eval() and Function constructor to prevent dynamic code generation.
    // These are CRITICAL — if any fails, the sandbox cannot prevent arbitrary code execution.
    if (!disableGlobal(QLatin1String("eval"), true)) {
        return false;
    }
    if (!criticalEval(QStringLiteral("Object.defineProperty(Function.prototype, 'constructor', "
                                     "{value: undefined, writable: false, configurable: false});"),
                      QStringLiteral("Function.prototype.constructor lockdown"))) {
        return false;
    }
    // M2: Disable the Function global to prevent dynamic code generation
    if (!disableGlobal(QLatin1String("Function"), true)) {
        return false;
    }

    // C1: Freeze GeneratorFunction and AsyncFunction constructors to prevent sandbox bypass.
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
    // AsyncFunction lockdown uses safeEval because engines that lack async syntax (ES5/ES6)
    // will produce a SyntaxError at parse time — that's safe since the constructor can't be
    // reached either. On engines with async support, a failure here is logged but non-fatal
    // because the Function constructor (already locked above) is the primary escape vector.
    safeEval(
        QStringLiteral("(function(){"
                       "var af=Object.getPrototypeOf(async function(){}).constructor;"
                       "Object.defineProperty(af.prototype,'constructor',{value:undefined,writable:false,configurable:"
                       "false});"
                       "})();"),
        QStringLiteral("AsyncFunction constructor lockdown"));

    // S3: Freeze built-in prototypes to prevent prototype pollution.
    // H1: Includes String, Number, Boolean, RegExp, Date, Error, Map, Set
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

    // H1: Close Object.constructor -> Function escape route on all major built-in objects
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

    // B4: Disable Proxy, Reflect, WeakRef, and FinalizationRegistry to prevent sandbox bypass
    {
        QJSValue global = engine->globalObject();
        QJSValue freezeObj = engine->evaluate(QStringLiteral("Object.freeze"));
        for (const auto& name : {QLatin1String("Proxy"), QLatin1String("Reflect"), QLatin1String("WeakRef"),
                                 QLatin1String("FinalizationRegistry")}) {
            QJSValue val = global.property(name);
            if (!val.isUndefined()) {
                freezeObj.call({val});
            }
            disableGlobal(name);
        }
    }

    // C-2: Disable globalThis and Symbol to prevent sandbox bypass
    for (const auto& name : {QLatin1String("globalThis"), QLatin1String("Symbol")}) {
        disableGlobal(name);
    }

    // S1: Strip dangerous QJSEngine-provided globals via defineProperty (not deleteProperty)
    // to prevent scripts from re-creating them (e.g., Qt.createQmlObject() escape)
    // m-9: Also blocks import() as a forward-looking measure
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
