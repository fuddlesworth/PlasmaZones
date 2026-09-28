// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Design fixtures only. Real collectors will be connected during the native port.
window.PhosphorStats = {
  defaults: {statsStyle:'traces',statsCpu:true,statsGpu:true,statsMemory:true,statsNetwork:false,statsStorage:false,statsMemoryUnit:'percent',statsInterval:2},
  preferences(value={}) {
    const result={...this.defaults};
    for(const key of Object.keys(result))if(Object.hasOwn(value,key))result[key]=value[key];
    if(!['traces','meters','numbers'].includes(result.statsStyle)||!['percent','used'].includes(result.statsMemoryUnit)||![1,2,5].includes(result.statsInterval))throw Error('Invalid system stats preferences.');
    const readings=['statsCpu','statsGpu','statsMemory','statsNetwork','statsStorage'];
    if(readings.some(key=>typeof result[key]!=='boolean')||![1,2,3].includes(readings.filter(key=>result[key]).length))throw Error('Choose one to three system stats readings.');
    return result;
  },
  create({root,review,desktop,icon,getSettings,patchSettings,onDismiss}) {
    const resources = [
      {id:'cpu',name:'CPU',caption:'Processor',key:'statsCpu',color:'c1'},
      {id:'gpu',name:'GPU',caption:'Graphics',key:'statsGpu',color:'c3'},
      {id:'memory',name:'RAM',caption:'Memory',key:'statsMemory',color:'c2'},
      {id:'network',name:'NET',caption:'Network',key:'statsNetwork',color:'c1'},
      {id:'storage',name:'SSD',caption:'Storage',key:'statsStorage',color:'c4'}
    ];
    const scenarios = {
      everyday:{name:'Everyday',cpu:28,gpu:16,memory:18.4,down:12.4,up:.8},
      compiling:{name:'Compiling',cpu:87,gpu:8,memory:31.6,down:2.8,up:.2},
      gaming:{name:'Gaming',cpu:46,gpu:94,memory:22.8,down:.9,up:.3},
      unavailable:{name:'Missing sensors',cpu:28,gpu:null,memory:18.4,down:12.4,up:.8},
      offline:{name:'Network offline',cpu:12,gpu:8,memory:15.2,down:null,up:null},
      full:{name:'Disk nearly full',cpu:28,gpu:16,memory:18.4,down:12.4,up:.8}
    };
    let active=false, page='overview', scenario='everyday', paused=false, range=1, seconds=0, timer;
    const fixture=()=>scenarios[scenario];
    const meta=id=>resources.find(r=>r.id===id);
    const clamp=(n,min,max)=>Math.min(max,Math.max(min,n));
    const selected=()=>resources.filter(r=>getSettings()[r.key]).slice(0,3);
    const color=id=>`--stat-color:var(--${meta(id).color})`;
    const button=(action,label,body,extra='')=>`<button data-stat-action="${action}" data-stat-focus="${action}" aria-label="${label}" ${extra}>${body}</button>`;
    const smallButton=(action,label,glyph)=>button(action,label,icon(glyph),'class="stat-icon-button"');
    function value(id,t=seconds) {
      const base=fixture()[id];
      if(id==='storage')return scenario==='full'?96:64;
      if(base===null)return null;
      const wave=Math.sin(t*.11)+.55*Math.sin(t*.39)+.25*Math.sin(t*.83);
      if(id==='memory')return clamp(base+wave*.12,0,64);
      if(id==='down'||id==='up')return Math.max(0,base*(1+wave*.16));
      return clamp(base+wave*(id==='cpu'?5:2.5),0,100);
    }
    function temperature(id) {
      if(scenario==='unavailable')return 'Not reported';
      return `${Math.round((id==='cpu'?43:38)+value(id)*.38)}°C`;
    }
    const percent=id=>value(id)===null?'—':`${Math.round(value(id))}%`;
    const rate=id=>value(id)===null?'—':value(id).toFixed(1);
    function memoryParts() {
      const used=value('memory'), cached=7.2;
      return {used,cached,free:64-used-cached,available:64-used};
    }
    function chart(id,{width=340,height=112,minutes=range,mini=false}={}) {
      const current=value(id), unknown=current===null;
      if(unknown)return `<div class="stat-chart-unavailable ${mini?'mini':''}">${mini?'—':'No readings available'}</div>`;
      const maximum=id==='memory'?64:id==='down'?25:id==='up'?2:100;
      const points=Array.from({length:41},(_,i)=>{
        const v=value(id,seconds-(40-i)*minutes*60/40);
        return `${(i*width/40).toFixed(1)},${(height-3-clamp(v/maximum,0,1)*(height-6)).toFixed(1)}`;
      });
      return `<svg class="stat-chart" viewBox="0 0 ${width} ${height}" preserveAspectRatio="none" aria-hidden="true">
        ${mini?'':`<path class="stat-grid" d="M0 3H${width}M0 ${height/2}H${width}M0 ${height-3}H${width}"/>`}
        <path class="stat-chart-fill" d="M0 ${height}L${points.join(' ')}L${width} ${height}Z"/>
        <polyline class="stat-chart-line" points="${points.join(' ')}" vector-effect="non-scaling-stroke"/>
      </svg>`;
    }
    function barReading(id) {
      if(id==='memory')return getSettings().statsMemoryUnit==='used'?`${value(id).toFixed(1)}G`:`${Math.round(value(id)/64*100)}%`;
      if(id==='network')return value('down')===null?'Off':`${rate('down')}M`;
      return percent(id);
    }
    function barContents() {
      const style=getSettings().statsStyle;
      return selected().map(r=>{
        const v=r.id==='network'?value('down'):value(r.id), pct=r.id==='memory'?v/64*100:r.id==='network'?v/25*100:v;
        const graphic=style==='traces'?chart(r.id==='network'?'down':r.id,{width:40,height:9,minutes:1,mini:true}):
          style==='meters'?`<span class="stat-bar-meter" aria-hidden="true">${Array.from({length:10},(_,i)=>`<i class="${v!==null&&i<pct/10?'lit':''}"></i>`).join('')}</span>`:'';
        return `<span class="stat-bar-reading" style="${color(r.id)}"><span class="stat-bar-label">${r.name}</span><span class="stat-bar-value">${barReading(r.id)}</span>${graphic}</span>`;
      }).join('');
    }
    function barMarkup() {
      return `<button class="bar-stats" data-view="stats" data-style="${getSettings().statsStyle}" aria-label="Open system stats" title="System stats" aria-expanded="${active}" aria-controls="stats">${barContents()}</button>`;
    }
    function updateBar() {
      const bar=desktop.querySelector('.bar-stats');if(!bar)return;
      bar.dataset.style=getSettings().statsStyle;bar.innerHTML=barContents();
      bar.setAttribute('aria-expanded',String(active));
      const description=selected().map(r=>`${r.id==='storage'?'System disk':r.caption}: ${r.id==='network'?(value('down')===null?'offline':`${rate('down')} megabytes per second down`):r.id==='memory'?`${value('memory').toFixed(1)} of 64 gigabytes`:value(r.id)===null?'not reported':percent(r.id)}`).join(', ');
      bar.setAttribute('aria-label',`Open system stats. ${description}${paused?'. Readings paused':''}`);
    }
    function memoryMeter() {
      const m=memoryParts();
      return `<span class="stat-memory-meter" aria-hidden="true"><i style="width:${m.used/64*100}%"></i><i style="width:${m.cached/64*100}%"></i><i style="flex:1"></i></span>`;
    }
    function driveRows(detailed=false) {
      return [{name:'System',mount:'/',used:scenario==='full'?960:642,total:1000},{name:'Home',mount:'/home',used:1160,total:2000}].map(d=>{
        const pct=Math.round(d.used/d.total*100),warn=pct>=90;
        return `<div class="stat-drive ${warn?'stat-drive-warning':''}"><div><span>${d.name}${detailed?` <small>${d.mount} · NVMe SSD</small>`:''}</span><span>${d.total-d.used} GB free${warn?' · Low space':''}</span></div><span class="stat-drive-meter" aria-hidden="true"><i style="width:${pct}%"></i></span><div class="stat-drive-detail">${d.used.toLocaleString('en-US')} / ${d.total.toLocaleString('en-US')} GB <span>${pct}% used</span></div></div>`;
      }).join('');
    }
    function overview() {
      return `<div class="stat-pair">${['cpu','gpu'].map(id=>button(`open-${id}`,`View ${meta(id).caption.toLowerCase()} details`,
        `<span class="stat-resource-label">${icon(id)}${meta(id).name}${icon('chevron')}</span><span class="stat-big">${value(id)===null?'—':Math.round(value(id))}<small>${value(id)===null?'':'%'}</small></span><span class="stat-small-chart">${chart(id,{width:170,height:48,minutes:1})}</span><span class="stat-card-meta">${value(id)===null?'Not reported':id==='cpu'?'16 cores · 32 threads':scenario==='gaming'?'11.2 / 16 GB VRAM':'3.1 / 16 GB VRAM'}<span>${temperature(id)==='Not reported'?'— °C':temperature(id)}</span></span>`,'class="stat-resource-card" style="'+color(id)+'"')).join('')}</div>
        ${button('open-memory','View memory details',`<span class="stat-resource-label">${icon('memory')}Memory${icon('chevron')}</span><span class="stat-reading-row"><strong>${value('memory').toFixed(1)} <small>/ 64 GB</small></strong><span>${Math.round(value('memory')/64*100)}% used</span></span>${memoryMeter()}<span class="stat-legend"><span>In use</span><span>Cached</span><span>${memoryParts().available.toFixed(1)} GB available</span></span>`,'class="stat-wide-card" style="'+color('memory')+'"')}
        ${button('open-network','View network details',`<span class="stat-resource-label">${icon('network')}Network<span class="stat-connection ${scenario==='offline'?'offline':''}">${scenario==='offline'?'Disconnected':'Ethernet'}</span>${icon('chevron')}</span><span class="stat-traffic"><span><small>↓ Download</small><strong>${rate('down')} <small>${scenario==='offline'?'':'MB/s'}</small></strong></span><span><small>↑ Upload</small><strong>${rate('up')} <small>${scenario==='offline'?'':'MB/s'}</small></strong></span><span class="stat-network-spark">${chart('down',{width:100,height:32,minutes:1,mini:true})}</span></span>`,'class="stat-wide-card" style="'+color('network')+'"')}
        ${button('open-storage','View storage details',`<span class="stat-resource-label">${icon('storage')}Storage${icon('chevron')}</span>${driveRows()}`,'class="stat-wide-card stat-storage-card" style="'+color('storage')+'"')}`;
    }
    function facts(items) {
      return `<dl class="stat-facts">${items.map(([label,reading])=>`<div><dt>${label}</dt><dd>${reading}</dd></div>`).join('')}</dl>`;
    }
    function historyChart(id) {
      const top=id==='memory'?'64 GB':id==='down'?'25 MB/s':'100%';
      return `<div class="stat-history" style="${color(id==='down'?'network':id)}"><div class="stat-section-label"><span>${id==='down'?'Download history':'Usage history'}</span><nav aria-label="History range">${[1,5,15].map(n=>button(`range-${n}`,`${n} minute history`,`${n}m`,`aria-pressed="${range===n}"`)).join('')}</nav></div><div class="stat-history-plot"><span class="stat-chart-ceiling">${top}</span>${chart(id,{width:360,height:120})}<span class="stat-chart-zero">0</span></div><div class="stat-axis"><span>${range} ${range===1?'minute':'minutes'} ago</span><span>Now</span></div></div>`;
    }
    function detailHero(id,description,reading,unit) {
      return `<div class="stat-detail-hero" style="${color(id)}"><div class="stat-resource-label">${icon(id)}${description}</div><div class="stat-detail-value">${reading}<small>${unit}</small></div></div>`;
    }
    function details() {
      if(page==='cpu') {
        const load=Math.round(value('cpu'));
        return detailHero('cpu','16-core desktop processor',load,'% in use')+historyChart('cpu')+
          facts([['Clock',scenario==='compiling'?'4.9 GHz':'4.2 GHz'],['Temperature',temperature('cpu')],['Package power',scenario==='unavailable'?'Not reported':`${Math.round(35+load*.95)} W`]])+
          `<div class="stat-section-label"><span>Logical processors</span><span>16 cores · 32 threads</span></div><div class="stat-threads">${Array.from({length:32},(_,i)=>{const v=Math.round(clamp(load+Math.sin(i*2.1+seconds*.05)*load*.65,1,100));return `<span style="--thread-load:${v}%" title="Thread ${i+1}: ${v}%"><small>${String(i+1).padStart(2,'0')}</small><b>${v}<small>%</small></b></span>`;}).join('')}</div>`;
      }
      if(page==='gpu') {
        if(value('gpu')===null)return `<div class="stat-unavailable">${icon('gpu')}<h3>GPU readings unavailable</h3><p>This device is not reporting graphics usage. Other system readings are still available.</p><span class="stat-unavailable-dashes">— &nbsp; — &nbsp; —</span></div>${facts([['Graphics usage','Not reported'],['Video memory','Not reported'],['Temperature','Not reported']])}`;
        const vram=scenario==='gaming'?11.2:3.1;
        return detailHero('gpu','Discrete graphics · 16 GB',Math.round(value('gpu')),'% in use')+historyChart('gpu')+
          facts([['Core clock',scenario==='gaming'?'2.4 GHz':'1.2 GHz'],['Temperature',temperature('gpu')],['Board power',`${Math.round(32+value('gpu')*2.3)} W`]])+
          `<div class="stat-section-label"><span>Video memory</span><span>${vram} / 16 GB</span></div><div class="stat-drive-meter" style="${color('gpu')}"><i style="width:${vram/16*100}%"></i></div><p class="stat-explainer">Dedicated memory used by applications and the desktop.</p>`+
          `<div class="stat-engine-list">${[['Render',Math.round(value('gpu'))],['Compute',scenario==='gaming'?18:0],['Video decode',scenario==='gaming'?0:12]].map(([label,v])=>`<div><span>${label}</span><span class="stat-drive-meter" style="${color('gpu')}"><i style="width:${v}%"></i></span><span>${v}%</span></div>`).join('')}</div>`;
      }
      if(page==='memory') {
        const m=memoryParts();
        return detailHero('memory','64 GB DDR5 · Physical memory',m.used.toFixed(1),'GB in use')+historyChart('memory')+
          `<div class="stat-memory-breakdown">${memoryMeter()}${facts([['In use',`${m.used.toFixed(1)} GB`],['Cached',`${m.cached.toFixed(1)} GB`],['Free',`${m.free.toFixed(1)} GB`]])}</div>`+
          `<div class="stat-availability"><span>Available to applications</span><strong>${m.available.toFixed(1)} <small>GB</small></strong></div><p class="stat-explainer">Available memory includes free memory and reclaimable cache. Cached memory can be used by applications when needed.</p>`+
          facts([['Swap in use','0.2 / 8 GB'],['Memory speed','5600 MT/s']]);
      }
      if(page==='network') {
        if(scenario==='offline')return `<div class="stat-unavailable">${icon('network')}<h3>No active connection</h3><p>Traffic readings will resume when a network connection is available.</p>${button('quick-settings','Open quick settings',`Network settings ${icon('arrow-right')}`,'class="stat-text-button"')}</div>${facts([['Interface','Ethernet · enp6s0'],['Connection','Disconnected']])}`;
        return detailHero('network','Ethernet · enp6s0',rate('down'),'MB/s down')+
          `<div class="stat-upload">↑ <strong>${rate('up')}</strong> MB/s upload</div>`+historyChart('down')+
          facts([['Link speed','2.5 Gbps'],['Local address','192.168.1.24']])+
          `<div class="stat-section-label"><span>This session</span><span>Since connection</span></div><div class="stat-session-traffic"><div><span>↓ Downloaded</span><strong>8.42 <small>GB</small></strong></div><div><span>↑ Uploaded</span><strong>624 <small>MB</small></strong></div></div><p class="stat-explainer">Traffic is shown in megabytes per second. Link speed is the connection capacity, in gigabits per second.</p>${button('quick-settings','Open quick settings',`Network settings ${icon('arrow-right')}`,'class="stat-text-button"')}`;
      }
      return `<div class="stat-detail-hero" style="${color('storage')}"><div class="stat-resource-label">${icon('storage')}Two local drives</div><h3>Room for what’s next.</h3><p>${scenario==='full'?'Your system drive is getting full.':'Space and activity, at a glance.'}</p></div><div class="stat-storage-details" style="${color('storage')}">${driveRows(true)}</div>${scenario==='full'?'<div class="stat-space-notice"><b>System drive · 4% free</b><span>40 GB remaining. Move or remove unneeded files to make more room.</span></div>':''}<div class="stat-section-label"><span>Disk activity</span><span>All local drives</span></div>${facts([['Read',scenario==='compiling'?'186 MB/s':'24.6 MB/s'],['Write',scenario==='compiling'?'82.4 MB/s':'3.2 MB/s']])}<p class="stat-explainer">Capacity is shown per filesystem. Activity is the combined read and write rate of these drives.</p>`;
    }
    function customization() {
      const s=getSettings(),count=selected().length;
      return `<div class="stat-custom-intro"><h3>Your desktop, your pulse.</h3><p>Keep the readings you care about within reach.</p></div><div class="stat-widget-preview"><span class="stat-kicker">BAR PREVIEW</span><div class="stat-preview-readings" data-style="${s.statsStyle}">${barContents()}</div></div>
        <fieldset class="stat-fieldset"><legend>Readout style</legend><div class="stat-style-options">${['traces','meters','numbers'].map(style=>button(`style-${style}`,`${style} style`,`<span class="stat-style-art ${style}" aria-hidden="true">${style==='traces'?chart('cpu',{width:65,height:20,mini:true}):style==='meters'?Array.from({length:9},(_,i)=>`<i style="opacity:${i<6?1:.18}"></i>`).join(''):'28<span>%</span>'}</span>${style[0].toUpperCase()+style.slice(1)}`,`aria-pressed="${s.statsStyle===style}"`)).join('')}</div></fieldset>
        <fieldset class="stat-fieldset"><legend>Readings in the bar <small>${count} / 3</small></legend><p>Choose up to three to keep the bar compact.</p><div class="stat-metric-options">${resources.map(r=>`<label style="${color(r.id)}">${icon(r.id)}<span>${r.id==='storage'?'System disk':r.caption}</span><input data-stat-metric="${r.key}" data-stat-focus="${r.key}" type="checkbox" ${s[r.key]?'checked':''} ${count===3&&!s[r.key]||count===1&&s[r.key]?'disabled':''}></label>`).join('')}</div></fieldset>
        <label class="stat-option-row">Memory readout<select data-stat-pref="statsMemoryUnit" data-stat-focus="statsMemoryUnit"><option value="percent" ${s.statsMemoryUnit==='percent'?'selected':''}>Percent used</option><option value="used" ${s.statsMemoryUnit==='used'?'selected':''}>GB used</option></select></label>
        <label class="stat-option-row">Refresh every<select data-stat-pref="statsInterval" data-stat-focus="statsInterval">${[1,2,5].map(v=>`<option value="${v}" ${s.statsInterval===v?'selected':''}>${v} ${v===1?'second':'seconds'}</option>`).join('')}</select></label>
        <p class="stat-explainer">Colors follow your shell palette. These choices are saved with your preset.</p>${button('reset','Reset system stats widget',`Reset widget ${icon('refresh')}`,'class="stat-text-button"')}`;
    }
    function position() {
      if(!active)return;
      const trigger=desktop.querySelector('.bar-stats');
      if(!trigger){root.style.left=`${desktop.offsetWidth-root.offsetWidth-18}px`;return;}
      const rect=trigger.getBoundingClientRect(),d=desktop.getBoundingClientRect(),scale=d.width/desktop.offsetWidth;
      const right=(rect.right-d.left)/scale;
      root.style.left=`${clamp(right-root.offsetWidth,18,desktop.offsetWidth-root.offsetWidth-18)}px`;
    }
    function renderPanel() {
      if(!active)return;
      const focused=root.contains(document.activeElement)?document.activeElement.dataset.statFocus:null;
      const scroll=root.querySelector('.stat-body')?.scrollTop||0;
      const title=page==='overview'?'System':page==='customize'?'Bar widget':meta(page)?.caption;
      root.innerHTML=`<header class="stat-heading">${page==='overview'?'<span class="stat-heading-mark">'+icon('cpu')+'</span>':smallButton('back','Back to system overview','arrow-left')}<div><span class="stat-kicker">${page==='overview'?'DESKTOP ACTIVITY':page==='customize'?'MAKE IT YOURS':'SYSTEM / '+(page==='cpu'?'PROCESSOR':page.toUpperCase())}</span><h2>${title}</h2></div>${page!=='customize'?smallButton('customize','Customize stats widget','tune'):''}${smallButton('close','Close system stats','close-icon')}</header>
        <div class="stat-body ${page==='overview'?'stat-overview':''}" tabindex="-1">${page==='overview'?overview():page==='customize'?customization():details()}</div>
        <footer class="stat-footer"><span>Uptime <strong>6h 24m</strong></span>${button('pause',paused?'Resume live readings':'Pause live readings',`<i class="${paused?'paused':''}"></i>${paused?'Paused':`Live · ${getSettings().statsInterval}s`}${icon(paused?'arrow-right':'pause')}`,`class="stat-live" aria-pressed="${paused}"`)}</footer>`;
      root.querySelector('.stat-body').scrollTop=scroll;
      if(focused)root.querySelector(`[data-stat-focus="${focused}"]`)?.focus({preventScroll:true});
      position();
    }
    function renderReview() {
      review.innerHTML=`<span class="stat-kicker">SAMPLE DESKTOP</span><nav aria-label="System activity scenario">${Object.entries(scenarios).map(([id,s])=>`<button data-stat-scenario="${id}" aria-pressed="${id===scenario}">${s.name}</button>`).join('')}</nav><p>Simulated readings · no system changes</p>`;
    }
    function render(show) {
      if(show&&!active)page='overview';
      active=show;root.className=show?'stat-panel material':'hidden';review.classList.toggle('hidden',!show);
      if(show){renderPanel();renderReview();}else root.replaceChildren();
      updateBar();schedule();
    }
    function open(next) {
      if(!['overview','customize',...resources.map(r=>r.id)].includes(next))return;
      page=next;root.querySelector('.stat-body')?.scrollTo(0,0);renderPanel();
      root.querySelector(`[data-stat-focus="${next==='overview'?'open-cpu':'back'}"]`)?.focus({preventScroll:true});
    }
    function save(patch,focusKey) {
      patchSettings(patch);updateBar();renderPanel();schedule();
      if(focusKey)root.querySelector(`[data-stat-focus="${focusKey}"]`)?.focus({preventScroll:true});
    }
    // Keep buttons and focused elements mounted while sample readings change.
    // A timer must never replace a card between pointer-down and pointer-up.
    function patchReadings(target,source) {
      if(target.nodeType!==source.nodeType||target.nodeName!==source.nodeName){target.replaceWith(source.cloneNode(true));return;}
      if(target.nodeType===Node.TEXT_NODE){if(target.textContent!==source.textContent)target.textContent=source.textContent;return;}
      for(const attr of [...target.attributes])if(!source.hasAttribute(attr.name))target.removeAttribute(attr.name);
      for(const attr of source.attributes)if(target.getAttribute(attr.name)!==attr.value)target.setAttribute(attr.name,attr.value);
      const oldChildren=[...target.childNodes],newChildren=[...source.childNodes];
      for(let i=0;i<Math.max(oldChildren.length,newChildren.length);i++){
        if(!newChildren[i])oldChildren[i].remove();
        else if(!oldChildren[i])target.append(newChildren[i].cloneNode(true));
        else patchReadings(oldChildren[i],newChildren[i]);
      }
    }
    function refreshReadings() {
      const body=root.querySelector('.stat-body');if(!body)return;
      const next=body.cloneNode(false);next.innerHTML=page==='overview'?overview():details();
      patchReadings(body,next);
    }
    function schedule() {
      clearTimeout(timer);
      if(paused||document.hidden)return;
      timer=setTimeout(()=>{
        seconds+=Number(getSettings().statsInterval);updateBar();
        if(active&&page!=='customize')refreshReadings();
        else if(active){const preview=root.querySelector('.stat-preview-readings');if(preview)preview.innerHTML=barContents();}
        schedule();
      },getSettings().statsInterval*1000);
    }
    root.addEventListener('click',e=>{
      const action=e.target.closest('[data-stat-action]')?.dataset.statAction;if(!action)return;
      if(action.startsWith('open-'))open(action.slice(5));
      else if(action.startsWith('range-')){range=Number(action.slice(6));renderPanel();}
      else if(action.startsWith('style-'))save({statsStyle:action.slice(6)},action);
      else if(action==='customize')open('customize');
      else if(action==='back'){const previous=page;open('overview');root.querySelector(`[data-stat-focus="${previous==='customize'?'customize':'open-'+previous}"]`)?.focus({preventScroll:true});}
      else if(action==='close')onDismiss();
      else if(action==='pause'){paused=!paused;updateBar();renderPanel();schedule();}
      else if(action==='reset')save({...PhosphorStats.defaults},'reset');
      else if(action==='quick-settings')desktop.querySelector('.status-cluster')?.click();
    });
    root.addEventListener('change',e=>{
      const input=e.target;
      if(input.dataset.statMetric){
        const count=selected().length;
        if(input.checked?count<3:count>1)save({[input.dataset.statMetric]:input.checked},input.dataset.statFocus);
      }
      if(input.dataset.statPref)save({[input.dataset.statPref]:input.dataset.statPref==='statsInterval'?Number(input.value):input.value},input.dataset.statFocus);
    });
    review.addEventListener('click',e=>{
      const id=e.target.closest('[data-stat-scenario]')?.dataset.statScenario;if(!scenarios[id])return;
      scenario=id;seconds=0;renderPanel();updateBar();renderReview();
      review.querySelector(`[data-stat-scenario="${id}"]`)?.focus({preventScroll:true});
    });
    document.addEventListener('visibilitychange',schedule);
    window.addEventListener('resize',position);
    return {render,barMarkup,open,focus:()=>root.querySelector('[data-stat-focus="open-cpu"]')?.focus({preventScroll:true}),
      handleKey(e){
        if(!active||e.key!=='Escape'||e.target.closest('#customizer'))return false;
        e.preventDefault();if(page==='overview')onDismiss();else root.querySelector('[data-stat-action="back"]')?.click();return true;
      }
    };
  }
};
