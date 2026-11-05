// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * Font construction helper for labels that need a whole-group `font:` binding
 * (a font.<sub> sibling next to a group binding is an illegal duplicate
 * binding that fails the whole document).
 *
 * withProps(base, extra) builds a font from `base` plus the caller's extra
 * properties, passing only the size dimension `base` actually carries: a
 * theme font holds exactly one of pixelSize / pointSize and the other reads
 * -1, which Qt.font() warns on. Pass `base` from the binding itself (e.g.
 * `FontUtils.withProps(Kirigami.Theme.smallFont, ...)`) so the binding stays
 * reactive to theme font changes.
 */
function withProps(base, extra) {
    const props = Object.assign({
        family: base.family
    }, extra);
    if (base.pixelSize > 0)
        props.pixelSize = base.pixelSize;
    else
        props.pointSize = base.pointSize;
    return Qt.font(props);
}
