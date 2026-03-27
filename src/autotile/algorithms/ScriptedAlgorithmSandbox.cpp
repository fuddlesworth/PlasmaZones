// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmSandbox.h"
#include "core/logging.h"
#include <QJSEngine>
#include <QJSValue>
#include <QLatin1String>
#include <QString>

namespace PlasmaZones {

bool hardenSandbox(QJSEngine* engine)
{
    // H2: Safe evaluate wrapper — checks for errors on all sandbox-hardening calls
    auto safeEval = [engine](const QString& code, const QString& context) {
        QJSValue result = engine->evaluate(code);
        if (result.isError()) {
            qWarning() << "ScriptedAlgorithm: sandbox hardening failed for" << context << ":" << result.toString();
        }
    };

    // m2: Freeze built-in helper globals so scripts cannot overwrite them
    auto freezeGlobal = [engine](const char* name) {
        QJSValue result = engine->evaluate(
            QStringLiteral("Object.defineProperty(this, '%1', {writable: false, configurable: false});")
                .arg(QLatin1String(name)));
        if (result.isError()) {
            qWarning() << "ScriptedAlgorithm: sandbox hardening failed for freezeGlobal" << QLatin1String(name) << ":"
                       << result.toString();
        }
    };
    freezeGlobal("applyTreeGeometry");
    freezeGlobal("lShapeLayout");
    freezeGlobal("deckLayout");
    freezeGlobal("distributeEvenly");

    // H2: Disable eval() and Function constructor to prevent dynamic code generation
    safeEval(QStringLiteral(
                 "Object.defineProperty(this, 'eval', {value: undefined, writable: false, configurable: false});"),
             QStringLiteral("eval lockdown"));
    safeEval(QStringLiteral("Object.defineProperty(Function.prototype, 'constructor', "
                            "{value: undefined, writable: false, configurable: false});"),
             QStringLiteral("Function.prototype.constructor lockdown"));
    // M2: Disable the Function global to prevent dynamic code generation
    safeEval(QStringLiteral(
                 "Object.defineProperty(this, 'Function', {value: undefined, writable: false, configurable: false});"),
             QStringLiteral("Function global lockdown"));

    // C1: Freeze GeneratorFunction and AsyncFunction constructors to prevent sandbox bypass
    safeEval(
        QStringLiteral("(function(){"
                       "try{var gf=Object.getPrototypeOf(function*(){}).constructor;"
                       "Object.defineProperty(gf.prototype,'constructor',{value:undefined,writable:false,configurable:"
                       "false});}catch(e){}"
                       "try{var af=Object.getPrototypeOf(async function(){}).constructor;"
                       "Object.defineProperty(af.prototype,'constructor',{value:undefined,writable:false,configurable:"
                       "false});}catch(e){}"
                       "})();"),
        QStringLiteral("generator/async constructor lockdown"));

    // S3: Freeze Object.prototype and Array.prototype in a separate call.
    // If this fails, the sandbox is compromised — caller must abort.
    {
        QJSValue freezeResult =
            engine->evaluate(QStringLiteral("Object.freeze(Object.prototype);Object.freeze(Array.prototype);"));
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
            safeEval(QStringLiteral(
                         "Object.defineProperty(this, '%1', {value: undefined, writable: false, configurable: false});")
                         .arg(name),
                     QStringLiteral("disable %1").arg(name));
        }
    }

    // S1: Strip dangerous QJSEngine-provided globals via defineProperty (not deleteProperty)
    // to prevent scripts from re-creating them (e.g., Qt.createQmlObject() escape)
    {
        static const char* const dangerousGlobals[] = {
            "Qt",         "qsTr",        "qsTrId",       "print",         "console",
            "setTimeout", "setInterval", "clearTimeout", "clearInterval", "gc"};
        for (const char* name : dangerousGlobals) {
            safeEval(QStringLiteral("Object.defineProperty(this, '%1', "
                                    "{value: undefined, writable: false, configurable: false});")
                         .arg(QLatin1String(name)),
                     QStringLiteral("disable %1").arg(QLatin1String(name)));
        }
    }

    return true;
}

} // namespace PlasmaZones
