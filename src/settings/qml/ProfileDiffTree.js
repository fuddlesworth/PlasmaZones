// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Path-grouping for the profile diff (see ProfileDiffView.qml).
 *
 * Diff rows arrive as a PATH ("Animations › Shader profile tree › Overrides ›
 * window.move › Duration"). Rendered flat, every row restates and elides the
 * whole breadcrumb. `buildRows` groups rows that share a prefix under one
 * parent node and returns the depth-annotated list the view's delegates draw.
 *
 * Kept as a plain script rather than inline functions so the grouping can be
 * exercised on its own — the connector geometry depends on `depth` and
 * `ancestors` being exactly right, and those are easy to get subtly wrong.
 */
.pragma library

/// Group @p rows into a tree by shared path prefix, then flatten depth-first.
/// Each result row is `{ label, depth, ancestors, entries, hasChildren,
/// source }` — `source` is the input row a leaf came from (null on grouping
/// nodes), carried through so a per-row action can hand the original row back
/// to the store.
function buildRows(rows) {
    const rootNode = _node("");

    for (let i = 0; i < (rows ? rows.length : 0); ++i) {
        const source = rows[i];
        const segments = source.segments !== undefined && source.segments !== null && source.segments.length > 0 ? source.segments : [source.label];
        let node = rootNode;
        for (let s = 0; s < segments.length; ++s) {
            const label = segments[s];
            // A row may carry a `treeKey` making its LEAF node unique even
            // when another row shares its display label (two unnamed rules
            // with the same match summary must not merge into one row).
            const mapKey = s === segments.length - 1 && source.treeKey !== undefined ? String(source.treeKey) : label;
            let child = node.byLabel[mapKey];
            if (child === undefined) {
                child = _node(label);
                node.byLabel[mapKey] = child;
                node.children.push(child);
            }
            node = child;
        }
        node.entries = source.entries;
        // The input row's OWN `source` field (the store row a revert hands
        // back), not the whole view row — the view row nests it.
        node.source = source.source !== undefined ? source.source : null;
    }

    _factorSuffixes(rootNode);
    _collapseChains(rootNode);

    const out = [];
    _flatten(rootNode, [], out);
    return out;
}

function _node(label) {
    return {
        "label": label,
        "children": [],
        "byLabel": Object.create(null),
        "entries": null,
        "source": null
    };
}

/// Hoist a suffix shared by sibling sub-trees up onto their parent.
///
/// Siblings often differ only in their FIRST segment and then repeat the same
/// tail: every shader-profile override is `<animation path> › Profile › Effect
/// id`. Left alone, the chain fold below glues that tail onto each row, and the
/// part that actually distinguishes the rows (the animation path) is what gets
/// elided. Factoring the tail onto the parent leaves each child holding only
/// its own name:
///
///     Overrides › Profile › Effect id
///       ├ desktop                  unset → desktop-crosszoom
///       └ window.appearance.open   unset → phosphor-condense
function _factorSuffixes(node) {
    for (let i = 0; i < node.children.length; ++i) {
        _factorSuffixes(node.children[i]);
    }
    if (node.children.length < 2) {
        return;
    }

    // Group siblings by the tail their single leaf hangs off. A child with more
    // than one leaf has no single tail to hoist and stays where it is.
    const groups = Object.create(null);
    const order = [];
    for (let c = 0; c < node.children.length; ++c) {
        const tail = _soleLeafPath(node.children[c]);
        // An empty tail means the child IS the leaf; there is nothing to factor.
        // Unit separator, not a space: segments contain spaces, so a space
        // join could collide two different tails into one group key. Escaped
        // (never a raw control byte) so the file stays plain text.
        const key = tail === null || tail.length === 0 ? null : tail.join("\u001f");
        if (key === null) {
            continue;
        }
        if (groups[key] === undefined) {
            groups[key] = {
                "tail": tail,
                "members": []
            };
            order.push(key);
        }
        groups[key].members.push(node.children[c]);
    }

    // Only a tail shared by at least two siblings is worth a node of its own.
    const rebuilt = [];
    const claimed = Object.create(null);
    for (let o = 0; o < order.length; ++o) {
        const group = groups[order[o]];
        if (group.members.length < 2) {
            continue;
        }
        for (let m = 0; m < group.members.length; ++m) {
            claimed[group.members[m].label] = order[o];
        }
    }
    const emitted = Object.create(null);
    for (let k = 0; k < node.children.length; ++k) {
        const child = node.children[k];
        const key = claimed[child.label];
        if (key === undefined) {
            rebuilt.push(child);
            continue;
        }
        if (emitted[key] === undefined) {
            const group = groups[key];
            const holder = _node(group.tail.join(" › "));
            for (let m = 0; m < group.members.length; ++m) {
                const member = group.members[m];
                const leaf = _node(member.label);
                const sole = _soleLeaf(member);
                leaf.entries = sole.entries;
                leaf.source = sole.source;
                holder.children.push(leaf);
            }
            emitted[key] = true;
            rebuilt.push(holder);
        }
    }
    node.children = rebuilt;
}

/// The path from @p node down to its single leaf, or null when the sub-tree
/// branches or carries a value partway down (either way there is no one tail).
function _soleLeafPath(node) {
    const path = [];
    let current = node;
    while (true) {
        if (current.entries !== null) {
            return current.children.length === 0 ? path : null;
        }
        if (current.children.length !== 1) {
            return null;
        }
        current = current.children[0];
        path.push(current.label);
    }
}

/// The single leaf node under @p node (see _soleLeafPath).
function _soleLeaf(node) {
    let current = node;
    while (current.entries === null && current.children.length === 1) {
        current = current.children[0];
    }
    return current;
}

/// Fold a node that only ever leads to one child into that child ("Rendering"
/// → "Backend" becomes "Rendering › Backend"). Without this, a path that never
/// branches costs one indent level per segment and says nothing at each one.
function _collapseChains(node) {
    for (let i = 0; i < node.children.length; ++i) {
        let child = node.children[i];
        while (child.entries === null && child.children.length === 1) {
            const only = child.children[0];
            only.label = child.label + " › " + only.label;
            child = only;
        }
        _collapseChains(child);
        node.children[i] = child;
    }
}

/// Depth-first walk. @p ancestors carries the last-child flag of every node on
/// the path INCLUDING the one being emitted, which is the axis the row's Canvas
/// indexes when deciding which ancestor columns are still open below it.
function _flatten(node, ancestors, out) {
    for (let i = 0; i < node.children.length; ++i) {
        const child = node.children[i];
        const path = ancestors.concat([i === node.children.length - 1]);
        out.push({
            "label": child.label,
            "depth": path.length,
            "ancestors": path,
            "entries": child.entries || [],
            "hasChildren": child.children.length > 0,
            "source": child.source
        });
        _flatten(child, path, out);
    }
}
