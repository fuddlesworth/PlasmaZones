// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const $ = selector => document.querySelector(selector);
const $$ = selector => [...document.querySelectorAll(selector)];
const icon = name => `<svg class="icon" aria-hidden="true"><use href="#${name}"/></svg>`;
const defaults = {palette:'spectrum',material:'glass',edge:'top',density:'comfortable',radius:18,gap:16,glow:true,media:true,motion:true,visualizer:'ribbon'};
const presets = {
  phosphor:{...defaults},
  paper:{...defaults,palette:'wallpaper',material:'light',radius:24,gap:22,glow:false},
  ember:{...defaults,palette:'ember',material:'solid',density:'compact',radius:8,gap:10,edge:'bottom',glow:false,media:false}
};
let settings = {...defaults};
try {
  const saved = JSON.parse(localStorage.getItem('phosphor-design-settings') || 'null');
  if (saved) for (const key of Object.keys(defaults)) {
    const choices = {palette:['spectrum','wallpaper','ember'],material:['glass','solid','light'],edge:['top','bottom'],density:['comfortable','compact'],visualizer:['ribbon','bars','halo','off']};
    if (choices[key]?.includes(saved[key])) settings[key] = saved[key];
    else if (typeof defaults[key] === 'boolean' && typeof saved[key] === 'boolean') settings[key] = saved[key];
    else if (key === 'radius' && Number.isFinite(saved[key])) settings[key] = Math.min(30,Math.max(4,saved[key]));
    else if (key === 'gap' && Number.isFinite(saved[key])) settings[key] = Math.min(30,Math.max(6,saved[key]));
  }
} catch { /* Storage is optional when opening a local file. */ }

const windows = [
  {id:'editor',app:'Kate',title:'Shell.qml',type:'editor',icon:'terminal',workspace:0},
  {id:'browser',app:'Firefox',title:'Phosphor · Surface library',type:'browser',icon:'globe',workspace:0},
  {id:'terminal',app:'Konsole',title:'shell-design',type:'terminal',icon:'terminal',workspace:0},
  {id:'build',app:'Konsole',title:'Build output',type:'terminal',icon:'terminal',workspace:1},
  {id:'docs',app:'Firefox',title:'Qt Quick documentation',type:'browser',icon:'globe',workspace:1},
  {id:'music',app:'Music',title:'Tycho · Dive',type:'music',icon:'music',workspace:2},
  {id:'files',app:'Dolphin',title:'Music collection',type:'files',icon:'folder',workspace:2},
  {id:'listen-browser',app:'Firefox',title:'Phosphor · Surface library',type:'browser',icon:'globe',workspace:2},
  {id:'listen-terminal',app:'Konsole',title:'shell-design',type:'terminal',icon:'terminal',workspace:2}
];
const workspaces = ['Develop','Build','Listen'];
const state = {study:'navigator',view:'overview',workspace:0,focused:'editor',mode:'tiling',modes:['tiling','tiling','scrolling'],offset:0,snapSlots:{},detail:null,
  wifi:true,bluetooth:true,dnd:false,night:false,playing:true,volume:64,brightness:78,network:'Home network',device:'Headphones',query:'',filter:'all',resultIndex:0};
// Match the fixed date/time in the design scene. Agenda entries are sample data.
const previewToday = new Date(2026,8,12,12);
const calendar = {year:2026,month:8,selected:new Date(2026,8,12,12)};
const dateKey = date => `${date.getFullYear()}-${date.getMonth()+1}-${date.getDate()}`;
const sampleEvents = {
  '2026-9-12':[['11:00','Shell design review','45 min · Design','var(--c2)'],['15:30','Focus time','1 hour · Personal','var(--c3)']],
  '2026-9-14':[['09:30','Weekly planning','30 min · Personal','var(--c2)']],
  '2026-9-17':[['14:00','Prototype review','1 hour · Design','var(--c3)']],
  '2026-9-21':[['10:00','Plan the next iteration','30 min · Design','var(--c2)']]
};
let toastTimer, osdTimer, lastResults = [], returnFocus = null;
const desktop = $('#desktop');
const currentWindows = () => windows.filter(w => w.workspace === state.workspace);
const selectedWindow = () => windows.find(w => w.id === state.focused);
const hue = index => ['var(--c1)','var(--c3)','var(--c4)','var(--c2)'][index % 4];
const escapeHTML = text => String(text).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

function geometry(list = currentWindows(), mode = state.mode) {
  const n = list.length;
  return list.map((w,i) => {
    if (mode === 'scrolling') return {x:i*.51,y:0,w:.5,h:1};
    if (mode === 'snapping') {
      const slot = state.snapSlots[w.id] ?? i;
      return {x:(slot%2)*.5,y:Math.floor(slot/2)*.5,w:.5,h:.5};
    }
    if (n === 1) return {x:0,y:0,w:1,h:1};
    if (n === 2) return {x:i*.5,y:0,w:.5,h:1};
    if (i === 0) return {x:0,y:0,w:.58,h:1};
    return {x:.58,y:(i-1)/(n-1),w:.42,h:1/(n-1)};
  });
}

function mappedGeometry(list = currentWindows(), mode = state.mode) {
  const rects = geometry(list, mode);
  const extent = mode === 'scrolling' ? Math.max(1,list.length*.51-.01) : 1;
  return rects.map(r => ({...r,x:r.x/extent,w:r.w/extent}));
}

function rectStyle(r, gap = 4) {
  return `left:calc(${r.x*100}% + ${gap/2}px);top:calc(${r.y*100}% + ${gap/2}px);width:calc(${r.w*100}% - ${gap}px);height:calc(${r.h*100}% - ${gap}px)`;
}

function normalizeFocus() {
  const list = currentWindows();
  if (!list.some(w => w.id === state.focused)) state.focused = list[0]?.id || null;
  if (state.mode === 'scrolling') {
    const index = Math.max(0,list.findIndex(w => w.id === state.focused));
    state.offset = Math.max(0,Math.min(index*.51-.25,list.length*.51-1.01));
  } else state.offset = 0;
}

function renderBar() {
  const list = currentWindows(), rects = mappedGeometry();
  const scrolling=state.mode==='scrolling',full=geometry();
  const before=scrolling?full.filter(r=>r.x+r.w<=state.offset+.001).length:0;
  const after=scrolling?full.filter(r=>r.x>=state.offset+1-.001).length:0;
  const mini=list.map((w,i)=>{
    const r=scrolling?{...full[i],x:full[i].x-state.offset}:rects[i];
    if(scrolling&&(r.x+r.w<=0||r.x>=1))return '';
    return `<span class="${w.id===state.focused?'focused':''}" style="${rectStyle(r,3)};--hue:${hue(i)}"></span>`;
  }).join('');
  $('#bar').innerHTML = `
    <button class="brand" data-view="launcher" aria-label="Open launcher">φ</button>
    <span class="bar-separator"></span><span class="bar-app">${escapeHTML(selectedWindow()?.app || 'Desktop')} &nbsp; / &nbsp; ${escapeHTML(selectedWindow()?.title || 'Empty workspace')}</span>
    ${settings.media ? `<button class="bar-media" data-view="controls" aria-label="Open media controls">${icon('music')} A Walk ${settings.visualizer!=='off'?'<canvas class="bar-spectrum" data-visualizer="mini" width="80" height="32" aria-hidden="true"></canvas>':''}</button>` : ''}
    <div class="bar-center">
      <button class="map-trigger" data-view="overview" aria-label="Open window navigation" aria-expanded="${state.view==='overview'}">
        ${scrolling?`<span class="map-overflow ${before?'':'empty'}" aria-label="${before} windows before viewport">${before?`+${before}`:'·'}</span>`:''}
        <span class="bar-map">${mini}</span>
        ${scrolling?`<span class="map-overflow ${after?'':'empty'}" aria-label="${after} windows after viewport">${after?`+${after}`:'·'}</span>`:''}
        <span class="map-caption"><b>${workspaces[state.workspace]}</b><br>${state.mode[0].toUpperCase()+state.mode.slice(1)} <span aria-hidden="true">⌄</span></span>
      </button>
      <div class="bar-workspaces">${workspaces.map((name,i)=>`<button data-workspace="${i}" aria-label="Switch to ${name}" aria-pressed="${i===state.workspace}">0${i+1}</button>`).join('')}</div>
    </div>
    <div class="bar-right"><button class="clock" data-view="datetime" aria-label="Open date and time" aria-expanded="${state.view==='datetime'}" aria-controls="datetime">Sat 12 &nbsp; <span style="color:var(--text)">10:24</span></button>
    <button class="status-cluster" data-view="controls" aria-label="Open quick settings" aria-expanded="${state.view==='controls'}">${icon('wifi')}${icon('volume')}<span>82%</span></button>
    <button class="settings-trigger" data-customize aria-label="Customize shell">◈</button></div>`;
  syncVisualizer();
}

function windowContent(w) {
  if (w.type === 'editor') return `<div class="editor-body"><aside class="file-tree"><b>PHOSPHOR SHELL</b>⌄ &nbsp; qml<br>&nbsp; ⌄ &nbsp; components<br>&nbsp; &nbsp; &nbsp; Bar.qml<br>&nbsp; &nbsp; &nbsp; Surface.qml<br><div class="selected">&nbsp; &nbsp; &nbsp; Shell.qml</div>&nbsp; &nbsp; &nbsp; WorkspaceMap.qml<br><br>› &nbsp; services<br>› &nbsp; themes<br>› &nbsp; shaders</aside><div class="code-pane"><div class="code-tab"><span>Shell.qml</span><span style="color:var(--muted)">main ●</span></div><pre><span class="keyword">import</span> QtQuick
<span class="keyword">import</span> Phosphor.Shell
<span class="keyword">import</span> Phosphor.Theme

<span class="string">Shell</span> {
    id: desktop

    <span class="string">Bar</span> {
        placement: <span class="value">Top</span>
        workspaceMap: <span class="value">true</span>
        surface: Theme.glass
    }

    <span class="string">WindowPlacement</span> {
        mode: <span class="value">Tiling</span>
        gap: Theme.spacing
    }
}</pre></div></div><div class="code-status"><span>⑂ shell-design &nbsp; ✓ No issues</span><span>QML &nbsp; UTF-8 &nbsp; 4 spaces</span></div>`;
  if (w.type === 'browser') return `<div class="browser-content"><div class="browser-url">⌕ &nbsp; ${w.id==='docs'?'doc.qt.io / qtquick':'phosphor / surfaces'}</div><small>PHOSPHOR / MATERIAL STUDIES</small><h2>Light defines<br>the edges.</h2><p>A shared color field connects the shell to the windows it manages. Depth gives each surface its place.</p><div class="sample-rail"></div><div class="sample-cards"><div>01 &nbsp; Glass</div><div>02 &nbsp; Ground</div><div>03 &nbsp; Light</div></div></div>`;
  if (w.type === 'terminal') return `<div class="terminal-content"><span class="prompt">❯</span> cmake --build build<br><span class="success">[128/128]</span> Linking phosphor-shell<br><br><span class="prompt">❯</span> ctest --test-dir build<br><span class="success">✓</span> shell geometry<br><span class="success">✓</span> workspace model<br><span class="success">✓</span> surface lifecycle<br><br><span class="success">100% tests passed.</span><br><span style="opacity:.6">Illustrative terminal content</span><br><br><span class="prompt">❯</span> <span style="color:var(--text)">▏</span></div>`;
  if (w.type === 'music') return `<div class="browser-content"><div class="album" style="width:100px;height:110px;margin-bottom:25px"></div><small>TYCHO / DIVE</small><h2>A Walk</h2><p>2:34 &nbsp; ━━━━━━━━━ &nbsp; 5:16</p><div class="sample-rail"></div><p>1. A Walk<br>2. Hours<br>3. Daydream<br>4. Dive</p></div>`;
  return `<div class="browser-content"><small>MUSIC COLLECTION</small><h2>Albums</h2><div class="sample-cards"><div>Dive</div><div>Awake</div><div>Epoch</div></div><p>3 folders · Local collection</p></div>`;
}

function renderWindows() {
  const list = currentWindows(), rects = geometry();
  $('#windows').style.overflow = state.mode==='scrolling'?'hidden':'visible';
  $('#windows').innerHTML = list.length ? list.map((w,i)=>{
    const r = {...rects[i],x:rects[i].x-state.offset};
    return `<article class="app-window ${w.id===state.focused?'focused':''}" style="${rectStyle(r,settings.gap)};--hue:${hue(i)}" data-select="${w.id}" tabindex="0" aria-label="Select ${escapeHTML(w.app+': '+w.title)}" role="button" aria-pressed="${w.id===state.focused}">
    <div class="window-title">${icon(w.icon)} ${escapeHTML(w.app)} <span style="opacity:.4">/</span> ${escapeHTML(w.title)} <span class="window-buttons">− ◇ ×</span></div>
    ${windowContent(w)}<span class="window-focus-label">${w.id===state.focused?'Selected · Enter to open':'Click to select'}</span></article>`;
  }).join('') : `<div class="material" style="position:absolute;left:25%;top:35%;width:50%;padding:40px;text-align:center"><h2 style="font-weight:500">An empty workspace</h2><p class="sub">Launch an app or move a window here.</p><button class="text-button" data-view="launcher">Open launcher →</button></div>`;
}

function modeControl() {
  return `<label class="mode-select">Placement <select data-mode aria-label="Placement mode">${['tiling','scrolling','snapping'].map(mode=>`<option value="${mode}" ${state.mode===mode?'selected':''}>${mode[0].toUpperCase()+mode.slice(1)}</option>`).join('')}</select></label>`;
}

function largeMap() {
  const list = currentWindows(), rects = mappedGeometry();
  if(state.mode==='scrolling') {
    const extent=Math.max(1,list.length*.51-.01)*220;
    return `<div class="scroll-map" tabindex="0" aria-label="All ${list.length} scrolling windows; scroll horizontally to browse"><div class="scroll-map-inner" style="width:${extent}px">${list.map((w,i)=>`<button class="map-cell" data-focus="${w.id}" aria-pressed="${w.id===state.focused}" style="left:${i*.51*220+3}px;--hue:${hue(i)}" aria-label="Focus window ${i+1} of ${list.length}: ${escapeHTML(w.app+': '+w.title)}">${icon(w.icon)}<span>WINDOW ${String(i+1).padStart(2,'0')}</span><b>${escapeHTML(w.app)}</b><span>${escapeHTML(w.title)}</span></button>`).join('')}<div class="strip-lens" style="left:${state.offset*220}px;width:220px" aria-hidden="true"></div></div></div>`;
  }
  const occupied = new Set(list.map((w,i)=>state.snapSlots[w.id] ?? i));
  const empty = state.mode==='snapping' ? [0,1,2,3].filter(i=>!occupied.has(i)).map(i=>`<button class="map-cell" data-zone="${i}" style="${rectStyle({x:(i%2)*.5,y:Math.floor(i/2)*.5,w:.5,h:.5},9)};--hue:var(--c2);background:transparent;border-style:dashed" aria-label="Move selected window to empty zone ${i+1}"><span>＋ Empty zone ${i+1}</span></button>`).join('') : '';
  const extent = Math.max(1,list.length*.51-.01);
  const lens = state.mode==='scrolling' ? `<div style="position:absolute;pointer-events:none;left:${state.offset/extent*100}%;width:${100/extent}%;top:0;bottom:0;border:2px solid var(--text);border-radius:8px;opacity:.65" aria-hidden="true"></div>` : '';
  return `<div class="large-map" aria-label="Current window placement">${list.map((w,i)=>`<button class="map-cell" data-focus="${w.id}" aria-pressed="${w.id===state.focused}" style="${rectStyle(rects[i],9)};--hue:${hue(i)}" aria-label="Focus ${escapeHTML(w.app+': '+w.title)}">${icon(w.icon)}<b>${escapeHTML(w.app)}</b><span>${escapeHTML(w.title)}</span></button>`).join('')}${empty}${lens}</div>`;
}

function renderOverview() {
  const el = $('#overview');
  el.className = state.view==='overview' ? (state.study==='navigator'?'material':'') : 'hidden';
  if (state.study === 'navigator') {
    el.innerHTML = `<div class="pane-heading"><div><small>Workspace 0${state.workspace+1} / ${workspaces[state.workspace]}</small><h2>Your windows, within reach.</h2></div>${modeControl()}<button class="close" data-dismiss aria-label="Close navigator">×</button></div>
    <div class="navigator-layout">${largeMap()}<div class="navigator-list">${currentWindows().map((w,i)=>`<button class="window-row" data-focus="${w.id}" aria-pressed="${w.id===state.focused}" style="--hue:${hue(i)}">${icon(w.icon)}<span><b>${escapeHTML(w.app)}</b><span class="sub">${escapeHTML(w.title)}</span></span></button>`).join('')}</div></div>
    ${state.mode==='scrolling'?`<div class="strip-navigation"><button data-strip-step="-1" ${currentWindows()[0]?.id===state.focused?'disabled':''}>← Previous</button><span>Window ${currentWindows().findIndex(w=>w.id===state.focused)+1} of ${currentWindows().length} · Drag scrollbar to browse</span><button data-strip-step="1" ${currentWindows().at(-1)?.id===state.focused?'disabled':''}>Next →</button></div>`:''}
    <div class="desktop-switcher">${workspaces.map((name,i)=>`<button class="desktop-choice" data-workspace="${i}" aria-pressed="${i===state.workspace}"><span>0${i+1} &nbsp; ${name}</span><span>${windows.filter(w=>w.workspace===i).length}</span></button>`).join('')}</div>
    <div class="pane-footer"><span><kbd>←</kbd> <kbd>→</kbd> Select &nbsp; <kbd>Enter</kbd> Focus &nbsp; <kbd>Esc</kbd> Close</span><span>${state.mode==='snapping'?'Empty zone → move selected window':'Click a window to focus it'}</span></div>`;
  } else {
    el.innerHTML = `<div class="stage-title"><div><span class="eyebrow">WORKSPACE 0${state.workspace+1} / ${state.mode.toUpperCase()}</span><h2>${workspaces[state.workspace]}</h2></div><span class="sub">Click to select · Enter or double-click to open</span></div>
    <nav class="stage-desktops" aria-label="Workspace thumbnails">${workspaces.map((name,index)=>{
      const list=windows.filter(w=>w.workspace===index),rects=mappedGeometry(list,state.modes[index]);
      return `<button class="stage-desktop" data-workspace="${index}" aria-pressed="${index===state.workspace}"><div class="thumb">${list.map((w,i)=>`<i style="${rectStyle(rects[i],5)};--hue:${hue(i)}"></i>`).join('')}</div><span class="desktop-name"><span>0${index+1} &nbsp; ${name}</span><span>${list.length}</span></span></button>`;
    }).join('')}</nav>
    <aside class="stage-inspector material"><div class="eyebrow">THIS WORKSPACE</div><h3>Shape the space</h3>${modeControl()}<span class="sub">${{tiling:'A main window with a supporting stack.',scrolling:'Columns continue beyond the viewport. Select a window to bring it into view.',snapping:'Four targets for deliberate placement.'}[state.mode]}</span><div class="inspector-rule"></div><div class="inspector-kicker">SELECTED WINDOW</div><b style="font-size:12px;font-weight:500">${escapeHTML(selectedWindow()?.app || 'No window')}</b><span class="sub" style="font-size:10px;margin-top:6px">${escapeHTML(selectedWindow()?.title || '')}</span><div class="inspector-rule"></div><div class="inspector-kicker">MOVE TO WORKSPACE</div><div class="move-buttons">${workspaces.map((name,i)=>`<button data-move="${i}" ${i===state.workspace || !state.focused?'disabled':''} aria-label="Move selected window to ${name}">0${i+1}</button>`).join('')}</div>${state.mode==='snapping'?`<div class="inspector-rule"></div><div class="inspector-kicker">PLACE IN ZONE</div><div class="move-buttons">${[0,1,2,3].map(i=>`<button data-zone="${i}" aria-label="Place selected window in zone ${i+1}">${i+1}</button>`).join('')}</div>`:''}</aside>
    <div class="stage-bottom"><span><kbd>←</kbd> <kbd>→</kbd> Select window &nbsp; <kbd>Enter</kbd> Open &nbsp; <kbd>Esc</kbd> Return</span><button class="text-button" data-dismiss>Return to desktop ↗</button></div>`;
  }
  const index=currentWindows().findIndex(w=>w.id===state.focused);
  const strip=$('#overview .scroll-map');
  if(strip)strip.scrollLeft=Math.max(0,(index*.51+.25)*220-strip.clientWidth/2);
  const row=$('#overview .window-row[aria-pressed=true]');
  if(row)row.parentElement.scrollTop=Math.max(0,row.offsetTop-row.parentElement.offsetTop-75);
}

function connectionRow(key,title,subtitle,symbol) {
  return `<div class="connection"><button class="connection-toggle" data-toggle="${key}" aria-pressed="${state[key]}" aria-label="Toggle ${title}">${icon(symbol)}<span><b>${title}</b><span class="sub">${state[key]?escapeHTML(subtitle):'Off'}</span></span></button><button class="connection-details" data-detail="${key}" aria-label="Choose ${title} device or network">›</button></div>`;
}

function slider(key,label,symbol) {
  return `<div class="slider-block"><label><span class="slider-label"><span>${icon(symbol)} ${label}</span><output id="${key}-output">${state[key]}%</output></span><input type="range" data-level="${key}" aria-label="${label}" min="0" max="100" value="${state[key]}" style="--value:${state[key]}%"></label></div>`;
}

function syncVisualizer() {
  window.PhosphorVisualizer.sync({playing:state.playing,motion:settings.motion,style:settings.visualizer,glow:settings.glow});
}

function mediaCard() {
  const styles=[['ribbon','Ribbon'],['bars','Bars'],['halo','Halo'],['off','Off']];
  return `<div class="media-card visual-media"><div class="media-top"><div class="album"></div><div class="track"><b>A Walk</b><span class="sub">Tycho · Dive</span></div><button class="play" data-play aria-label="${state.playing?'Pause':'Play'} music" aria-pressed="${state.playing}">${state.playing?'Ⅱ':'▶'}</button></div>
    ${settings.visualizer!=='off'?`<canvas class="media-visualizer" data-visualizer="main" width="640" height="180" role="img" aria-label="${settings.visualizer} visualizer with simulated audio"></canvas>`:''}
    <div class="visualizer-footer"><span>${settings.visualizer==='off'?'2:34 / 5:16':state.playing?'Preview signal':'Paused'}</span><select data-viz-style aria-label="Media visualizer style">${styles.map(([id,name])=>`<option value="${id}" ${settings.visualizer===id?'selected':''}>${name}</option>`).join('')}</select></div></div>`;
}

function renderControls() {
  const el = $('#controls');
  el.className = state.view==='controls'?'material':'hidden';
  const heading = `<div class="controls-heading"><h2>${state.detail?'Connections':'Quick settings'}</h2><span class="battery-summary">${icon('battery')} 82% <span style="opacity:.6">· 6h left</span></span><button class="close" data-dismiss aria-label="Close quick settings">×</button></div>`;
  if (state.detail) {
    const wifi = state.detail==='wifi';
    const choices = wifi?['Home network','Studio 5G','Guest']:['Headphones','Speakers','USB DAC'];
    el.innerHTML = `${heading}<div class="detail-content"><button class="text-button" data-detail="back">← Quick settings</button><h3>${wifi?'Wi-Fi networks':'Audio devices'}</h3>${choices.map(name=>`<button class="device-row" data-device="${name}" aria-pressed="${name===(wifi?state.network:state.device)}"><span>${escapeHTML(name)}<span class="sub">${wifi?'Secured network':'Audio output'}</span></span><span>${name===(wifi?state.network:state.device)?'Connected ✓':'Connect'}</span></button>`).join('')}</div>`;
    syncVisualizer();
    return;
  }
  const connections = connectionRow('wifi','Wi-Fi',state.network,'wifi')+connectionRow('bluetooth','Bluetooth',state.device,'bluetooth');
  const pair = `<div class="quick-pair"><button data-toggle="dnd" aria-pressed="${state.dnd}">${icon('moon')} Focus ${state.dnd?'on':'off'}</button><button data-toggle="night" aria-pressed="${state.night}">${icon('sun')} Night light ${state.night?'on':'off'}</button></div>`;
  const levels = slider('volume','Volume','volume')+`<button class="device-button" data-detail="bluetooth">${escapeHTML(state.device)} <span>Change output ›</span></button>`+slider('brightness','Brightness','sun');
  el.innerHTML = state.study==='navigator' ? `${heading}${connections}${pair}${levels}${mediaCard()}<div class="pane-footer"><span>Balanced power</span><button class="text-button" data-customize>Appearance ↗</button></div>` : `${heading}<div class="shelf-grid"><div class="shelf-column"><h3>CONNECTIONS & FOCUS</h3>${connections}${pair}</div><div class="shelf-column"><h3>SOUND & DISPLAY</h3>${levels}</div><div class="shelf-column"><h3>NOW PLAYING</h3>${mediaCard()}</div></div><div class="pane-footer"><span>Balanced power &nbsp; · &nbsp; No pending notifications</span><button class="text-button" data-customize>Appearance ↗</button></div>`;
  syncVisualizer();
}

function getResults() {
  const q = state.query.toLowerCase();
  const open = windows.map(w=>({label:w.title,sub:`${w.app} · ${workspaces[w.workspace]}`,icon:w.icon,kind:'windows',id:w.id}));
  const apps = [{label:'Firefox',sub:'Web browser',icon:'globe'},{label:'Kate',sub:'Text editor',icon:'terminal'},{label:'Dolphin',sub:'File manager',icon:'folder'},{label:'Konsole',sub:'Terminal',icon:'terminal'}].map(a=>({...a,kind:'apps',id:a.label}));
  const actions = [{label:'Customize shell',sub:'Colors, material, geometry',icon:'sun',kind:'actions',id:'customize'},{label:'Toggle do not disturb',sub:'Silence interruptions',icon:'moon',kind:'actions',id:'dnd'}];
  return [...open,...apps,...actions].filter(r=>(state.filter==='all'||r.kind===state.filter)&&`${r.label} ${r.sub}`.toLowerCase().includes(q)).slice(0,7);
}

function resultRows(results) {
  return results.map((r,i)=>`<button class="search-result ${i===state.resultIndex?'selected':''}" data-result="${i}" aria-label="${escapeHTML(r.label+', '+r.sub)}">${icon(r.icon)}<span><b>${escapeHTML(r.label)}</b><span class="sub">${escapeHTML(r.sub)}</span></span>${i===state.resultIndex?'<kbd>↵</kbd>':''}</button>`).join('') || '<div class="search-empty">No matches. Try an app name or window title.</div>';
}

function renderResults() {
  lastResults = getResults();
  state.resultIndex = Math.min(state.resultIndex,Math.max(0,lastResults.length-1));
  const results = $('#results');
  if (!results) return;
  if (state.study==='stage' && !state.query && state.filter==='all') {
    const recent = windows.filter(w=>w.workspace===state.workspace);
    lastResults = recent.map(w=>({label:w.title,sub:w.app,icon:w.icon,kind:'windows',id:w.id}));
    results.innerHTML = `<div class="launch-columns"><div><h3>PINNED APPLICATIONS</h3><div class="app-grid">${[['Firefox','globe'],['Kate','terminal'],['Dolphin','folder'],['Konsole','terminal']].map(([name,symbol])=>`<button class="app-tile" data-app="${name}">${icon(symbol)}${name}</button>`).join('')}</div></div><div class="launcher-recent"><h3>ON THIS WORKSPACE</h3>${resultRows(lastResults)}</div></div>`;
  } else results.innerHTML = resultRows(lastResults);
}

function renderLauncher() {
  $('#launcher').className = state.view==='launcher'?'material':'hidden';
  $('#launcher').innerHTML = `<div class="search-field">${icon('search')}<input id="launcher-search" aria-label="Search apps, windows, and actions" placeholder="${state.study==='stage'?'Search your desktop…':'Apps, windows, actions…'}" value="${escapeHTML(state.query)}" autocomplete="off"><kbd>Esc</kbd></div>
  <nav class="search-tabs" aria-label="Search provider">${[['all','All'],['windows','Windows'],['apps','Apps'],['actions','Actions']].map(([id,title])=>`<button data-filter="${id}" aria-pressed="${state.filter===id}">${title}</button>`).join('')}</nav><div id="results"></div><div class="pane-footer"><span><kbd>↑</kbd> <kbd>↓</kbd> Select &nbsp; <kbd>Enter</kbd> Open</span><span>Search by title or application</span></div>`;
  renderResults();
}

function renderNotes() {
  const stage = state.study==='stage';
  $('#study-kicker').textContent = stage?'B / SPATIAL OVERVIEW':'A / ANCHORED NAVIGATOR';
  $('#study-title').textContent = stage?'Work with the space.':'Stay in context.';
  $('#study-description').textContent = stage?'The actual desktop contracts into an overview. Workspaces sit beside it; placement and moving windows live in an inspector. Quick settings form a wide shelf.':'The branch’s map becomes a readable, anchored navigator. It exposes window titles, workspace switching, and placement mode without covering the entire desktop.';
  $('#ux-description').textContent = stage?'Click to select, Enter to return, or move the selection to another workspace. The large overview favors spatial editing; it deliberately takes you out of the working view.':'One click on a mapped window focuses it and dismisses the navigator. Settings stay beside their trigger. Separate toggles and chevrons distinguish changing a state from choosing a device.';
}

function positionCalendar() {
  const clock=$('.clock'),panel=$('#datetime');
  if(!clock||!panel)return;
  const shellRect=desktop.getBoundingClientRect(),rect=clock.getBoundingClientRect();
  const scale=shellRect.width/1440;
  const center=(rect.left+rect.width/2-shellRect.left)/scale;
  desktop.style.setProperty('--calendar-left',`${Math.max(18,Math.min(1440-410-18,center-205))}px`);
}

function renderDateTime() {
  const panel=$('#datetime');panel.className=state.view==='datetime'?'material':'hidden';
  const first=new Date(calendar.year,calendar.month,1,12);
  const start=new Date(calendar.year,calendar.month,1-(first.getDay()+6)%7,12);
  const cells=Array.from({length:42},(_,i)=>new Date(start.getFullYear(),start.getMonth(),start.getDate()+i,12));
  const events=sampleEvents[dateKey(calendar.selected)] || [];
  panel.innerHTML=`<div class="datetime-top"><div><span class="eyebrow">SATURDAY, SEPTEMBER 12</span><div class="datetime-time">10<span>:</span>24</div></div><button class="close" data-dismiss aria-label="Close date and time">×</button></div>
    <div class="datetime-zone"><span>Chicago · CDT</span><span>UTC −05:00</span></div>
    <div class="month-toolbar"><h3 aria-live="polite">${first.toLocaleDateString('en-US',{month:'long',year:'numeric'})}</h3><div class="month-actions"><button data-month="-1" aria-label="Previous month">‹</button><button class="today-button" data-today>Today</button><button data-month="1" aria-label="Next month">›</button></div></div>
    <div class="calendar-weekdays" aria-hidden="true">${['M','T','W','T','F','S','S'].map(d=>`<span>${d}</span>`).join('')}</div>
    <div class="calendar-grid" role="group" aria-label="Choose a date">${cells.map(date=>{
      const key=dateKey(date),selected=key===dateKey(calendar.selected),today=key===dateKey(previewToday),count=sampleEvents[key]?.length||0;
      return `<button class="calendar-day ${date.getMonth()!==calendar.month?'outside-month':''} ${today?'is-today':''} ${count?'has-events':''}" data-date="${date.getTime()}" aria-label="${date.toLocaleDateString('en-US',{weekday:'long',month:'long',day:'numeric',year:'numeric'})}${count?`, ${count} sample events`:''}" aria-pressed="${selected}" ${today?'aria-current="date"':''} tabindex="${selected?0:-1}">${date.getDate()}</button>`;
    }).join('')}</div>
    <div class="calendar-agenda" aria-live="polite"><div class="agenda-heading"><h4>${calendar.selected.toLocaleDateString('en-US',{weekday:'long',month:'short',day:'numeric'})}</h4><span>Sample agenda</span></div>
    ${events.length?events.map(([time,title,detail,color])=>`<div class="agenda-event"><time>${time}</time><div style="--event-color:${color}"><b>${title}</b><span class="sub">${detail}</span></div></div>`).join(''):'<div class="agenda-empty">Nothing scheduled for this day.</div>'}</div>
    <div class="calendar-footer"><span>Arrow keys to browse · Esc to close</span><span>Preview date</span></div>`;
  positionCalendar();
}

function selectDate(date, focus=true) {
  calendar.selected=date;calendar.year=date.getFullYear();calendar.month=date.getMonth();
  renderDateTime();
  if(focus)$('#datetime [aria-pressed=true]')?.focus({preventScroll:true});
}

function shiftMonth(delta, focus=false) {
  const month=new Date(calendar.year,calendar.month+delta,1,12);
  const last=new Date(month.getFullYear(),month.getMonth()+1,0,12).getDate();
  selectDate(new Date(month.getFullYear(),month.getMonth(),Math.min(calendar.selected.getDate(),last),12),focus);
  if(!focus)$(`#datetime [data-month="${delta}"]`)?.focus({preventScroll:true});
}

function render() {
  normalizeFocus();
  desktop.classList.toggle('navigator',state.study==='navigator');
  desktop.classList.toggle('stage',state.study==='stage');
  desktop.classList.toggle('overview-open',state.view==='overview');
  $('#stage-shade').classList.toggle('hidden',!(state.study==='stage'&&state.view==='overview'));
  renderBar();renderWindows();renderOverview();renderControls();renderLauncher();renderDateTime();renderNotes();
  $$('[data-study]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.study===state.study));
  $$('.view-switch [data-view]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.view===state.view));
  history.replaceState(null,'',`#${state.study}/${state.view}`);
}

function applySettings() {
  for (const [key,value] of Object.entries(settings)) {
    if (key==='radius'||key==='gap') desktop.style.setProperty(`--${key}`,`${value}px`);
    else desktop.dataset[key] = String(value);
    const control = $(`[data-setting="${key}"]`);
    if (control) {
      if (control.type==='checkbox') control.checked=value;
      else control.value=value;
      if (control.type==='range') control.style.setProperty('--value',`${100*(value-Number(control.min))/(Number(control.max)-Number(control.min))}%`);
    }
  }
  $('#radius-value').textContent=`${settings.radius} px`;
  $('#gap-value').textContent=`${settings.gap} px`;
  try { localStorage.setItem('phosphor-design-settings',JSON.stringify(settings)); } catch { /* Optional persistence. */ }
  render();
}

function setView(view) {
  const previousView=state.view;
  if (view!=='desktop') returnFocus=document.activeElement;
  state.view=view;state.detail=null;
  render();
  if (view==='launcher') $('#launcher-search').focus();
  if (view==='datetime') $('#datetime [aria-pressed=true]')?.focus({preventScroll:true});
  if (view==='desktop') {
    if (previousView==='datetime') $('.clock').focus();
    else if (returnFocus?.isConnected) returnFocus.focus();
    else $('.map-trigger')?.focus();
  }
}

function notify(message) {
  clearTimeout(toastTimer);$('#toast').textContent=message;$('#toast').classList.remove('hidden');
  toastTimer=setTimeout(()=>$('#toast').classList.add('hidden'),2300);
}

function customize(open=true) {
  $('#customizer').classList.toggle('hidden',!open);
  $('#customize-toggle').setAttribute('aria-expanded',open);
}

function focusWindow(id, dismiss=true) {
  const w=windows.find(w=>w.id===id);if(!w)return;
  const workspaceChanged=state.workspace!==w.workspace;
  state.focused=id;state.workspace=w.workspace;
  state.mode=state.modes[state.workspace];
  if(dismiss)setView('desktop');
  else if(workspaceChanged)render();
  else {
    // Preserve the clicked element so double-click and keyboard focus survive selection.
    normalizeFocus();
    const rects=geometry();
    $$('.app-window').forEach((el,i)=>{
      const focused=el.dataset.select===id;
      el.classList.toggle('focused',focused);el.setAttribute('aria-pressed',focused);
      const r={...rects[i],x:rects[i].x-state.offset};
      el.style.cssText=`${rectStyle(r,settings.gap)};--hue:${hue(i)}`;
      el.querySelector('.window-focus-label').textContent=focused?'Selected · Enter to open':'Click to select';
    });
    renderBar();renderOverview();
  }
}

function launchApp(name) {
  const existing=windows.find(w=>w.app===name);
  if(existing) focusWindow(existing.id);
  else notify(`${name} selected in the mock`);
}

function runResult(index) {
  const r=lastResults[index];if(!r)return;
  if(r.kind==='windows')focusWindow(r.id);
  else if(r.kind==='apps')launchApp(r.id);
  else if(r.id==='customize'){setView('desktop');customize();}
  else {state.dnd=!state.dnd;setView('desktop');notify(`Do not disturb ${state.dnd?'on':'off'}`);}
}

function placeInZone(zone) {
  if(!state.focused)return;
  const list=currentWindows();
  const previous=state.snapSlots[state.focused] ?? list.findIndex(w=>w.id===state.focused);
  const occupant=list.find((w,i)=>(state.snapSlots[w.id] ?? i)===zone);
  if(occupant)state.snapSlots[occupant.id]=previous;
  state.snapSlots[state.focused]=zone;
  render();
}

document.addEventListener('click',e=>{
  const b=e.target.closest('button');
  if (b) {
    if(b.dataset.stripStep){const list=currentWindows(),i=list.findIndex(w=>w.id===state.focused);focusWindow(list[Math.max(0,Math.min(list.length-1,i+Number(b.dataset.stripStep)))].id,false);}
    if(b.dataset.month)shiftMonth(Number(b.dataset.month));
    if(b.hasAttribute('data-today'))selectDate(new Date(previewToday));
    if(b.dataset.date)selectDate(new Date(Number(b.dataset.date)));
    if(b.dataset.study){state.study=b.dataset.study;render();}
    if(b.dataset.view)setView(b.closest('#bar')&&state.view===b.dataset.view?'desktop':b.dataset.view);
    if(b.hasAttribute('data-dismiss'))setView('desktop');
    if(b.hasAttribute('data-customize'))customize();
    if(b.dataset.workspace!==undefined){state.workspace=Number(b.dataset.workspace);state.mode=state.modes[state.workspace];render();}
    if(b.dataset.focus)focusWindow(b.dataset.focus);
    if(b.dataset.zone!==undefined)placeInZone(Number(b.dataset.zone));
    if(b.dataset.move!==undefined){
      const w=selectedWindow();if(w){const target=Number(b.dataset.move);w.workspace=target;state.snapSlots={};render();notify(`${w.app} moved to ${workspaces[target]}`);}
    }
    if(b.dataset.toggle){state[b.dataset.toggle]=!state[b.dataset.toggle];renderControls();}
    if(b.dataset.detail){state.detail=b.dataset.detail==='back'?null:b.dataset.detail;renderControls();}
    if(b.dataset.device){if(state.detail==='wifi'){state.network=b.dataset.device;state.wifi=true;}else{state.device=b.dataset.device;state.bluetooth=true;}renderControls();}
    if(b.hasAttribute('data-play')){state.playing=!state.playing;renderControls();renderBar();}
    if(b.dataset.filter){state.filter=b.dataset.filter;state.resultIndex=0;renderLauncher();$('#launcher-search').focus();}
    if(b.dataset.result!==undefined)runResult(Number(b.dataset.result));
    if(b.dataset.app)launchApp(b.dataset.app);
    return;
  }
  const win=e.target.closest('[data-select]');
  if(win){focusWindow(win.dataset.select,state.view!=='overview');return;}
  if(e.target.closest('#desktop')&&!e.target.closest('#overview,#controls,#launcher,#datetime,#bar'))setView('desktop');
});

document.addEventListener('dblclick',e=>{
  const win=e.target.closest('[data-select]');
  if(win&&state.view==='overview')focusWindow(win.dataset.select);
});

document.addEventListener('change',e=>{
  if(e.target.id==='scroll-count') {
    for(let i=windows.length-1;i>=0;i--)if(windows[i].stress)windows.splice(i,1);
    const count=Number(e.target.value),base=windows.filter(w=>w.workspace===2);
    for(let i=base.length;i<count;i++) {
      const source=base[(i-base.length)%base.length];
      windows.push({...source,id:`stress-${i+1}`,title:`${source.title} · ${i+1}`,workspace:2,stress:true});
    }
    state.workspace=2;state.focused=base[0]?.id;state.mode='scrolling';state.modes[2]='scrolling';state.snapSlots={};
    setView('overview');
  }
  if(e.target.matches('[data-viz-style]')){settings.visualizer=e.target.value;$('#preset').value='custom';applySettings();}
  if(e.target.matches('[data-mode]')){state.mode=e.target.value;state.modes[state.workspace]=state.mode;for(const w of currentWindows())delete state.snapSlots[w.id];render();}
  if(e.target.matches('[data-setting]')){
    const key=e.target.dataset.setting;
    settings[key]=e.target.type==='checkbox'?e.target.checked:e.target.type==='range'?Number(e.target.value):e.target.value;
    $('#preset').value='custom';applySettings();
  }
});

document.addEventListener('input',e=>{
  if(e.target.id==='launcher-search'){state.query=e.target.value;state.resultIndex=0;renderResults();}
  if(e.target.matches('[data-level]')){
    const key=e.target.dataset.level;state[key]=Number(e.target.value);
    e.target.style.setProperty('--value',`${state[key]}%`);$(`#${key}-output`).textContent=`${state[key]}%`;
    if(key==='volume'){
      clearTimeout(osdTimer);const osd=$('#osd'),win=$('.app-window.focused');
      const container=$('#windows');
      if(win){osd.style.left=`${container.offsetLeft+win.offsetLeft}px`;osd.style.top=`${container.offsetTop+win.offsetTop+win.offsetHeight-5}px`;osd.style.bottom='auto';osd.style.width=`${win.offsetWidth*state.volume/100}px`;}
      osd.innerHTML=`<span>Volume ${state.volume}%</span>`;osd.classList.remove('hidden');
      osdTimer=setTimeout(()=>osd.classList.add('hidden'),1000);
    }
  }
  if(e.target.matches('[data-setting][type=range]')){
    settings[e.target.dataset.setting]=Number(e.target.value);$('#preset').value='custom';applySettings();
  }
});

document.addEventListener('keydown',e=>{
  if(e.key==='Escape'){setView('desktop');return;}
  if(e.target.matches('[data-date]')) {
    const deltas={ArrowLeft:-1,ArrowRight:1,ArrowUp:-7,ArrowDown:7};
    if(e.key in deltas){e.preventDefault();const date=new Date(calendar.selected);date.setDate(date.getDate()+deltas[e.key]);selectDate(date);return;}
    if(e.key==='PageUp'||e.key==='PageDown'){e.preventDefault();shiftMonth(e.key==='PageUp'?-1:1,true);return;}
    if(e.key==='Home'||e.key==='End'){e.preventDefault();const date=new Date(calendar.selected),day=(date.getDay()+6)%7;date.setDate(date.getDate()+(e.key==='Home'?-day:6-day));selectDate(date);return;}
  }
  if(e.target.closest('#customizer') || e.target.tagName==='SELECT' || e.target.type==='range')return;
  if(state.view==='launcher'&&['ArrowUp','ArrowDown','Enter'].includes(e.key)){
    e.preventDefault();
    if(e.key==='Enter')runResult(state.resultIndex);
    else {state.resultIndex=Math.max(0,Math.min(lastResults.length-1,state.resultIndex+(e.key==='ArrowDown'?1:-1)));renderResults();}
  } else if(state.view==='overview'&&['ArrowLeft','ArrowRight','Enter'].includes(e.key)){
    if(e.key==='Enter'&&e.target.closest('#overview button'))return;
    e.preventDefault();const list=currentWindows();
    if(e.key==='Enter')setView('desktop');
    else if(list.length){const index=list.findIndex(w=>w.id===state.focused);state.focused=list[(index+(e.key==='ArrowRight'?1:list.length-1))%list.length].id;render();}
  } else if(e.target.matches('[data-select]')&&['Enter',' '].includes(e.key)){
    e.preventDefault();focusWindow(e.target.dataset.select,false);
  }
});

$('#customize-toggle').onclick=()=>customize($('#customizer').classList.contains('hidden'));
$('#close-customizer').onclick=()=>customize(false);
$('#preset').onchange=e=>{if(presets[e.target.value]){settings={...presets[e.target.value]};applySettings();}};
$('#reset').onclick=()=>{settings={...defaults};$('#preset').value='phosphor';applySettings();};
$('#export').onclick=()=>{
  const file=new Blob([JSON.stringify({name:'Phosphor custom study',version:1,settings},null,2)+'\n'],{type:'application/json'});
  const url=URL.createObjectURL(file),link=document.createElement('a');link.href=url;link.download='phosphor-study-preset.json';link.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);notify('Preset exported');
};
new ResizeObserver(()=>{const scale=$('.frame').clientWidth/1440;desktop.style.transform=`scale(${scale})`;$('.frame').style.height=`${900*scale+2}px`;}).observe($('.frame'));
const [study,view]=location.hash.slice(1).split('/');
if(['navigator','stage'].includes(study))state.study=study;
if(['desktop','overview','controls','launcher','datetime'].includes(view))state.view=view;
$('#preset').value=Object.entries(presets).find(([,preset])=>JSON.stringify(preset)===JSON.stringify(settings))?.[0] || 'custom';
applySettings();
