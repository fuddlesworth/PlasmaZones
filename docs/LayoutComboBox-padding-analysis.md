# LayoutComboBox Dropdown Padding Analysis

## Summary

Analysis of left/right padding for the KCM assignments layout dropdown to ensure alignment with KDE/Qt/Kirigami standards.

---

## Current State (LayoutComboBox.qml)

| Property     | Value                                              | Approx. Pixels |
|-------------|-----------------------------------------------------|----------------|
| leftPadding | `Kirigami.Units.smallSpacing`                       | 4px            |
| rightPadding| `smallSpacing + gridUnit * 1.5`                      | 4 + 12 = 16px  |
| delegate width | `root.popup.width`                             | Full popup     |

---

## Findings

### 1. Qt Quick Controls ItemDelegate Defaults

- **ItemDelegate** inherits from `Control` and uses style-defined padding.
- Qt Quick Controls 2 base implementation used `padding: 12` (uniform).
- **Breeze style** (KDE's default) provides its own padding; exact values are style-dependent.
- Overriding `leftPadding`/`rightPadding` bypasses the style and can diverge from other KDE dropdowns.

### 2. Kirigami Units (KDE HIG)

| Unit          | Typical Value | Use Case                          |
|---------------|---------------|-----------------------------------|
| gridUnit      | 8px           | Base layout unit, major spacing   |
| smallSpacing  | 4px           | Minor spacing between elements    |
| largeSpacing  | 16px          | Section separation                |

### 3. In-Project Patterns

**FontPickerDialog.qml** (KCM):
- Uses **default ItemDelegate** (no explicit padding).
- Delegate width: `familyList.width - (ScrollBar.vertical.visible ? ScrollBar.vertical.width : 0)`
- **Key**: Delegate width excludes scrollbar, so content stays in viewport and style padding applies symmetrically.

**ControlBar.qml** (Editor template ComboBox):
- Uses **default ItemDelegate** (no explicit padding).
- Delegate width: `templateCombo.width` (combo box width, not popup).
- No scrollbar handling; relies on style.

**AppRulesCard.qml** (KCM):
- Uses **default ItemDelegate** (no explicit padding).
- Delegate width: `ListView.view.width`.
- Content: icon + labels + button.

### 4. Root Cause of Asymmetry

- Delegate uses `width: root.popup.width` (full popup width).
- When a scrollbar is visible, it occupies space on the right.
- Content is laid out in the full width, so the badge appears closer to the visible right edge (scrollbar).
- Extra right padding was added to compensate, but this is a workaround rather than fixing the geometry.

---

## Recommendations

### Option A: Match FontPickerDialog (Preferred)

1. **Remove explicit padding** – use the style’s default padding.
2. **Adjust delegate width** – use content area width, excluding scrollbar when visible:
   ```qml
   width: root.popup.contentItem ? (root.popup.contentItem.width - (root.popup.contentItem.ScrollBar?.vertical?.visible ? root.popup.contentItem.ScrollBar.vertical.width : 0)) : root.popup.width
   ```
   Or, if the popup’s list view is nested, use the list view’s effective width.
3. **Result**: Symmetric padding from the style, consistent with other KDE dropdowns.

### Option B: Keep Explicit Padding, Use Standard Values

If the popup structure makes Option A impractical:

- **leftPadding**: `Kirigami.Units.gridUnit` (8px) – common for list items.
- **rightPadding**: `Kirigami.Units.gridUnit` when no scrollbar; add scrollbar width when visible.
- Aligns with Kirigami spacing and keeps left/right balanced.

### Option C: Minimal Override

- Set only `rightPadding: leftPadding` to enforce symmetry.
- Do not override `leftPadding`; let the style define it.
- Ensures left and right match without changing the style’s left padding.

---

## Conclusion

The most standard approach is **Option A**: avoid custom padding and fix delegate width so the content area excludes the scrollbar, matching FontPickerDialog and letting the Breeze style provide consistent padding across KDE dropdowns.
