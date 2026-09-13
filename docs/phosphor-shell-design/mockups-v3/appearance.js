// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

window.PhosphorAppearance = (() => {
  const copy = value => JSON.parse(JSON.stringify(value));
  const esc = value => String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const walls = [
    {id:'spectrum',name:'Soft orbit',collection:'Abstract',description:'A quiet horizon, a little light.',colors:['#41d4e8','#6e9cfd','#b68aee','#f390b3'],size:'Scalable artwork'},
    {id:'alpine',name:'Violet horizon',collection:'Abstract',description:'The last light of the evening.',image:'notification-attachment.png',colors:['#919dcc','#aca0d7','#c0a5bd','#d4b8b5'],size:'2880 × 1800'},
    {id:'iris',name:'After hours',collection:'Abstract',description:'Iris and peach, after the sun.',colors:['#9cafdc','#ada0df','#d998bb','#ebbc9c'],size:'Scalable artwork'},
    {id:'dunes',name:'Sundown',collection:'Abstract',description:'Warm curves and long shadows.',colors:['#e8c988','#d4b08a','#d3906c','#d27b83'],size:'Scalable artwork'},
    {id:'tide',name:'Low tide',collection:'Nature',description:'The space between sea and sky.',colors:['#79c7c4','#78a6c4','#9eadd1','#d4c0b1'],size:'Scalable artwork'},
    {id:'moss',name:'Understory',collection:'Nature',description:'A softer kind of green.',colors:['#9bb897','#bcc394','#93b5af','#d2bc9b'],size:'Scalable artwork'},
    {id:'graphite',name:'Fold',collection:'Abstract',description:'Light on a folded surface.',colors:['#a6b8c4','#a9abc0','#bba9bf','#ccb8b1'],size:'Scalable artwork'},
    {id:'linen',name:'First light',collection:'Abstract',description:'A pale canvas for your day.',colors:['#96afa9','#9eaec4','#c2a6b0','#d4bb98'],size:'Scalable artwork'}
  ];
  const widgets = {launcher:['Launcher','grid'],focus:['Focused app','terminal'],media:['Media','music'],workspaces:['Workspaces','grid'],clock:['Date & time','sun'],notifications:['Notifications','bell'],status:['Quick settings','wifi'],appearance:['Appearance','picture'],power:['Power','power']};
  const initial = {display:'main',linked:false,displays:{main:{wall:'spectrum',fit:'fill'},secondary:{wall:'iris',fit:'fill'}},colorSource:'spectrum',accent:1,inset:16,font:'Noto Sans',numberFont:'monospace',interfaceScale:100,desktopStyle:true,surfaceEffect:'none',regions:{left:['launcher','focus','media'],center:['workspaces'],right:['clock','notifications','status','appearance','power']}};
  const styleKeys = ['palette','material','density','radius','gap','glow'];
  const options = {palette:['spectrum','wallpaper','ember'],material:['glass','solid','light'],density:['comfortable','compact'],edge:['top','bottom'],visualizer:['ribbon','bars','halo','off'],lockLayout:['split','centered'],notificationGrouping:['app','time']};

  const paletteRoles = ['c1','c2','c3','c4','bg','card','recess','text','muted','outline','shadow','glow','stage-shade','modal-shade'];
  const rgb = color => [1,3,5].map(at=>parseInt(color.slice(at,at+2),16)/255);
  const hex = channels => '#'+channels.map(v=>Math.round(Math.max(0,Math.min(1,v))*255).toString(16).padStart(2,'0')).join('');
  function hsl(color) {
    const [r,g,b]=rgb(color),max=Math.max(r,g,b),min=Math.min(r,g,b),delta=max-min,l=(max+min)/2;
    if(!delta)return [0,0,l];
    const h=max===r?((g-b)/delta+6)%6:max===g?(b-r)/delta+2:(r-g)/delta+4;
    return [h/6,delta/(1-Math.abs(2*l-1)),l];
  }
  function tone([h,s],l,saturation=s) {
    const a=saturation*Math.min(l,1-l);
    return hex([0,8,4].map(n=>{const k=(n+h*12)%12;return l-a*Math.max(-1,Math.min(k-3,9-k,1));}));
  }
  function luminance(color) {
    const [r,g,b]=rgb(color).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4);
    return .2126*r+.7152*g+.0722*b;
  }
  function readableColor(color,light,backgrounds) {
    const seed=hsl(color),saturation=Math.min(.72,seed[1]),step=light?-.02:.02;
    let level=light?Math.min(.38,seed[2]):Math.max(.72,seed[2]);
    for(let i=0;i<40;i++,level=Math.max(0,Math.min(1,level+step))) {
      const result=tone(seed,level,saturation),a=luminance(result);
      if(backgrounds.every(bg=>{const b=luminance(bg);return (Math.max(a,b)+.05)/(Math.min(a,b)+.05)>=4.5;}))return result;
    }
    return light?'#000000':'#ffffff';
  }
  function wallpaperPalette(colors,settings) {
    // The dominant wallpaper hue colors every semantic role. Keep luminance
    // predictable so changing an image cannot turn glass or Paper unreadable.
    // Achromatic images stay neutral rather than acquiring an arbitrary hue.
    const seed=hsl(colors[0]),saturation=Math.min(.38,seed[1]*1.15),light=settings.material==='light';
    const surface=tone(seed,light?.952:.135,saturation);
    const card=tone(seed,light?.885:.205,saturation*.86);
    const recess=tone(seed,light?.916:.085,saturation*.8);
    const text=readableColor(tone(seed,light?.145:.945,saturation*.6),light,[surface,card,recess]);
    const muted=readableColor(tone(seed,light?.355:.715,saturation*.65),light,[surface,card,recess]);
    const shade=tone(seed,.055,saturation);
    const result={bg:surface+(settings.material==='solid'?'':light?'f5':'f2'),card,recess,text,muted,
      outline:tone(seed,light?.28:.76,saturation*.7)+(light?'30':'2b'),
      shadow:`0 22px 55px ${shade}${light?'40':'80'}`,
      'stage-shade':shade+'a3','modal-shade':shade+'80'};
    colors.forEach((color,i)=>{result[`c${i+1}`]=readableColor(color,light,[surface,card,recess]);});
    result.glow=settings.glow?`0 0 24px ${result.c2}22`:'0 0 0 transparent';
    return result;
  }

  function create({root,desktop,icon,getSettings,setSettings,presets,onClose,notify}) {
    let data=copy(initial),applied,baseline,active=false,peek=false,page='wallpaper',filter='All',query='',dialog=null,pendingExit=null,skipGuard=false,chosenWidget='workspaces',dragged='',message='',saved=[],uploads=[];
    const key='phosphor-appearance-study-v1';
    try {
      const stored=JSON.parse(localStorage.getItem(key)||'null');
      if(stored) { data=validateData(stored.data); saved=(stored.saved||[]).map(validatePreset); }
    } catch { /* Malformed or unavailable storage starts with the defaults. */ }
    applied=copy(data);
    const allWalls=()=>walls.concat(uploads);
    const current=()=>allWalls().find(w=>w.id===data.displays[data.display].wall)||walls[0];
    const snapshot=()=>({data:copy(data),settings:copy(getSettings())});
    const dirty=()=>{if(!baseline)return false;const now=snapshot(),before=copy(baseline);before.data.display=now.data.display;return JSON.stringify(now)!==JSON.stringify(before);};
    const button=(action,label,extra='')=>`<button type="button" data-ap="${action}" ${extra}>${label}</button>`;
    const artwork=(wall,extra='')=>`<div class="ap-art ap-art-${wall.id}" ${extra}>${wall.image?`<img src="${esc(wall.image)}" alt="">`:'<i></i><i></i><i></i>'}</div>`;
    const swatches=(colors=current().colors)=>`<span class="ap-swatches">${colors.map(c=>`<i style="background:${c}"></i>`).join('')}</span>`;
    const select=(id,label,values,value)=>`<label class="ap-field"><span>${label}</span><select data-ap-setting="${id}">${values.map(v=>`<option value="${v[0]}" ${String(value)===String(v[0])?'selected':''}>${v[1]}</option>`).join('')}</select></label>`;
    const toggle=(id,label,description,checked)=>`<div class="ap-toggle-row"><span><b>${label}</b>${description?`<small>${description}</small>`:''}</span>${button(`toggle:${id}`,'',`class="ap-switch" role="switch" aria-label="${label}" aria-checked="${checked}"`)}</div>`;
    const range=(id,label,min,max,value,unit)=>`<label class="ap-field ap-range"><span>${label}<output data-ap-output="${id}">${value}${unit}</output></span><input type="range" data-ap-setting="${id}" aria-label="${label}" min="${min}" max="${max}" value="${value}"></label>`;
    const sectionTitle=(name,description='')=>`<div class="ap-section-title"><h3>${name}</h3>${description?`<p>${description}</p>`:''}</div>`;

    function validateData(value) {
      if(!value||typeof value!=='object')throw Error('Missing appearance settings.');
      const v=copy(initial);
      if(!['main','secondary'].includes(value.display))throw Error('Choose a valid display.');
      v.display=value.display;
      if(typeof value.linked!=='boolean')throw Error('Invalid display preference.');
      v.linked=value.linked;
      for(const id of ['main','secondary']) {
        const d=value.displays?.[id];
        if(!d||!walls.some(w=>w.id===d.wall)||!['fill','fit','stretch','center'].includes(d.fit))throw Error('This preset needs a wallpaper from the bundled collection.');
        v.displays[id]=copy(d);
      }
      if(!['spectrum','wallpaper','ember'].includes(value.colorSource))throw Error('Unknown color source.');
      v.colorSource=value.colorSource;
      for(const [id,min,max] of [['accent',0,3],['inset',6,30],['interfaceScale',90,115]]) {
        if(!Number.isInteger(value[id])||value[id]<min||value[id]>max)throw Error('An appearance value is outside its allowed range.');
        v[id]=value[id];
      }
      if(!['Noto Sans','sans-serif','serif'].includes(value.font)||!['monospace','Noto Sans'].includes(value.numberFont))throw Error('Unsupported preview font.');
      v.font=value.font;v.numberFont=value.numberFont;
      if(typeof value.desktopStyle!=='boolean'||!['none','phosphor-glass','phosphor-motes'].includes(value.surfaceEffect))throw Error('Invalid surface effect.');
      v.desktopStyle=value.desktopStyle;v.surfaceEffect=value.surfaceEffect;
      const entries=[];
      for(const region of ['left','center','right']) {
        const list=value.regions?.[region];
        if(!Array.isArray(list)||list.some(id=>!widgets[id]))throw Error('Unknown bar widget.');
        entries.push(...list);v.regions[region]=list.slice();
      }
      if(new Set(entries).size!==entries.length)throw Error('A widget can appear only once in the bar.');
      return v;
    }

    function validatePreset(value) {
      if(!value||value.kind!=='phosphor-appearance-study'||value.version!==1||typeof value.name!=='string'||!value.name.trim()||value.name.length>60)throw Error('Choose a Phosphor appearance study preset (version 1).');
      const s=value.settings;
      if(!s||typeof s!=='object')throw Error('This preset has no style settings.');
      for(const key of styleKeys) {
        if(options[key]&&!options[key].includes(s[key]))throw Error(`Invalid ${key} setting.`);
        if(key==='glow'&&typeof s[key]!=='boolean')throw Error('Invalid glow setting.');
        if(['radius','gap'].includes(key)&&(!Number.isFinite(s[key])||s[key]<(key==='gap'?6:4)||s[key]>30))throw Error('Invalid spacing setting.');
      }
      if(value.bar&&(!['top','bottom'].includes(value.bar.edge)||typeof value.bar.media!=='boolean'))throw Error('Invalid bar settings.');
      return {kind:value.kind,version:1,name:value.name.trim(),settings:Object.fromEntries(styleKeys.map(key=>[key,s[key]])),appearance:validateData(value.appearance),wallpaper:!!value.wallpaper,bar:value.bar?{edge:value.bar.edge,media:value.bar.media}:null};
    }

    function persist() {
      // Imported image files are session previews, never silently copied into a preset.
      const storable=copy(applied);
      for(const id of ['main','secondary'])if(!walls.some(w=>w.id===storable.displays[id].wall))storable.displays[id]=copy(initial.displays[id]);
      try { localStorage.setItem(key,JSON.stringify({data:storable,saved})); }
      catch { notify('Browser storage is full. Your changes remain in this tab.'); }
    }

    function paint() {
      const wall=current(),s=getSettings(),bg=desktop.querySelector('.wallpaper');
      bg.classList.add('ap-wallpaper');bg.innerHTML=artwork(wall);bg.dataset.fit=data.displays[data.display].fit;
      desktop.dataset.apColorSource=data.colorSource;
      desktop.dataset.apDesktopStyle=data.desktopStyle;desktop.dataset.apEffect=data.surfaceEffect;
      for(const role of paletteRoles)desktop.style.removeProperty(`--${role}`);
      if(data.colorSource==='wallpaper')for(const [role,color] of Object.entries(wallpaperPalette(wall.colors,s)))desktop.style.setProperty(`--${role}`,color);
      desktop.style.setProperty('--ap-accent',`var(--c${data.accent+1})`);
      desktop.style.setProperty('--ap-inset',`${data.inset}px`);
      desktop.style.setProperty('--ap-font',data.font);
      desktop.style.setProperty('--ap-number-font',data.numberFont);
      desktop.style.setProperty('--ap-type-scale',data.interfaceScale/100);
      desktop.style.fontFamily=`'${data.font}', sans-serif`;
      const bar=desktop.querySelector('#bar');
      const selectors={launcher:'.launcher-mark',focus:'.bar-app',media:'.bar-media',workspaces:'.bar-center',clock:'.clock',notifications:'.bar-notifications',status:'.status-cluster',appearance:'.settings-trigger',power:'.bar-power'};
      // The study renderer replaces the bar first. Reparent those real controls so
      // widget edits preserve their existing navigation and media interactions.
      if(!bar.querySelector('.ap-live-region')) {
        const nodes=Object.fromEntries(Object.entries(selectors).map(([id,selector])=>[id,bar.querySelector(selector)]));
        if(!nodes.launcher)nodes.launcher=bar.querySelector('[data-view="launcher"]');
        bar.replaceChildren();
        for(const region of ['left','center','right']) {
          const group=document.createElement('div');group.className=`ap-live-region ap-live-${region}`;
          for(const id of data.regions[region])if(nodes[id])group.append(nodes[id]);
          bar.append(group);
        }
      }
    }

    function scene(wall,mini=false) {
      return `<div data-fit="${mini?'fill':data.displays[data.display].fit}" class="ap-scene ${mini?'ap-scene-mini':''}">${artwork(wall)}<div class="ap-scene-bar"><span>φ</span><i></i><span class="ap-tiny-map"><b></b><b></b><b></b></span><span>10:24</span></div><div class="ap-scene-panes"><div><span></span><i></i><i></i><i></i></div><div><span></span><b></b></div></div>${mini?'':`<div class="ap-scene-caption"><span>DESKTOP PREVIEW</span><b>${esc(wall.name)}</b><small>${wall.description||'Your own view.'}</small></div>${button('peek',`${icon('grid')} View on desktop`,'class="ap-preview-link"')}`}</div>`;
    }

    function wallpaperPage() {
      const wall=current();
      const listed=allWalls().filter(w=>(filter==='All'||filter==='Added'?filter==='All'||w.collection==='Added':filter==='Favorites'?['spectrum','alpine','iris'].includes(w.id):w.collection===filter)&&w.name.toLowerCase().includes(query.toLowerCase()));
      return `<div class="ap-wall-layout"><div class="ap-wall-browser">${scene(wall)}
        <div class="ap-library-toolbar"><nav aria-label="Wallpaper collection">${['All','Abstract','Nature','Added'].map(name=>button(`filter:${name}`,name,`aria-pressed="${filter===name}"`)).join('')}</nav><label class="ap-search">${icon('search')}<input id="ap-search" aria-label="Search wallpapers" placeholder="Search" value="${esc(query)}"></label></div>
        <div class="ap-wall-grid">${listed.map(w=>`<button class="ap-wall-card" data-ap="wall:${w.id}" aria-label="Preview ${esc(w.name)}" aria-pressed="${w.id===wall.id}">${artwork(w)}<span><b>${esc(w.name)}</b>${w.id===wall.id?icon('check'):'<small>'+w.collection+'</small>'}</span></button>`).join('')||`<div class="ap-empty">${icon('picture')}<h3>${query?'No matching wallpapers':'A place for your collection'}</h3><p>${query?'Try a different name or collection.':'Add an image to try it on your desktop.'}</p>${button(query?'clear-search':'add-images',query?'Clear search':'Add images','class="ap-primary"')}</div>`}</div>
      </div><aside class="ap-wall-inspector">
        <div class="ap-display-map" aria-hidden="true"><i class="${data.display==='main'?'selected':''}"><span>1</span></i><i class="${data.display==='secondary'?'selected':''}"><span>2</span></i></div>
        ${sectionTitle('Make it fit','Wallpaper is set per display.')}
        ${select('display','Display',[['main','1 · Main display'],['secondary','2 · Secondary display']],data.display)}
        ${toggle('linked','Use on both displays','Keep the same wallpaper and fit.',data.linked)}
        <div class="ap-rule"></div>
        <div class="ap-wall-meta"><span class="ap-kicker">SELECTED WALLPAPER</span><h3>${esc(wall.name)}</h3><small>${wall.size} · ${wall.collection}</small></div>
        ${select('fit','Image placement',[['fill','Fill screen'],['fit','Fit inside'],['stretch','Stretch'],['center','Center']],data.displays[data.display].fit)}
        <div class="ap-fit-diagram ap-fit-${data.displays[data.display].fit}" aria-hidden="true">${artwork(wall)}</div>
        <div class="ap-rule"></div>
        <div class="ap-palette-heading"><span>Colors from this wallpaper</span>${swatches()}</div>
        ${toggle('wall-colors','Follow wallpaper','Retint the shell when it changes.',data.colorSource==='wallpaper')}
        ${button('page:style',`Fine-tune colors ${icon('arrow-right')}`,'class="ap-text-link"')}
      </aside></div>`;
    }

    function stylePage() {
      const s=getSettings();
      return `<div class="ap-style-grid"><section class="ap-style-demo"><div><span class="ap-kicker">BUILT AROUND YOUR WINDOWS</span><h3>Find your balance.</h3><p>Color, texture and a little breathing room.</p></div><div class="ap-demo-stack"><div class="ap-demo-window"><div>${icon('terminal')} <span>Shell.qml</span><i></i><b>×</b></div><p>Light defines the edges.</p><span>Everything else gets room to breathe.</span><div class="ap-demo-controls"><span>Comfortable</span><span>Focused</span></div></div><div class="ap-demo-osd">${icon('volume')}<i></i><b>64</b></div></div></section>
      <section class="ap-setting-card">${sectionTitle('Color field','Four related colors carry through the shell.')}
        <div class="ap-color-options">${[['spectrum','Phosphor','The original spectrum'],['wallpaper','Wallpaper','Drawn from your background'],['ember','Warm','Honey, clay and rose']].map(([id,name,desc])=>button(`source:${id}`,`<span class="ap-color-strip ap-color-${id}">${id==='wallpaper'?swatches():''}</span><b>${name}</b><small>${desc}</small>`,`class="ap-color-option" aria-pressed="${data.colorSource===id}"`)).join('')}</div>
        <div class="ap-accent-row"><span>Focus accent</span><div>${[0,1,2,3].map(i=>button(`accent:${i}`,data.accent===i?icon('check'):'',`style="--swatch:var(--c${i+1})" aria-label="Use color ${i+1} for focus" aria-pressed="${data.accent===i}"`)).join('')}</div></div>
      </section>
      <section class="ap-setting-card">${sectionTitle('Material','Give each surface its own weight.')}<div class="ap-material-options">${[['glass','Glass','Tinted and translucent'],['solid','Solid','Quiet and opaque'],['light','Paper','Light and softly shaded']].map(([id,name,desc])=>button(`setting:material:${id}`,`<div class="ap-material-art ap-material-${id}"><i></i><i></i></div><b>${name}</b><small>${desc}</small>`,`aria-pressed="${s.material===id}"`)).join('')}</div>${toggle('glow','Edge glow','A soft halo around focused surfaces.',s.glow)}</section>
      <section class="ap-setting-card">${sectionTitle('Shape & spacing')}${select('density','Density',[['comfortable','Comfortable'],['compact','Compact']],s.density)}${range('radius','Corner radius',4,30,s.radius,' px')}${range('gap','Window gaps',6,30,s.gap,' px')}</section>
      <section class="ap-setting-card">${sectionTitle('Type & motion')}${select('font','Interface font',[['Noto Sans','Noto Sans'],['sans-serif','System sans'],['serif','System serif']],data.font)}${select('numberFont','Numbers & time',[['monospace','System monospace'],['Noto Sans','Noto Sans']],data.numberFont)}${range('interfaceScale','Text size',90,115,data.interfaceScale,'%')}${toggle('motion','Animations','Gentle transitions between states.',s.motion)}</section>
      <section class="ap-setting-card ap-wide">${sectionTitle('Other surfaces','Keep the same character throughout your desktop.')}<div class="ap-surface-options"><div>${select('visualizer','Media visualizer',[['ribbon','Ribbon'],['bars','Bars'],['halo','Halo'],['off','Off']],s.visualizer)}${select('notificationGrouping','Notification organization',[['app','Group by application'],['time','Chronological']],s.notificationGrouping)}${select('lockLayout','Lock screen layout',[['split','Clock beside unlock card'],['centered','Centered']],s.lockLayout)}</div><div>${toggle('lockMedia','Media on lock screen','Show track information while locked.',s.lockMedia)}${toggle('notificationPreviews','Notification previews','Show message text and pictures.',s.notificationPreviews)}${toggle('lockNotifications','Lock screen notification count','Keep message content private.',s.lockNotifications)}</div></div></section>
      <section class="ap-setting-card ap-wide">${sectionTitle('Surface effects','Optional details for a more expressive desktop.')}<div class="ap-surface-options"><div>${select('surfaceEffect','Effect pack',[['none','None · clean surfaces'],['phosphor-glass','Phosphor Glass · soft sweep'],['phosphor-motes','Phosphor Motes · drifting light']],data.surfaceEffect)}<p class="ap-hint">Effects follow your palette. Turning them off keeps your colors and material.</p></div><div>${toggle('desktopStyle','Match desktop windows','Use the shell’s frame colors and corners.',data.desktopStyle)}<div class="ap-effect-sample"><i></i><i></i><span>Surface preview</span></div></div></div></section></div>`;
    }

    function barPage() {
      const s=getSettings(),region=Object.keys(data.regions).find(r=>data.regions[r].includes(chosenWidget)),used=Object.values(data.regions).flat();
      return `<div class="ap-bar-layout"><div><div class="ap-bar-preview">${artwork(current())}<span class="ap-kicker">YOUR BAR, IN PLACE</span><div class="ap-bar-mini" data-edge="${s.edge}">${['left','center','right'].map(r=>`<div>${data.regions[r].map(id=>`<span>${icon(widgets[id][1])}${id==='clock'?'<small>10:24</small>':''}</span>`).join('')}</div>`).join('')}</div><p>Drag widgets between regions. Select one for more controls.</p></div>
        <div class="ap-regions">${['left','center','right'].map(r=>`<section class="ap-region" data-ap-drop="${r}"><header><h3>${r[0].toUpperCase()+r.slice(1)}</h3><span>${data.regions[r].length}</span></header><div>${data.regions[r].map(id=>`<button draggable="true" data-ap-drag="${id}" data-ap="widget:${id}" aria-pressed="${chosenWidget===id}" class="ap-widget"><span class="ap-grip">⠿</span>${icon(widgets[id][1])}<span>${widgets[id][0]}</span></button>`).join('')||'<p class="ap-drop-label">Drop a widget here</p>'}</div></section>`).join('')}</div>
        <section class="ap-widget-library">${sectionTitle('Available widgets','Hidden widgets stay here. Add them whenever you like.')}<div>${Object.keys(widgets).filter(id=>!used.includes(id)).map(id=>button(`add-widget:${id}`,`${icon(widgets[id][1])}${widgets[id][0]} <span>＋</span>`)).join('')||'<p>All widgets are in your bar.</p>'}</div></section>
        </div><aside class="ap-wall-inspector">${sectionTitle('Bar placement')}${select('edge','Screen edge',[['top','Top'],['bottom','Bottom']],s.edge)}${range('inset','Screen inset',6,30,data.inset,' px')}<div class="ap-rule"></div>${sectionTitle(widgets[chosenWidget][0],region?'Selected widget':'This widget is hidden.')}
        ${region?`${select('region','Region',[['left','Left'],['center','Center'],['right','Right']],region)}<span class="ap-kicker">ORDER WITHIN REGION</span><div class="ap-order">${button('widget-left','← Move earlier',data.regions[region].indexOf(chosenWidget)===0?'disabled':'')}${button('widget-right','Move later →',data.regions[region].indexOf(chosenWidget)===data.regions[region].length-1?'disabled':'')}</div>${button('hide-widget',`${icon('close-icon')} Hide widget`,'class="ap-text-link"')}`:button(`add-widget:${chosenWidget}`,'Add to bar','class="ap-primary"')}
        ${chosenWidget==='media'?toggle('media','Show media','Display the current track in the bar.',s.media):''}
        <div class="ap-rule"></div><p class="ap-hint">The workspace map stays readable as more windows open. Long labels make room for the controls.</p>${button('reset-bar','Restore default layout','class="ap-text-link"')}</aside></div>`;
    }

    function recipe(name,s,extras={}) {
      const appearance=copy(data);appearance.colorSource=s.palette;
      for(const id of ['main','secondary'])if(!walls.some(w=>w.id===appearance.displays[id].wall))appearance.displays[id]=copy(initial.displays[id]);
      return {kind:'phosphor-appearance-study',version:1,name,settings:Object.fromEntries(styleKeys.map(key=>[key,s[key]])),appearance,wallpaper:false,bar:null,...extras};
    }
    const library=()=>Object.entries(presets).map(([id,s])=>({id,...recipe({phosphor:'Phosphor',paper:'Paper',ember:'Ember'}[id],s),builtIn:true})).concat(saved.map((p,i)=>({id:`saved-${i}`,...p})));

    function presetsPage() {
      return `<div class="ap-preset-intro"><div><span class="ap-kicker">A STARTING POINT, NOT A BOX</span><h3>Keep what makes it yours.</h3><p>Start with a look, change any part of it, then save your own.</p></div>${button('save',`${icon('picture')} Save current look`,'class="ap-primary"')}</div>
      <div class="ap-preset-grid">${library().map(p=>`<article class="ap-preset-card ap-preset-${p.settings.material} ap-preset-${p.settings.palette}">${button(`preset:${p.id}`,`${scene(walls[p.settings.palette==='ember'?3:p.settings.palette==='wallpaper'?2:0],true)}<div class="ap-preset-info"><div><h3>${esc(p.name)}</h3><small>${p.builtIn?'Phosphor collection':'Your collection'}</small></div><span>↗</span></div><p>${p.builtIn?{Phosphor:'Deep glass. A familiar spectrum.',Paper:'Bright surfaces. A little more space.',Ember:'Warm tones. Quiet, compact shapes.'}[p.name]:'Your colors, shapes and material.'}</p>`,'class="ap-preset-open"')}<footer><span>${p.wallpaper?'Style + wallpaper':'Style'}${p.bar?' + bar':''}</span>${button(`export:${p.id}`,`Export ${icon('arrow-right')}`,'class="ap-text-link"')}${!p.builtIn?button(`delete:${p.id}`,'Remove','class="ap-text-link"'):''}</footer></article>`).join('')}</div>
      <div class="ap-import-card"><span class="ap-import-icon">${icon('folder')}</span><div><h3>Bring a look with you.</h3><p>Import a preset file. Review what it changes before trying it.</p></div>${button('import','Import preset','class="ap-secondary"')}</div><div class="ap-preset-note">${icon('check')} Choosing a style keeps your wallpaper, bar layout and privacy choices unless you include them.</div>`;
    }

    function modal() {
      if(!dialog)return '';
      let body='';
      if(dialog.type==='discard')body=`<span class="ap-modal-symbol">${icon('picture')}</span><h3>Keep this look?</h3><p>You have unapplied changes. Apply them to keep this look, or discard them to return to your previous setup.</p><div class="ap-modal-actions">${button('keep-editing','Keep editing','class="ap-secondary"')}${button('discard','Discard changes','class="ap-secondary"')}${button('apply-close','Apply & close','class="ap-primary"')}</div>`;
      if(dialog.type==='save')body=`<span class="ap-kicker">YOUR COLLECTION</span><h3>Save this look.</h3><p>Choose what travels with your preset.</p><label class="ap-field"><span>Name</span><input id="ap-preset-name" maxlength="60" required placeholder="My evening setup" value="${esc(dialog.name||'')}"></label><div class="ap-included">${icon('check')} Colors, material, shape and typography</div><label class="ap-checkbox"><input id="ap-include-wall" type="checkbox" ${dialog.wallpaper?'checked':''}> Include wallpapers</label><label class="ap-checkbox"><input id="ap-include-bar" type="checkbox" ${dialog.bar?'checked':''}> Include bar layout & placement</label><p class="ap-form-error" role="alert">${esc(dialog.error||'')}</p><div class="ap-modal-actions">${button('cancel-dialog','Cancel','class="ap-secondary"')}<button type="submit" class="ap-primary">Save preset</button></div>`;
      if(dialog.type==='preset') {
        const p=dialog.preset;
        body=`<span class="ap-kicker">${dialog.imported?'IMPORTED PRESET':'TRY A LOOK'}</span><h3>${esc(p.name)}</h3>${dialog.imported?`<label class="ap-field"><span>Save in your collection as</span><input id="ap-import-name" maxlength="60" value="${esc(saved.some(item=>item.name===p.name)?p.name.slice(0,53)+' (copy)':p.name)}"></label><p id="ap-import-error" class="ap-form-error" role="alert"></p>`:''}<p>Preview these changes before you apply them.</p><div class="ap-preset-summary"><span>Material<b>${{glass:'Tinted glass',light:'Light ceramic',solid:'Solid'}[p.settings.material]}</b></span><span>Density<b>${p.settings.density}</b></span><span>Corner radius<b>${p.settings.radius} px</b></span></div><div class="ap-included">${icon('check')} Colors, material, shape and typography</div>${p.wallpaper?'<label class="ap-checkbox"><input id="ap-load-wall" type="checkbox"> Include wallpapers</label>':''}${p.bar?'<label class="ap-checkbox"><input id="ap-load-bar" type="checkbox"> Include bar layout & placement</label>':''}<p class="ap-hint">Your privacy and motion preferences stay as they are.</p><div class="ap-modal-actions">${button('cancel-dialog','Cancel','class="ap-secondary"')}${button('try-preset','Preview look','class="ap-primary"')}</div>`;
      }
      if(dialog.type==='error')body=`<h3>Couldn’t import this file.</h3><p>${esc(dialog.error)}</p><div class="ap-modal-actions">${button('cancel-dialog','Back to Appearance','class="ap-primary"')}</div>`;
      if(dialog.type==='delete')body=`<h3>Remove this preset?</h3><p>${esc(dialog.preset.name)} will leave your saved collection. Your applied appearance stays the same.</p><div class="ap-modal-actions">${button('cancel-dialog','Keep preset','class="ap-secondary"')}${button('confirm-delete','Remove preset','class="ap-primary"')}</div>`;
      return `<div class="ap-modal-shade"><form class="ap-modal material" role="dialog" aria-modal="true" aria-label="${dialog.type==='save'?'Save preset':dialog.type==='discard'?'Unapplied changes':'Preset details'}">${body}</form></div>`;
    }

    function draw(focusKey) {
      if(!active)return;
      const scroll=root.querySelector('.ap-content')?.scrollTop||0;
      const focusId=document.activeElement?.id;
      const focusAction=focusKey||document.activeElement?.dataset?.ap;
      const field=document.activeElement?.dataset?.apSetting;
      const selection=focusId==='ap-search'?[document.activeElement.selectionStart,document.activeElement.selectionEnd]:null;
      const headings={wallpaper:['Wallpaper','A different view. The same place to work.'],style:['Style','Give your shell a character of its own.'],bar:['Bar','Your everyday controls, where you want them.'],presets:['Presets','Good starting points. Space for your own.']};
      root.className=peek?'ap-root ap-peeking':'ap-root';
      desktop.classList.toggle('ap-peek',peek);
      desktop.classList.toggle('ap-open',!peek);
      root.innerHTML=peek?`<div class="ap-peek-panel material"><span class="ap-peek-dot"></span><div><b>Previewing ${esc(current().name)}</b><small>${data.linked?'Both displays':data.display==='main'?'Main display':'Secondary display'} · ${dirty()?'Not applied yet':'Current appearance'}</small></div>${button('back',`Back to Appearance`,'class="ap-secondary"')}${button('apply',`Apply look`,'class="ap-primary"')}</div>`:
        `<div class="ap-window material" role="dialog" aria-modal="true" aria-label="Appearance"><aside class="ap-sidebar"><div class="ap-brand"><span>φ</span><div>PHOSPHOR<small>Make it yours.</small></div></div><nav aria-label="Appearance section">${[['wallpaper','Wallpaper','picture'],['style','Style','sun'],['bar','Bar','grid'],['presets','Presets','folder']].map(([id,label,symbol])=>button(`page:${id}`,`${icon(symbol)}${label}`,`aria-current="${page===id?'page':'false'}"`)).join('')}</nav><div class="ap-sidebar-bottom"><div class="ap-signature" aria-hidden="true"><i></i><i></i><i></i></div><p>Same Phosphor.<br>Your expression.</p><span class="ap-kicker">PREVIEW IT. MAKE IT YOURS.</span></div></aside>
        <main class="ap-main"><header class="ap-heading"><div><h2>${headings[page][0]}</h2><p>${headings[page][1]}</p></div><div>${page==='wallpaper'?button('add-images',`${icon('picture')} Add images`,'class="ap-secondary"'):''}${button('close',icon('close-icon'),'class="ap-icon-button" aria-label="Close Appearance"')}</div></header><div class="ap-content">${page==='wallpaper'?wallpaperPage():page==='style'?stylePage():page==='bar'?barPage():presetsPage()}</div></main>
        <footer class="ap-footer"><div class="ap-status" role="status"><i class="${dirty()?'pending':''}"></i><span><b>${esc(message)||(dirty()?'Previewing changes':'Your current look')}</b><small>${dirty()?'Apply when it feels right.':'Changes preview here before you apply them.'}</small></span></div><div>${button('peek',`${icon('grid')} View desktop`,'class="ap-text-link"')}${button('revert','Revert',`class="ap-secondary" ${dirty()?'':'disabled'}`)}${button('apply','Apply changes',`class="ap-primary" ${dirty()?'':'disabled'}`)}</div></footer></div>`;
      root.insertAdjacentHTML('beforeend',`<input class="hidden" id="ap-image-input" type="file" accept="image/png,image/jpeg,image/webp" multiple><input class="hidden" id="ap-import-input" type="file" accept="application/json,.json">${modal()}`);
      const content=root.querySelector('.ap-content');if(content)content.scrollTop=scroll;
      const candidate=focusAction?root.querySelector(`[data-ap="${focusAction}"]`):focusId?root.querySelector(`#${focusId}`):field?root.querySelector(`[data-ap-setting="${field}"]`):null;
      if(candidate){candidate.focus({preventScroll:true});if(selection)candidate.setSelectionRange(...selection);}
      for(const input of root.querySelectorAll('input[type="range"]'))input.style.setProperty('--value',`${100*(input.value-input.min)/(input.max-input.min)}%`);
      if(dialog)root.querySelector('.ap-window')?.setAttribute('inert','');
    }

    function refresh(focusKey) {message='';setSettings(getSettings(),false);if(focusKey)root.querySelector(`[data-ap="${focusKey}"]`)?.focus({preventScroll:true});}
    function showDialog(value) {dialog=value;draw();(root.querySelector('.ap-modal input:not([type="checkbox"])')||root.querySelector('.ap-modal button'))?.focus({preventScroll:true});}
    function closeDialog() {dialog=null;draw();focus();}
    function apply() {applied=copy(data);baseline=snapshot();message=Object.values(data.displays).some(d=>d.wall.startsWith('added-'))?'Applied for this preview session':'Appearance applied';persist();setSettings(getSettings(),true);}
    function revert() {const s=copy(baseline),display=data.display;data=s.data;data.display=display;message='Returned to your applied look';setSettings(s.settings,false);}
    function chooseWall(id) {const d=data.displays[data.display];d.wall=id;if(data.linked)for(const other of Object.values(data.displays))Object.assign(other,d);refresh();}
    function moveWidget(id,region,index) {for(const list of Object.values(data.regions)){const at=list.indexOf(id);if(at!==-1)list.splice(at,1);}data.regions[region].splice(index??data.regions[region].length,0,id);chosenWidget=id;refresh();}
    function exportPreset(p) {const url=URL.createObjectURL(new Blob([JSON.stringify(p,null,2)+'\n'],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download=`phosphor-${p.name.toLowerCase().replace(/[^a-z0-9]+/g,'-')}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);message='Preset exported';draw();}
    function usePreset() {
      const p=copy(dialog.preset),s={...getSettings(),...p.settings};
      if(dialog.imported){const name=root.querySelector('#ap-import-name').value.trim();if(!name||saved.some(item=>item.name.toLowerCase()===name.toLowerCase())){root.querySelector('#ap-import-error').textContent='Choose a unique name for this preset.';root.querySelector('#ap-import-name').focus();return;}p.name=name;}
      for(const k of ['colorSource','accent','font','numberFont','interfaceScale','desktopStyle','surfaceEffect'])data[k]=p.appearance[k];
      if(root.querySelector('#ap-load-wall')?.checked){data.displays=copy(p.appearance.displays);data.linked=p.appearance.linked;}
      if(root.querySelector('#ap-load-bar')?.checked){data.regions=copy(p.appearance.regions);data.inset=p.appearance.inset;s.edge=p.bar.edge;s.media=p.bar.media;}
      if(dialog.imported){saved.push(copy(p));persist();}
      dialog=null;message=`Previewing ${p.name}`;setSettings(s,false);focus();
    }

    root.addEventListener('click',event=>{
      event.stopPropagation();
      const b=event.target.closest('[data-ap]');if(!b)return;
      const [action,...parts]=b.dataset.ap.split(':'),value=parts.join(':');
      if(action==='page'){page=value;query='';message='';const content=root.querySelector('.ap-content');if(content)content.scrollTop=0;draw(`page:${value}`);}
      if(action==='filter'){filter=value;draw(`filter:${value}`);}
      if(action==='clear-search'){query='';filter='All';draw();}
      if(action==='wall')chooseWall(value);
      if(action==='close')onClose();
      if(action==='peek'){peek=true;draw('back');}
      if(action==='back'){peek=false;draw();focus();}
      if(action==='apply'){apply();if(peek){peek=false;draw();focus();}}
      if(action==='revert')revert();
      if(action==='source'){data.colorSource=value;const s={...getSettings(),palette:value};setSettings(s,false);}
      if(action==='accent'){data.accent=Number(value);refresh(b.dataset.ap);}
      if(action==='setting'){const [key,val]=parts;setSettings({...getSettings(),[key]:val},false);}
      if(action==='toggle') {
        if(value==='desktopStyle'){data.desktopStyle=!data.desktopStyle;refresh();}
        else if(value==='linked'){data.linked=!data.linked;if(data.linked)for(const d of Object.values(data.displays))Object.assign(d,data.displays[data.display]);refresh();}
        else if(value==='wall-colors'){data.colorSource=data.colorSource==='wallpaper'?'spectrum':'wallpaper';setSettings({...getSettings(),palette:data.colorSource},false);}
        else setSettings({...getSettings(),[value]:!getSettings()[value]},false);
        root.querySelector(`[data-ap="${b.dataset.ap}"]`)?.focus({preventScroll:true});
      }
      if(action==='widget'){chosenWidget=value;draw(b.dataset.ap);}
      if(action==='add-widget')moveWidget(value,'right');
      if(action==='hide-widget'){for(const list of Object.values(data.regions)){const at=list.indexOf(chosenWidget);if(at!==-1)list.splice(at,1);}refresh();}
      if(action==='widget-left'||action==='widget-right'){const r=Object.keys(data.regions).find(r=>data.regions[r].includes(chosenWidget)),at=data.regions[r].indexOf(chosenWidget);moveWidget(chosenWidget,r,Math.max(0,Math.min(data.regions[r].length-1,at+(action==='widget-left'?-1:1))));}
      if(action==='reset-bar'){data.regions=copy(initial.regions);refresh();}
      if(action==='save')showDialog({type:'save'});
      if(action==='preset')showDialog({type:'preset',preset:library().find(p=>p.id===value)});
      if(action==='try-preset')usePreset();
      if(action==='export')exportPreset(library().find(p=>p.id===value));
      if(action==='delete')showDialog({type:'delete',preset:library().find(p=>p.id===value),index:Number(value.slice(6))});
      if(action==='confirm-delete'){saved.splice(dialog.index,1);persist();closeDialog();}
      if(action==='import')root.querySelector('#ap-import-input').click();
      if(action==='add-images')root.querySelector('#ap-image-input').click();
      if(action==='cancel-dialog'||action==='keep-editing'){pendingExit=null;closeDialog();}
      if(action==='discard'||action==='apply-close'){const next=pendingExit;dialog=null;pendingExit=null;if(action==='discard')revert();else apply();skipGuard=true;if(next)next();else onClose();}
    });

    root.addEventListener('submit',event=>{
      event.preventDefault();event.stopPropagation();if(dialog?.type!=='save')return;
      const name=root.querySelector('#ap-preset-name').value.trim(),wallpaper=root.querySelector('#ap-include-wall').checked,bar=root.querySelector('#ap-include-bar').checked;
      if(!name||saved.some(p=>p.name.toLowerCase()===name.toLowerCase())){showDialog({type:'save',name,wallpaper,bar,error:name?'Choose a different name for this preset.':'Give your preset a name.'});return;}
      if(wallpaper&&Object.values(data.displays).some(d=>!walls.some(w=>w.id===d.wall))){showDialog({type:'save',name,wallpaper,bar,error:'Added images stay in this tab. Save the style without wallpapers, or choose a bundled wallpaper.'});return;}
      saved.push(recipe(name,getSettings(),{wallpaper,bar:bar?{edge:getSettings().edge,media:getSettings().media}:null}));persist();dialog=null;message='Preset saved to your collection';page='presets';draw();focus();
    });

    function imageColors(img) {
      const canvas=document.createElement('canvas');canvas.width=canvas.height=32;
      const ctx=canvas.getContext('2d',{willReadFrequently:true});ctx.drawImage(img,0,0,32,32);
      const pixels=ctx.getImageData(0,0,32,32).data,buckets=new Map();
      for(let i=0;i<pixels.length;i+=4){if(pixels[i+3]<128)continue;const rgb=[pixels[i],pixels[i+1],pixels[i+2]].map(v=>Math.round(v/32)*32);const k=rgb.join(',');buckets.set(k,(buckets.get(k)||0)+1);}
      const colors=[];
      for(const [key] of [...buckets].sort((a,b)=>b[1]-a[1])){const rgb=key.split(',').map(Number);if(colors.every(c=>c.reduce((sum,v,i)=>sum+(v-rgb[i])**2,0)>3000))colors.push(rgb);if(colors.length===4)break;}
      while(colors.length<4)colors.push(colors.length?colors[0].map(v=>Math.min(255,v+20*colors.length)):[120,140,160]);
      return colors.map(rgb=>'#'+rgb.map(v=>Math.max(0,Math.min(255,Math.round(v*.65+80))).toString(16).padStart(2,'0')).join(''));
    }

    function changeSetting(input) {
      const id=input.dataset.apSetting,value=input.type==='range'?Number(input.value):input.value;
      if(id==='display'){data.display=value;refresh();}
      else if(id==='fit'){data.displays[data.display].fit=value;if(data.linked)for(const d of Object.values(data.displays))d.fit=value;refresh();}
      else if(id==='region')moveWidget(chosenWidget,value);
      else if(['font','numberFont','interfaceScale','inset','surfaceEffect'].includes(id)){data[id]=value;refresh();}
      else setSettings({...getSettings(),[id]:value},false);
    }
    root.addEventListener('change',async event=>{
      event.stopPropagation();const input=event.target;
      if(input.dataset.apSetting)changeSetting(input);
      if(input.id==='ap-import-input'&&input.files[0]) {
        try {if(input.files[0].size>1000000)throw Error('This preset is too large. Choose a JSON file smaller than 1 MB.');showDialog({type:'preset',preset:validatePreset(JSON.parse(await input.files[0].text())),imported:true});}
        catch(error){showDialog({type:'error',error:error instanceof SyntaxError?'The file is not valid JSON. Choose an exported appearance preset.':error.message});}
      }
      if(input.id==='ap-image-input') {
        try {
          for(const file of [...input.files].slice(0,20)) {
            if(!['image/png','image/jpeg','image/webp'].includes(file.type)||file.size>10*1024*1024)throw Error('Choose PNG, JPEG or WebP images smaller than 10 MB each.');
            const image=URL.createObjectURL(file),img=new Image();img.src=image;
            try{await img.decode();}catch{URL.revokeObjectURL(image);throw Error('That image could not be opened. Try another file.');}
            uploads.push({id:`added-${uploads.length}`,name:file.name.replace(/\.[^.]+$/,''),collection:'Added',image,colors:imageColors(img),size:`${img.naturalWidth} × ${img.naturalHeight}`,description:'Your image. Your space.'});
          }
          filter='Added';query='';message='Images added for this preview session';draw();
        } catch(error){showDialog({type:'error',error:error.message});}
      }
    });
    root.addEventListener('input',event=>{
      event.stopPropagation();const input=event.target;
      if(input.id==='ap-search'){query=input.value;draw();}
      // Update the live scene without replacing the slider under the pointer.
      if(input.type==='range'&&input.dataset.apSetting){
        const id=input.dataset.apSetting,value=Number(input.value);
        if(['inset','interfaceScale'].includes(id))data[id]=value;
        else setSettings({...getSettings(),[id]:value},false,false);
        paint();
        root.querySelector('[data-ap="apply"]').disabled=!dirty();root.querySelector('[data-ap="revert"]').disabled=!dirty();
        root.querySelector('.ap-status b').textContent=dirty()?'Previewing changes':'Your current look';
        root.querySelector('.ap-status small').textContent=dirty()?'Apply when it feels right.':'Changes preview here before you apply them.';
        root.querySelector('.ap-status>i').classList.toggle('pending',dirty());
        root.querySelector(`[data-ap-output="${input.dataset.apSetting}"]`).textContent=input.value+(input.dataset.apSetting==='interfaceScale'?'%':' px');input.style.setProperty('--value',`${100*(input.value-input.min)/(input.max-input.min)}%`);}
    });
    root.addEventListener('dragstart',event=>{const widget=event.target.closest('[data-ap-drag]');if(!widget)return;dragged=widget.dataset.apDrag;event.dataTransfer.setData('text/plain',dragged);event.dataTransfer.effectAllowed='move';});
    root.addEventListener('dragover',event=>{if(event.target.closest('[data-ap-drop]')&&dragged){event.preventDefault();event.dataTransfer.dropEffect='move';}});
    root.addEventListener('drop',event=>{const region=event.target.closest('[data-ap-drop]');if(region&&widgets[dragged]){event.preventDefault();const before=event.target.closest('[data-ap-drag]');moveWidget(dragged,region.dataset.apDrop,before?data.regions[region.dataset.apDrop].indexOf(before.dataset.apDrag):undefined);}dragged='';});
    root.addEventListener('dragend',()=>{dragged='';});

    function focus(){root.querySelector(peek?'[data-ap="back"]':`[data-ap="page:${page}"]`)?.focus({preventScroll:true});}
    return {
      focus,
      syncSettings(value){data.colorSource=value.palette;},
      render(open) {
        if(open&&!active){active=true;baseline=snapshot();peek=false;message='';}
        else if(!open&&active){active=false;peek=false;dialog=null;baseline=null;desktop.classList.remove('ap-open','ap-peek');root.className='hidden';root.replaceChildren();}
        paint();if(active)draw();
      },
      guardExit(next) {if(skipGuard){skipGuard=false;return false;}if(!dirty())return false;pendingExit=next;showDialog({type:'discard'});return true;},
      handleKey(event) {
        if(!active||!root.contains(event.target)&&event.key!=='Escape')return false;
        if(event.key==='Escape'){event.preventDefault();if(dialog){pendingExit=null;closeDialog();}else if(peek){peek=false;draw();focus();}else onClose();return true;}
        if(event.key==='Tab') {
          const scope=root.querySelector('.ap-modal')||root;
          const targets=[...scope.querySelectorAll('button:not(:disabled),input:not(.hidden),select')].filter(e=>e.offsetWidth&&!e.closest('[inert]'));
          if(event.shiftKey&&document.activeElement===targets[0]){event.preventDefault();targets.at(-1)?.focus();}
          else if(!event.shiftKey&&document.activeElement===targets.at(-1)){event.preventDefault();targets[0]?.focus();}
        }
        return true;
      }
    };
  }
  return {create};
})();
