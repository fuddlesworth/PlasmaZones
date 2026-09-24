// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Browser fixtures, not live bindings. Defaults and applicability come from
// src/config/configdefaults*.h and shortcutmanager_catalog.cpp. Keep individual
// actions so a customized member never inherits a compressed family's chord.
window.PhosphorShortcutCatalog = (() => {
  const rows = [];
  const add = (id,label,chord,group,modes='all',extra={}) => rows.push({id,label,triggers:chord?[chord]:[],group,modes,...extra});
  const directions = ['Left','Right','Up','Down'];
  for(const [prefix,label,modifiers,group,modes,familyLabel] of [
    ['focus_zone','Focus','Alt+Shift','Focus','all','Focus a window'],
    ['move_window','Move window','Meta+Alt+Shift','Arrange','all','Move a window'],
    ['swap_window','Swap window','Meta+Ctrl+Alt','Arrange','all','Swap two windows'],
    ['span_window','Extend / shrink span','Ctrl+Alt','Zones','snapping','Extend or shrink a span'],
    ['swap_virtual_screen','Swap virtual screen','Meta+Ctrl+Alt+Shift','Virtual screens','general','Swap virtual screens']
  ]) for(const direction of directions) add(`${prefix}_${direction.toLowerCase()}`,`${label} ${direction.toLowerCase()}`,`${modifiers}+${direction}`,group,modes,{family:prefix,familyLabel});
  for(let i=1;i<=9;i++) {
    add(`snap_to_zone_${i}`,`Move to slot ${i}`,`Meta+Ctrl+${i}`,'Arrange','all',{family:'slots',familyLabel:'Move to numbered slot',description:'Addresses a zone, tiling slot, or visible scrolling tile. Only slots 1–9 have numbered shortcuts.'});
    add(`quick_layout_${i}`,`Quick layout ${i}`,`Meta+Alt+${i}`,'Layouts','layouts',{family:'layouts',familyLabel:'Quick layout / template'});
    add(`scroll_focus_tab_${i}`,`Focus tab ${i}`,'','Tabs','scrolling',{family:'tabs',familyLabel:'Focus numbered tab'});
  }
  for(const row of [
    ['focus_master','Focus master','Meta+Shift+M','Focus','tiling'],
    ['swap_master','Swap with master','Meta+Shift+Return','Arrange','tiling'],
    ['increase_master_ratio','Increase master size','Meta+Shift+L','Sizing','tiling'],
    ['decrease_master_ratio','Decrease master size','Meta+Shift+H','Sizing','tiling'],
    ['increase_master_count','Add a master window','Meta+Ctrl+=','Sizing','tiling'],
    ['decrease_master_count','Remove a master window','Meta+Ctrl+-','Sizing','tiling'],
    ['toggle_window_float','Toggle floating','Meta+F','Arrange'],
    ['scroll_switch_focus_float_tiling','Switch floating / placed focus','Meta+Alt+X','Focus'],
    ['restore_window_size','Restore window size','Meta+Alt+Escape','Sizing'],
    ['retile','Retile windows','Meta+Ctrl+T','Sizing','managed'],
    ['push_to_empty_zone','Move to empty zone','Meta+Alt+Return','Zones','snapping'],
    ['cycle_window_forward','Cycle windows forward','Meta+Alt+.','Focus'],
    ['cycle_window_backward','Cycle windows backward','Meta+Alt+,','Focus'],
    ['rotate_windows_clockwise','Rotate windows clockwise','Meta+Ctrl+]','Arrange'],
    ['rotate_windows_counterclockwise','Rotate windows counterclockwise','Meta+Ctrl+[','Arrange'],
    ['toggle_autotile','Cycle placement mode','Meta+Shift+T','Layouts'],
    ['layout_picker','Choose layout / template','Meta+Alt+Space','Layouts','layouts'],
    ['previous_layout','Previous layout / template','Meta+Alt+[','Layouts','layouts'],
    ['next_layout','Next layout / template','Meta+Alt+]','Layouts','layouts'],
    ['toggle_layout_lock','Lock current layout','Meta+Ctrl+L','Layouts','layouts'],
    ['resnap_to_new_layout','Reapply layout','Meta+Ctrl+Z','Layouts'],
    ['snap_all_windows','Arrange all windows','Meta+Ctrl+S','Layouts'],
    ['open_editor','Open layout editor','Meta+Shift+E','General','general'],
    ['open_settings','Open settings','Meta+Shift+P','General','general'],
    ['toggle_cheatsheet','Keyboard shortcuts','Meta+Alt+/','General','general',{description:'Show shortcuts for window placement and layout commands.'}],
    ['rotate_virtual_screens_clockwise','Rotate screens clockwise','Meta+Ctrl+Alt+]','Virtual screens','general'],
    ['rotate_virtual_screens_counterclockwise','Rotate screens counterclockwise','Meta+Ctrl+Alt+[','Virtual screens','general']
  ]) add(...row);
  for(const [id,label,chord,group,description] of [
    ['focus_column_first','Focus first column','Meta+Alt+Home','Focus'],
    ['focus_column_last','Focus last column','Meta+Alt+End','Focus'],
    ['focus_window_top','Focus top window','Meta+Alt+V','Focus'],
    ['focus_window_bottom','Focus bottom window','Meta+Alt+Shift+V','Focus'],
    ['focus_column_left','Focus column left','','Focus'],
    ['focus_column_right','Focus column right','','Focus'],
    ['focus_column_left_or_last','Focus left, wrapping','','Focus'],
    ['focus_column_right_or_first','Focus right, wrapping','','Focus'],
    ['move_column_to_first','Move column to start','Meta+Alt+Shift+Home','Columns'],
    ['move_column_to_last','Move column to end','Meta+Alt+Shift+End','Columns'],
    ['consume_window','Add window to column','Meta+Alt+I','Columns','Pull a neighboring window into the focused column.'],
    ['expel_window','Split into own column','Meta+Alt+Shift+I','Columns','Give this window its own column in the strip.'],
    ['consume_or_expel_left','Add or split toward left','Meta+Alt+U','Columns'],
    ['consume_or_expel_right','Add or split toward right','Meta+Alt+O','Columns'],
    ['move_to_floating','Move to floating','','Columns'],
    ['move_to_tiling','Move to scrolling layout','','Columns'],
    ['center_column','Center column','Meta+Alt+C','View'],
    ['center_visible_columns','Center visible columns','Meta+Alt+Shift+C','View'],
    ['view_page_back','Pan one page back','Meta+Alt+Y','View'],
    ['view_page_forward','Pan one page forward','Meta+Alt+Shift+Y','View'],
    ['toggle_windowed_fullscreen','Fullscreen within tile','Meta+Alt+Shift+F','View','Changes the app’s fullscreen presentation while keeping its tile in the strip.'],
    ['toggle_column_tabbed','Toggle column tabs','Meta+Alt+T','Tabs'],
    ['cycle_tab','Next tab','Meta+Alt+Tab','Tabs'],
    ['cycle_tab_back','Previous tab','Meta+Alt+Shift+Tab','Tabs'],
    ['cycle_column_width','Next column width','Meta+Alt+PgUp','Column size'],
    ['cycle_column_width_back','Previous column width','Meta+Alt+PgDown','Column size'],
    ['increase_column_width','Increase column width','Meta+Alt+W','Column size'],
    ['decrease_column_width','Decrease column width','Meta+Alt+Shift+W','Column size'],
    ['maximize_column','Maximize column','Meta+Alt+F','Column size'],
    ['maximize_to_edges','Maximize column to edges','Meta+Alt+M','Column size'],
    ['expand_column','Grow into empty space','Meta+Alt+E','Column size'],
    ['minimize_column_width','Minimize column width','Meta+Alt+Shift+E','Column size'],
    ['equalize_column_widths','Equalize column widths','Meta+Ctrl+Shift+T','Column size'],
    ['cycle_window_height','Next window height','Meta+Alt+Shift+PgUp','Window size'],
    ['cycle_window_height_back','Previous window height','Meta+Alt+Shift+PgDown','Window size'],
    ['increase_window_height','Increase window height','Meta+Alt+H','Window size'],
    ['decrease_window_height','Decrease window height','Meta+Alt+Shift+H','Window size'],
    ['maximize_window_height','Maximize window height','Meta+Ctrl+Alt+F','Window size'],
    ['expand_window','Grow into empty space','Meta+Ctrl+Alt+E','Window size'],
    ['minimize_window_height','Minimize window height','Meta+Ctrl+Alt+Shift+E','Window size'],
    ['equalize_window_heights','Equalize window heights','Meta+Ctrl+Alt+T','Window size']
  ]) add(`scroll_${id}`,label,chord,group,'scrolling',{description});
  for(const [id,label] of [['launcher.toggle','Launcher'],['dashboard.toggle','Workspace overview'],['control-center.toggle','Quick settings'],['notify.toggle','Notification center'],['power.toggle','Session menu'],['picker.toggle','Appearance'],['lock.lock','Lock session'],['cheatsheet.toggle','Shell shortcut reference']]) {
    add(id,label,'','Shell','shell',{external:true,description:'This shell action uses a compositor binding. It is not part of the registered shortcut catalog.'});
  }
  return rows;
})();
