.pragma library

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Shared Loader.onLoaded utilities used by SettingsAppWindow chrome
// elements that mount consumer-supplied Component slots (Sidebar's
// footerContent, AboutPageShell's topContent). Centralised so the
// subtle "imperative Qt.binding assignment overrides any consumer
// Layout.fillWidth on the loaded item" pitfall has one place to
// document it.
//
// Usage from inside a Loader's onLoaded handler:
//   onLoaded: PhosphorLoaderHelpers.bindItemWidthToLoader(loaderId)

/**
 * Bind the loaded item's `width` to the Loader's own width.
 *
 * Why imperative `item.width = Qt.binding(...)` rather than a
 * declarative `binding`? The loaded item is created at runtime by
 * `sourceComponent`/`source`, so a declarative binding inside the
 * Loader cannot directly target the not-yet-instantiated child.
 * `onLoaded` runs once the item exists; assigning a Qt.binding from
 * there installs a real binding (re-evaluates whenever loader.width
 * changes) rather than a one-shot copy.
 *
 * KNOWN CONSUMER CONTRACT: consumer Components whose root sets
 * `Layout.fillWidth: true` will see this imperative assignment
 * override that — the loader's width wins. If a consumer needs a
 * narrower slot than the loader, they must compute their preferred
 * width inside their Component and the loader's parent layout has
 * to size the loader accordingly upstream.
 */
function bindItemWidthToLoader(loader) {
    if (loader && loader.item)
        loader.item.width = Qt.binding(function () {
            return loader.width;
        });
}

/**
 * Inject `value` into `item[propName]` only when the item declares
 * that property — and silently swallow the assignment when the
 * consumer's binding made it readonly. Centralises the
 * `hasOwnProperty + try-catch` idiom PageHost.qml uses three times
 * to forward a controller pointer into pages without crashing on
 * pages that bind their own controller.
 *
 * `value` may be `null` — the caller passes through whatever the
 * registry produced. PageHost treats null re-registration as
 * "keep prior" and won't call this function with a null value;
 * other consumers may need different policy.
 */
function injectIfAssignable(item, propName, value) {
    if (!item || !item.hasOwnProperty(propName))
        return;
    try {
        item[propName] = value;
    } catch (e) {
        // Page binds its own controller (readonly) — skip.
    }
}
