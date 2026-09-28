// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Design fixtures only. No device APIs, network requests or saved credentials.
window.PhosphorQuickSettings = {create({root,review,icon,shared,onRedraw,onSummary,getSettings,patchSettings,getWallpaper,onAppearance}) {
  const esc = value => String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const networks = [
    {name:'Home network',meta:'5 GHz · Strong signal',saved:true},
    {name:'Studio 5G',meta:'5 GHz · Strong signal'},
    {name:'Guest',meta:'2.4 GHz · Open network',open:true},
    {name:'Workshop',meta:'2.4 GHz · Fair signal',saved:true}
  ];
  const devices = [
    {name:'Headphones',type:'headphones',meta:'Stereo audio',battery:78,paired:true,connected:true},
    {name:'MX Master',type:'mouse',meta:'Mouse',battery:62,paired:true,connected:true},
    {name:'Orbit Keyboard',type:'keyboard',meta:'Keyboard',paired:false,connected:false},
    {name:'Pocket Speaker',type:'speaker',meta:'Portable audio',paired:false,connected:false}
  ];
  const outputs = [['Headphones','Bluetooth · Stereo','headphones'],['Speakers','Built-in audio','speaker'],['USB DAC','USB audio · Line out','volume']];
  const inputs = [['USB microphone','USB audio · Mono'],['Internal microphone','Built-in audio · Stereo']];
  const scenarios = {
    wifi:[['ready','Connected'],['password','Password'],['connecting','Connecting'],['error','Wrong password'],['portal','Sign-in required'],['off','Wi-Fi off'],['empty','No networks'],['unavailable','Adapter unavailable']],
    bluetooth:[['ready','Connected devices'],['pair','Confirm pairing'],['pin','Enter PIN'],['connecting','Connecting'],['error','Pairing failed'],['off','Bluetooth off'],['empty','No nearby devices'],['unavailable','Adapter unavailable']],
    audio:[['ready','Output'],['input','Microphone'],['apps','App volumes'],['silent','No active apps'],['unplugged','Device disconnected'],['empty','No audio devices'],['unavailable','Audio unavailable']],
    nightlight:[['ready','Evening'],['paused','Paused'],['location','Location unavailable'],['unavailable','Service unavailable']],
    darkmode:[['ready','Manual appearance'],['system-light','System light'],['system-dark','System dark'],['unavailable','System preference unavailable']],
    airplane:[['ready','Radios available'],['on','Airplane mode'],['exception','Bluetooth enabled'],['blocked','Hardware switch'],['unavailable','Service unavailable']],
    power:[['ready','All profiles'],['limited','Performance limited'],['unsupported','Two profiles'],['unavailable','Service unavailable']],
    battery:[['ready','On battery'],['charging','Charging'],['low','Low battery'],['full','Fully charged'],['unknown','Estimate unavailable'],['desktop','No internal battery'],['unavailable','Service unavailable']],
    wallpaper:[['ready','Current wallpaper']]
  };
  const titles={wifi:'Wi-Fi',bluetooth:'Bluetooth',audio:'Audio',nightlight:'Night light',darkmode:'Dark mode',airplane:'Airplane mode',power:'Power profiles',battery:'Battery',wallpaper:'Wallpaper'};
  const fixtures=Object.fromEntries(Object.keys(titles).slice(3).map(key=>[key,'ready']));
  let warmth=65,schedule='sunset',paused=false,startTime='21:00',endTime='07:00',scheduleError='';
  let airplane=false,previousRadios=null,profile='balanced',themeMode='manual',systemDark=true;
  let darkMaterial=getSettings().material==='light'?'glass':getSettings().material,lastThemeMaterial=getSettings().material;
  const profiles=[['saver','Power saver','Use less energy and reduce background work.','battery'],['balanced','Balanced','Balance responsiveness and energy use.','tune'],['performance','Performance','Favor speed for demanding work.','cpu']];
  let active=false,section=null,scenario='ready',notice='',timer=null,serial=0;
  let wifiName='Home network',wifiStep='',wifiTarget='',password='',passwordVisible=false,autoConnect=true,wifiInfo=false,scanning=false;
  let btTarget='',btStep='',pin='',btEmpty=false;
  let audioTab='output',inputName='USB microphone',gain=72,inputMuted=false,testing=false;
  shared.volumeMuted=false;
  const appLevels={Music:84,Firefox:45},appRoutes={Music:'Default output',Firefox:'Default output'},appMuted={Music:false,Firefox:false};
  const button = (action,label,extra='',classes='qs-button') => `<button type="button" class="${classes}" data-qs="${action}" ${extra}>${label}</button>`;
  const smallIcon = (action,symbol,label,extra='') => button(action,icon(symbol),`aria-label="${label}" ${extra}`,'qs-icon-button');
  function stop() { clearTimeout(timer);timer=null;serial++;scanning=false; }
  function later(callback) {
    const token=++serial;
    clearTimeout(timer);
    timer=setTimeout(()=>{if(token===serial){timer=null;callback();redraw();}},1100);
  }
  function focus(key) {
    const target=root.querySelector(`[data-qs-key="${key}"]`);if(!target)return;
    target.focus({preventScroll:true});
    const body=target.closest('.qs-body');if(!body)return;
    const bounds=target.getBoundingClientRect(),clip=body.getBoundingClientRect(),scale=clip.height/body.offsetHeight;
    if(bounds.top<clip.top)body.scrollTop+=(bounds.top-clip.top-8)/scale;
    else if(bounds.bottom>clip.bottom)body.scrollTop+=(bounds.bottom-clip.bottom+8)/scale;
  }
  function redraw(key) {
    const previous=root.contains(document.activeElement)?document.activeElement.dataset.qsKey:null;
    const previewFocus=document.activeElement.hasAttribute('data-qs-scenario');
    const scroll=root.querySelector('.qs-body')?.scrollTop||0;
    onRedraw();onSummary();
    const body=root.querySelector('.qs-body');if(body)body.scrollTop=scroll;
    if(key||previous)focus(key||previous);
    if(previewFocus)review.querySelector('[data-qs-scenario]')?.focus({preventScroll:true});
  }
  function syncNetwork() { shared.network=wifiName||'Not connected'; }
  function bluetoothSummary() {
    const connected=devices.filter(d=>d.connected);
    return connected.length>1?`${connected.length} devices connected`:connected[0]?.name||'Not connected';
  }
  function open(value) {
    const old=section;
    stop();password='';passwordVisible=false;pin='';wifiStep='';btStep='';notice='';testing=false;
    section=value;shared.detail=value;scenario=fixtures[value]|| (value==='audio'&&audioTab!=='output'?audioTab:value==='wifi'&&wifiName==='Guest'?'portal':'ready');btEmpty=false;
    redraw(value?'back':null);
    if(!value)root.querySelector(`[data-detail="${old}"]`)?.focus({preventScroll:true});
    history.replaceState(null,'',`#${shared.study}/controls${value?'/'+value:''}`);
  }
  function deactivate() { stop();password='';pin='';wifiStep='';btStep='';testing=false;section=null; }
  function redrawRadio(key) {
    redraw(section==='airplane'?`airplane-radio-${key}`:'radio');
    if(!section)root.querySelector(`[data-toggle="${key}"]`)?.focus({preventScroll:true});
  }
  function toggleRadio(key) {
    if(fixtures.airplane==='blocked') {notice='Use your device’s wireless switch to enable radios.';redrawRadio(key);return;}
    if(fixtures.airplane==='unavailable'&&airplane) {notice='Radio controls are unavailable. Try again from Airplane mode.';redrawRadio(key);return;}
    if(key==='wifi'&&!shared.wifi&&airplane) {
      setAirplane(false);setRadios(true,shared.bluetooth);notice='Airplane mode is off.';redrawRadio(key);
      return;
    }
    stop();shared[key]=!shared[key];notice='';scenario='ready';
    if(key==='wifi') {wifiStep='';password='';wifiName=shared.wifi?'Home network':'';syncNetwork();}
    else {
      btStep='';pin='';devices.forEach(d=>d.connected=shared.bluetooth&&d.paired&&d.name!=='Pocket Speaker');
      if(!shared.bluetooth&&shared.device==='Headphones'){shared.device='Speakers';notice='Headphones disconnected. Sound moved to Speakers.';}
    }
    redrawRadio(key);
  }
  function setRadios(wifi,bluetooth) {
    shared.wifi=wifi;shared.bluetooth=bluetooth;wifiName=wifi?'Home network':'';syncNetwork();
    devices.forEach(d=>d.connected=bluetooth&&d.paired&&d.name!=='Pocket Speaker');
    if(!bluetooth&&shared.device==='Headphones')shared.device='Speakers';
  }
  function setAirplane(enabled) {
    if(enabled&&!airplane) {previousRadios={wifi:shared.wifi,bluetooth:shared.bluetooth};setRadios(false,false);}
    else if(!enabled&&airplane) {setRadios(previousRadios?.wifi??true,previousRadios?.bluetooth??true);previousRadios=null;}
    airplane=enabled;
  }
  function applyTheme(mode) {
    const current=getSettings().material;
    if(current!=='light')darkMaterial=current;
    themeMode=mode;
    lastThemeMaterial=(mode==='system'?systemDark:mode==='dark')?darkMaterial:'light';
    patchSettings({material:lastThemeMaterial});
  }
  function choiceList(label,action,selected,choices) {
    const tabStop=choices.find(c=>c[0]===selected&&!c[4])?.[0]||choices.find(c=>!c[4])?.[0];
    return `<div class="qs-list" role="radiogroup" aria-label="${label}">${choices.map(([id,name,description,symbol,disabled])=>button(action,`<span class="qs-row-icon">${icon(symbol)}</span><span class="qs-row-copy"><b>${name}</b><small>${description}</small></span><span class="qs-radio" aria-hidden="true">${selected===id?icon('check'):''}</span>`,`data-name="${id}" role="radio" aria-checked="${selected===id}" tabindex="${tabStop===id?'0':'-1'}" ${disabled?'disabled':''}`,'qs-row')).join('')}</div>`;
  }
  function serviceHero(symbol,label,title,description,extra='') {
    return `<div class="qs-hero"><div class="qs-hero-top"><div class="qs-orbit">${icon(symbol)}</div><span class="qs-status-pill">${label}</span></div><h3>${title}</h3><p>${description}</p>${extra}</div>`;
  }
  const powerSummary=()=>fixtures.power==='unavailable'?'Power profiles unavailable':profiles.find(p=>p[0]===profile)[1];
  function batteryState() {
    return ({ready:[82,'About 6 h remaining','Discharging',8.2],charging:[64,'About 48 min until full','Charging',24.6],low:[8,'About 24 min remaining','Low battery',9.8],full:[100,'Connected to power','Fully charged',0],unknown:[82,'Calculating remaining time','Discharging',8.2]})[fixtures.battery];
  }
  function batterySummary() {
    const b=batteryState();return b?`${b[0]}% · ${b[2]}`:fixtures.battery==='desktop'?'AC power':'Battery unavailable';
  }
  function overview() {
    const services=[['nightlight','sun','Night light',!shared.night?'Off':paused?'Paused':'On'],['darkmode','moon','Dark mode',`${getSettings().material==='light'?'Off':'On'}${themeMode==='system'?' · System':''}`],['airplane','airplane','Airplane mode',airplane?'On':'Off'],['power','tune','Power profiles',powerSummary()],['wallpaper','picture','Wallpaper',getWallpaper().name]];
    return `${notice?status(esc(notice)):''}<div class="qs-services-grid">${button('focus',`<span class="qs-row-icon">${icon('bell')}</span><span><b>Focus</b><small>${shared.dnd?'On':'Off'}</small></span>`,`aria-pressed="${shared.dnd}"`,'qs-service')}${services.map(([id,symbol,title,description])=>`<button class="qs-service" data-detail="${id}"><span class="qs-row-icon">${icon(symbol)}</span><span><b>${title}</b><small>${esc(description)}</small></span></button>`).join('')}</div>`;
  }
  function nightlightBody() {
    if(scenario==='unavailable')return empty('sun','Night light isn’t available','Reconnect to the display service to adjust screen warmth.');
    const enabled=shared.night&&!paused;
    return serviceHero('sun',!shared.night?'Off':paused?'Paused':'On',enabled?'A softer evening.':'Warmth when you need it.',schedule==='sunset'?'Scheduled from sunset to sunrise.':schedule==='custom'?`${startTime} to ${endTime}, every day.`:'Follows your on and off control.',`<div class="qs-warmth-preview" style="--warmth:${enabled?warmth:0}%" aria-hidden="true"><i></i><i></i><i></i></div><div class="qs-level"><div><label for="qs-warmth">Warmth</label><output id="qs-warmth-value">${warmth}%</output></div><input id="qs-warmth" data-qs-key="warmth" type="range" min="0" max="100" value="${warmth}"></div>`)+
      `<div class="qs-section-title"><h3>Schedule</h3></div>${choiceList('Night light schedule','schedule',schedule,[['sunset','Sunset to sunrise','Uses your location to follow daylight.','sun',scenario==='location'],['custom','Custom hours','Choose when the warmer colors begin and end.','tune'],['manual','Manual','Turn night light on whenever you want.','power']])}${scenario==='location'?status('Location is unavailable. Choose custom hours or manual control.',true):''}${schedule==='custom'?`<form class="qs-inline-card" data-qs-form="schedule"><div class="qs-time-fields"><label>Starts<input type="time" name="start" data-qs-key="start" value="${startTime}" required></label><label>Ends<input type="time" name="end" data-qs-key="end" value="${endTime}" required></label></div>${scheduleError?status(scheduleError,true):''}<div class="qs-actions"><button class="qs-button" type="submit" data-qs-key="save-schedule">Save hours</button></div></form>`:''}${shared.night?`<div class="qs-actions">${button('pause-night',paused?'Resume night light':schedule==='manual'?'Pause night light':'Pause until next schedule','data-qs-key="pause-night"')}</div>`:''}<p class="qs-footnote">The sample above previews warmth. Your display colors stay unchanged.</p>`;
  }
  function darkmodeBody() {
    const selected=themeMode==='system'?'system':getSettings().material==='light'?'light':'dark';
    return serviceHero('moon',getSettings().material==='light'?'Light':'Dark','Your shell, in a different light.','Change the shell’s appearance while keeping your colors and wallpaper.')+`<div class="qs-section-title"><h3>Appearance</h3></div>${choiceList('Color mode','theme',selected,[['light','Light','Bright surfaces and darker text.','sun'],['dark','Dark','Dim surfaces with softer contrast.','moon'],['system','Follow system',scenario==='unavailable'?'System preference is unavailable.':`System currently prefers ${systemDark?'dark':'light'} colors.`,'tune',scenario==='unavailable']])}${scenario==='unavailable'?status('Couldn’t read the system preference. Your current appearance is kept.',true):''}<div class="qs-actions">${button('appearance','More style settings','data-name="style"')}</div>`;
  }
  function airplaneBody() {
    if(scenario==='unavailable')return empty('airplane','Radio service unavailable','Your last radio settings are kept. Reconnect to change airplane mode.');
    if(scenario==='blocked')return empty('airplane','Wireless is switched off','Use the wireless switch on your device to make Wi-Fi and Bluetooth available.','hardware-clear','Preview switch released');
    return serviceHero('airplane',airplane?'On':'Off',airplane?'A little time offline.':'Stay connected.',airplane?'Wi-Fi is off. You can turn Bluetooth back on for headphones and other devices.':'Turn off wireless radios together. Turning airplane mode off restores their previous settings.')+`<div class="qs-section-title"><h3>Wireless radios</h3></div><div class="qs-list">${[['wifi','Wi-Fi'],['bluetooth','Bluetooth']].map(([id,label])=>button('airplane-radio',`<span class="qs-row-icon">${icon(id)}</span><span class="qs-row-copy"><b>${label}</b><small>${shared[id]?'On':'Off'}${id==='wifi'&&airplane?' · Turning on exits airplane mode':''}</small></span><span class="qs-radio">${shared[id]?icon('check'):''}</span>`,`data-name="${id}" aria-pressed="${shared[id]}"`,'qs-row')).join('')}</div>${airplane&&shared.bluetooth?status('Bluetooth is on while airplane mode remains enabled.'):''}<p class="qs-footnote">Wired connections are unaffected.</p>`;
  }
  function powerBody() {
    if(scenario==='unavailable')return empty('tune','Power profiles unavailable','The current profile is kept. Reconnect to the power service to change it.');
    const choices=profiles.filter(p=>scenario!=='unsupported'||p[0]!=='performance').map(p=>p[0]==='performance'&&scenario==='limited'?[p[0],p[1],'Unavailable while the device is too warm.',p[3],true]:p);
    return serviceHero('tune','Active profile',powerSummary(),'Choose how your device balances speed and battery life.')+`<div class="qs-section-title"><h3>Power profile</h3></div>${choiceList('Power profile','profile',profile,choices)}${scenario==='limited'?status('Performance will be available when the device cools down.'):''}${scenario==='unsupported'?'<p class="qs-footnote">This device supports Balanced and Power saver.</p>':''}<div class="qs-actions">${button('detail','Battery details','data-name="battery"')}</div>`;
  }
  function batteryBody() {
    if(scenario==='unavailable')return empty('battery','Battery information unavailable','Reconnect to the battery service to read charge and device status.');
    const b=batteryState();
    const hero=b?serviceHero('battery',b[2],`${b[0]}%`,b[1],`<div class="qs-charge" role="meter" aria-label="Battery charge" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${b[0]}" style="--charge:${b[0]}%"><i></i></div><dl class="qs-facts"><dt>${scenario==='charging'?'Charging at':'Power draw'}</dt><dd>${b[3].toFixed(1)} W</dd><dt>Battery health</dt><dd>92%</dd><dt>Full charge capacity</dt><dd>55.2 Wh of 60 Wh</dd></dl>`):serviceHero('power','AC power','Connected to power.','This device has no internal battery. Connected accessories can still report their charge.');
    return `${scenario==='low'?status('Battery is low. Connect power or switch to Power saver.',true):''}${hero}<div class="qs-actions">${scenario==='low'&&profile!=='saver'?button('save-power','Use Power saver',fixtures.power==='unavailable'?'disabled':'','qs-button qs-primary'):''}${button('detail',`${powerSummary()} ${icon('chevron')}`,'data-name="power"')}</div><div class="qs-section-title"><h3>Connected devices</h3></div><div class="qs-list">${devices.filter(d=>d.connected&&d.battery!==undefined).map(d=>`<div class="qs-row"><span class="qs-row-icon">${icon(d.type)}</span><span class="qs-row-copy"><b>${d.name}</b><small>${d.meta}</small></span><span class="qs-battery">${d.battery}%</span></div>`).join('')||'<p class="qs-small-empty">No connected devices report a battery level.</p>'}</div><p class="qs-footnote">${b?'Time estimates change with use. Health compares full charge capacity with the original capacity.':'Battery details appear when a device reports them.'}</p>`;
  }
  function wallpaperBody() {
    const wall=getWallpaper();
    return `<div class="qs-wallpaper-preview">${wall.art}</div>${serviceHero('picture',esc(wall.display),esc(wall.name),'Choose an image for each display, then preview it with your shell before applying.')}<div class="qs-actions">${button('appearance','Choose wallpaper','data-name="wallpaper"','qs-button qs-primary')}${button('appearance','Style settings','data-name="style"')}</div><p class="qs-footnote">Opens Appearance with your current wallpaper and colors.</p>`;
  }
  function status(message,error=false) { return `<div class="qs-notice ${error?'qs-error':''}" role="${error?'alert':'status'}">${icon(error?'shield':'check')}<span>${message}</span></div>`; }
  function empty(symbol,title,description,action='retry',label='Try again') {
    return `<div class="qs-empty"><div class="qs-empty-art">${icon(symbol)}<i></i><i></i></div><h3>${title}</h3><p>${description}</p>${button(action,label)}</div>`;
  }
  function sectionTitle(title,action='scan',label='Refresh') {
    return `<div class="qs-section-title"><h3>${title}</h3>${button(action,`${icon('refresh')} ${scanning?'Searching…':label}`,`data-qs-key="scan" ${scanning||wifiStep==='connecting'||btStep==='connecting'?'disabled':''}`,'qs-text-button')}</div>`;
  }
  function actions(primary,label,cancel='cancel') {
    return `<div class="qs-actions">${button(cancel,'Cancel')}${button(primary,label,'','qs-button qs-primary')}</div>`;
  }
  function wifiForm() {
    if(!wifiStep)return '';
    if(wifiStep==='connecting')return `<div class="qs-inline-card" role="status"><div class="qs-progress-line"></div><h3>Connecting to ${esc(wifiTarget)}</h3><p>Checking the connection. This may take a moment.</p>${button('cancel','Cancel')}</div>`;
    return `<form class="qs-inline-card" data-qs-form="wifi"><span class="qs-kicker">SECURED NETWORK</span><h3>Connect to ${esc(wifiTarget)}</h3><p>Enter the password for this network.</p>
      <label class="qs-field-label" for="qs-password">Password</label><div class="qs-password-field"><input id="qs-password" data-qs-key="password" type="${passwordVisible?'text':'password'}" value="${esc(password)}" autocomplete="off" spellcheck="false" aria-describedby="qs-password-help" ${wifiStep==='error'?'aria-invalid="true"':''}>${smallIcon('show-password','eye',passwordVisible?'Hide password':'Show password',`aria-pressed="${passwordVisible}" data-qs-key="show-password"`)}</div>
      <div id="qs-password-help" class="qs-help ${wifiStep==='error'?'qs-error-text':''}" ${wifiStep==='error'?'role="alert"':''}>${wifiStep==='error'?'That password didn’t work. Check it and try again.':'Your password is only used for this connection.'}</div>
      <label class="qs-checkbox"><input type="checkbox" data-qs-auto ${autoConnect?'checked':''}> Connect automatically</label><div class="qs-actions">${button('cancel','Cancel')}<button type="submit" class="qs-button qs-primary" data-qs-key="connect" ${password.length<8?'disabled':''}>Connect</button></div></form>`;
  }
  function wifiBody() {
    if(scenario==='unavailable')return empty('wifi','Wi-Fi isn’t available','Check that your wireless adapter is connected, then try again.');
    if(!shared.wifi)return empty('wifi','Wi-Fi is off','Turn it on to discover networks nearby.','enable','Turn on Wi-Fi');
    let result='';
    const connected=networks.find(n=>n.name===wifiName);
    if(wifiName) result=`<div class="qs-hero qs-wifi-hero ${wifiStep?'qs-hero-compact':''}"><div class="qs-hero-top"><div class="qs-orbit">${icon('wifi')}</div><span class="qs-status-pill">${scenario==='portal'?'Action needed':'Connected'}</span></div><h3>${esc(wifiName)}</h3><p>${scenario==='portal'?'Sign in to get internet access.':'You’re online. Make yourself at home.'}</p><div class="qs-connection-meta">${icon(scenario==='portal'?'globe':'lock')} ${scenario==='portal'?'Open network · Limited access':`${connected?.meta||'Strong signal'} · ${connected?.open?'Open':'WPA3'}`}</div><div class="qs-hero-actions">${button('network-info',`${wifiInfo?'Hide details':'Details'} ${icon('chevron')}`,`aria-expanded="${wifiInfo}" data-qs-key="network-info"`,'qs-text-button')}${button('disconnect-wifi','Disconnect','','qs-text-button')}</div>${wifiInfo?`<dl class="qs-facts"><dt>IP address</dt><dd>192.168.1.42</dd><dt>Link speed</dt><dd>866 Mb/s</dd><dt>Auto-connect</dt><dd>${connected?.auto!==false?'On':'Off'}</dd></dl>`:''}${scenario==='portal'?button('portal','Open sign-in '+icon('arrow-right'),'','qs-button qs-primary'):''}</div>`;
    result+=wifiForm();
    result+=sectionTitle('Available networks');
    if(scenario==='empty')return result+empty('wifi','No networks found','Move closer to your router or search again.','scan','Search again');
    return result+`<div class="qs-list">${networks.filter(n=>n.name!==wifiName).map(n=>`<button class="qs-row ${n.name===wifiTarget&&wifiStep?'qs-row-selected':''}" data-qs="network" data-name="${n.name}" ${wifiStep==='connecting'?'disabled':''}><span class="qs-row-icon">${icon('wifi')}</span><span class="qs-row-copy"><b>${n.name}</b><small>${n.meta}${n.saved?' · Saved':''}</small></span><span class="qs-row-end">${n.open?icon('globe'):icon('lock')}${icon('chevron')}</span></button>`).join('')}</div><p class="qs-footnote">${icon('shield')} Secured networks keep your connection private.</p>`;
  }
  function btForm() {
    if(!btStep)return '';
    if(btStep==='connecting')return `<div class="qs-inline-card" role="status"><div class="qs-progress-line"></div><h3>Connecting to ${esc(btTarget)}</h3><p>Keep your device nearby and in pairing mode.</p>${button('cancel','Cancel')}</div>`;
    if(btStep==='error')return `<div class="qs-inline-card qs-failed"><h3>Couldn’t connect</h3><p>Make sure ${esc(btTarget)} is nearby, discoverable, and not connected to another device.</p>${actions('retry-pair','Try again')}</div>`;
    if(btStep==='forget')return `<div class="qs-inline-card"><h3>Forget ${esc(btTarget)}?</h3><p>You’ll need to pair it again to reconnect.</p>${actions('forget-confirm','Forget device')}</div>`;
    if(btStep==='pair')return `<div class="qs-inline-card"><span class="qs-kicker">PAIRING REQUEST</span><h3>Pair with ${esc(btTarget)}?</h3><p>Check that this code matches the one on your device.</p><div class="qs-pair-code" aria-label="Pairing code 4 2 8 1 6 3">428 <span>163</span></div>${actions('confirm-pair','Codes match')}${button('mismatch','The codes don’t match','','qs-text-button qs-centered')}</div>`;
    if(btStep==='pin')return `<form class="qs-inline-card" data-qs-form="pin"><span class="qs-kicker">DEVICE PIN</span><h3>Pair with ${esc(btTarget)}</h3><p>Enter the PIN provided by your device.</p><label class="qs-field-label" for="qs-pin">PIN</label><input class="qs-pin-field" id="qs-pin" data-qs-key="pin" type="text" inputmode="numeric" pattern="[0-9]{4,16}" maxlength="16" value="${esc(pin)}" autocomplete="off"><div class="qs-actions">${button('cancel','Cancel')}<button type="submit" data-qs-key="pair" class="qs-button qs-primary" ${pin.length<4?'disabled':''}>Pair device</button></div></form>`;
    const d=devices.find(d=>d.name===btTarget);
    return `<div class="qs-inline-card"><span class="qs-kicker">PAIRED DEVICE</span><h3>${esc(d.name)}</h3><p>${d.meta}${d.connected?' · Connected':' · Ready to connect'}</p><div class="qs-actions">${button('forget','Forget…')}${button('bt-connect',d.connected?'Disconnect':'Connect','','qs-button qs-primary')}</div></div>`;
  }
  function btRow(d) {
    return `<button class="qs-row ${btTarget===d.name&&btStep?'qs-row-selected':''}" data-qs="bt-device" data-name="${d.name}"><span class="qs-row-icon">${icon(d.type)}</span><span class="qs-row-copy"><b>${d.name}</b><small>${d.connected?'Connected · ':d.paired?'Saved · ':''}${d.meta}</small></span><span class="qs-row-end">${d.connected?`<span class="qs-battery">${icon('battery')}${d.battery}%</span>`:d.paired?icon('chevron'):'<span>Pair</span>'}</span></button>`;
  }
  function bluetoothBody() {
    if(scenario==='unavailable')return empty('bluetooth','Bluetooth isn’t available','Connect a Bluetooth adapter, then try again.');
    if(!shared.bluetooth)return empty('bluetooth','Bluetooth is off','Turn it on to connect your headphones, keyboard, and other devices.','enable','Turn on Bluetooth');
    return `<div class="qs-hero qs-bluetooth-hero ${btStep?'qs-hero-compact':''}"><div class="qs-hero-top"><div class="qs-orbit">${icon('bluetooth')}</div><span class="qs-status-pill">${bluetoothSummary()}</span></div><h3>Your space, connected.</h3><p>Visible as <strong>Phosphor Desktop</strong> while this panel is open.</p></div>${btForm()}<div class="qs-section-title"><h3>Your devices</h3><span>${devices.filter(d=>d.paired).length} paired</span></div><div class="qs-list">${devices.filter(d=>d.paired).map(btRow).join('')||'<p class="qs-help">Paired devices will appear here.</p>'}</div>${sectionTitle('Nearby devices','scan','Scan')}${btEmpty||!devices.some(d=>!d.paired)?'<div class="qs-small-empty">No devices found. Put your device in pairing mode, then scan again.</div>':`<div class="qs-list">${devices.filter(d=>!d.paired).map(btRow).join('')}</div>`}<p class="qs-footnote">${icon('shield')} Only pair with devices you recognize.</p>`;
  }
  function level(key,label,value,isMuted=false) {
    return `<div class="qs-level"><div><label for="qs-level-${key}">${label}</label><output id="qs-value-${key}">${value}%</output></div><div class="qs-level-control">${button('mute',icon(key==='gain'?'microphone':'volume'),`data-name="${key}" data-qs-key="mute-${key}" aria-label="${isMuted?'Unmute':'Mute'} ${label.toLowerCase()}" aria-pressed="${isMuted}"`,'qs-icon-button'+(isMuted?' qs-muted':''))}<input id="qs-level-${key}" data-qs-level="${key}" aria-label="${label}" type="range" min="0" max="100" value="${value}" style="--value:${value}%"></div>${isMuted?'<span class="qs-help">Muted · Your level is remembered.</span>':''}</div>`;
  }
  function outputChoices(value) { return ['Default output',...outputs.map(d=>d[0])].map(name=>`<option ${value===name?'selected':''}>${name}</option>`).join(''); }
  function audioBody() {
    if(scenario==='unavailable')return empty('volume','Sound service unavailable','Your volume settings are kept. Try reconnecting to the audio service.');
    if(scenario==='empty')return empty('headphones','No audio devices','Connect a speaker, headset, or microphone to get started.');
    const tabs=`<div class="qs-tabs" role="tablist" aria-label="Audio controls">${[['output','Output'],['input','Input'],['apps','Apps']].map(([id,label])=>button('audio-tab',`${icon(id==='output'?'headphones':id==='input'?'microphone':'grid')} ${label}`,`data-name="${id}" data-qs-key="tab-${id}" role="tab" id="qs-tab-${id}" aria-controls="qs-audio-panel" aria-selected="${audioTab===id}" tabindex="${audioTab===id?'0':'-1'}"`,'')).join('')}</div>`;
    let content='';
    if(audioTab==='output')content=`<div class="qs-hero qs-audio-hero"><div class="qs-hero-top"><div class="qs-orbit">${icon(outputs.find(d=>d[0]===shared.device)?.[2]||'speaker')}</div><span class="qs-status-pill">${shared.volumeMuted?'Muted':'Default output'}</span></div><h3>${shared.device}</h3><p>${shared.device==='Headphones'?'A little room for your sound.':'Sound, right where you want it.'}</p>${level('volume','Output volume',shared.volume,shared.volumeMuted)}</div><div class="qs-section-title"><h3>Play sound through</h3></div><div class="qs-list" role="radiogroup" aria-label="Output device">${outputs.filter(d=>!(scenario==='unplugged'&&d[0]==='USB DAC')).map(([name,meta,symbol])=>`<button class="qs-row" data-qs="output" data-name="${name}" data-qs-key="output-${name}" role="radio" aria-checked="${shared.device===name}" tabindex="${shared.device===name?'0':'-1'}"><span class="qs-row-icon">${icon(symbol)}</span><span class="qs-row-copy"><b>${name}</b><small>${meta}</small></span><span class="qs-radio" aria-hidden="true">${shared.device===name?icon('check'):''}</span></button>`).join('')}</div>${button('test-output',`${icon('volume')} Play test sound`,'data-qs-key="test-output"','qs-text-button qs-test-button')}<p class="qs-footnote">Apps using the default output follow this device.</p>`;
    else if(audioTab==='input')content=`<div class="qs-hero qs-input-hero"><div class="qs-hero-top"><div class="qs-orbit">${icon('microphone')}</div><span class="qs-status-pill">${inputMuted?'Muted':'Default input'}</span></div><h3>${inputName}</h3><p>${inputMuted?'Your microphone is muted.':'Make yourself heard.'}</p>${level('gain','Input volume',gain,inputMuted)}<div class="qs-input-meter" aria-label="${testing&&!inputMuted?'Simulated microphone signal':'Microphone test idle'}">${Array.from({length:28},(_,i)=>`<i class="${testing&&!inputMuted&&i<17?'lit':''}" style="--i:${i}"></i>`).join('')}</div><div class="qs-meter-label"><span>${testing?'Listening…':'Check your microphone'}</span>${button('test-input',testing?'Stop test':'Test microphone','data-qs-key="test-input"','qs-text-button')}</div></div><div class="qs-section-title"><h3>Record sound from</h3></div><div class="qs-list" role="radiogroup" aria-label="Input device">${inputs.map(([name,meta])=>`<button class="qs-row" data-qs="input" data-name="${name}" data-qs-key="input-${name}" role="radio" aria-checked="${inputName===name}" tabindex="${inputName===name?'0':'-1'}"><span class="qs-row-icon">${icon('microphone')}</span><span class="qs-row-copy"><b>${name}</b><small>${meta}</small></span><span class="qs-radio" aria-hidden="true">${inputName===name?icon('check'):''}</span></button>`).join('')}</div><p class="qs-footnote">${icon('shield')} Testing stops when you leave this view.</p>`;
    else if(scenario==='silent')content=empty('music','A quiet moment','Apps appear here when they start playing sound.','back','Back to quick settings');
    else content=`<div class="qs-apps-heading"><span class="qs-kicker">YOUR MIX</span><h3>A place for every sound.</h3><p>Balance active apps and choose where each one plays.</p></div>${Object.keys(appLevels).map(name=>`<div class="qs-app-card"><div class="qs-app-title"><span class="qs-row-icon">${icon(name==='Music'?'music':'globe')}</span><div><h3>${name}</h3><p>${name==='Music'?'A Walk · Tycho':'Video playback'}</p><span class="qs-app-activity">${appMuted[name]?'Muted':'Playing'}</span></div></div>${level(name,`${name} volume`,appLevels[name],appMuted[name])}<label class="qs-route">Play through<select data-qs-route="${name}" aria-label="${name} output">${outputChoices(appRoutes[name])}</select></label></div>`).join('')}<p class="qs-footnote">Apps appear here when they play sound.</p>`;
    return tabs+`<div id="qs-audio-panel" role="tabpanel" aria-labelledby="qs-tab-${audioTab}">${content}</div>`;
  }
  function renderPreview() {
    review.classList.toggle('hidden',!active);
    if(!active)return;
    review.innerHTML=`<span class="qs-review-label">DETAIL STUDIES</span><nav aria-label="Quick settings detail">${[[null,'Overview'],...Object.entries(titles)].map(([id,label])=>`<button data-qs-preview="${id||'overview'}" aria-pressed="${section===id}">${label}</button>`).join('')}</nav>${section?`<label>Example <select aria-label="Detail scenario" data-qs-scenario>${scenarios[section].map(([id,label])=>`<option value="${id}" ${scenario===id?'selected':''}>${label}</option>`).join('')}</select></label>`:''}<p>Simulated devices.${section==='wifi'?' Use <code>phosphor</code> as the demo password.':section==='audio'?' Audio tests are visual only.':' No system settings change.'}</p>`;
  }
  function render(isActive,detail) {
    active=isActive;section=detail;
    if(getSettings().material!==lastThemeMaterial) {themeMode='manual';lastThemeMaterial=getSettings().material;}
    renderPreview();
    root.classList.toggle('qs-detail',!!detail);
    root.dataset.qsSection=detail||'overview';
    if(!active||!detail)return false;
    const title=titles[detail];
    const pages={wifi:wifiBody,bluetooth:bluetoothBody,audio:audioBody,nightlight:nightlightBody,darkmode:darkmodeBody,airplane:airplaneBody,power:powerBody,battery:batteryBody,wallpaper:wallpaperBody};
    const switches={wifi:shared.wifi,bluetooth:shared.bluetooth,nightlight:shared.night,airplane};
    root.innerHTML=`<div class="qs-heading">${smallIcon('back','arrow-left','Back to quick settings','data-qs-key="back"')}<div><span class="qs-kicker">QUICK SETTINGS</span><h2>${title}</h2></div>${Object.hasOwn(switches,detail)?button('radio','<span></span>',`role="switch" aria-checked="${switches[detail]}" aria-label="${title}" data-qs-key="radio" ${['unavailable','blocked'].includes(scenario)?'disabled':''}`,'qs-switch'):''}<button class="qs-icon-button" data-dismiss aria-label="Close quick settings">${icon('close-icon')}</button></div><div class="qs-body">${notice?status(esc(notice)):''}${pages[detail]()}</div><footer class="qs-footer"><span>${icon(detail==='audio'?'volume':'shield')}${detail==='wifi'?'Your connection':detail==='bluetooth'?'Your devices':detail==='audio'?'Your sound':'Your desktop'}</span><span>PHOSPHOR</span></footer>`;
    for(const element of root.querySelectorAll('[data-qs]')) {
      if(!element.dataset.qsKey)element.dataset.qsKey=element.dataset.qs+(element.dataset.name?'-'+element.dataset.name:'');
    }
    return true;
  }
  function setScenario(value) {
    stop();scenario=value;notice='';password='';passwordVisible=false;pin='';wifiInfo=false;wifiStep='';btStep='';testing=false;
    if(section==='wifi') {
      shared.wifi=value!=='off';wifiName=['empty','unavailable','off'].includes(value)?'':value==='portal'?'Guest':'Home network';
      wifiTarget='Studio 5G';wifiStep=['password','connecting','error'].includes(value)?value:'';syncNetwork();
    } else if(section==='bluetooth') {
      shared.bluetooth=value!=='off';btEmpty=value==='empty';btTarget=value==='pin'?'Pocket Speaker':'Orbit Keyboard';
      devices.forEach(d=>{d.paired=['Headphones','MX Master'].includes(d.name);d.connected=d.paired&&shared.bluetooth&&value!=='unavailable';});
      if(!devices[0].connected&&shared.device==='Headphones')shared.device='Speakers';
      btStep=['pair','pin','connecting','error'].includes(value)?value:'';
    } else if(section==='audio') {
      audioTab=value==='silent'?'apps':['input','apps'].includes(value)?value:'output';shared.volumeMuted=false;inputMuted=false;
      if(value==='unplugged'){shared.device='Speakers';notice='USB DAC disconnected. Sound moved to Speakers.';}
    }
    if(Object.hasOwn(fixtures,section)) {
      fixtures[section]=value;
      if(section==='nightlight') {shared.night=true;paused=value==='paused';schedule=value==='location'?'custom':'sunset';scheduleError='';}
      if(section==='darkmode') {
        if(value.startsWith('system-')) {systemDark=value==='system-dark';applyTheme('system');}
        else if(value==='ready')themeMode='manual';
      }
      if(section==='airplane'&&value!=='unavailable') {
        setAirplane(false);setRadios(true,true);
        if(['on','exception','blocked'].includes(value))setAirplane(true);
        if(value==='exception')setRadios(false,true);
      }
      if(section==='power'&&['limited','unsupported'].includes(value)&&profile==='performance')profile='balanced';
    }
    if(['wifi','bluetooth'].includes(section)) {airplane=false;previousRadios=null;fixtures.airplane='ready';}
    redraw();
  }
  function connectWifi() {
    stop();
    const correct=password==='phosphor'||!password;
    password='';passwordVisible=false;wifiStep='connecting';scenario='connecting';redraw('back');
    later(()=>{
      if(correct){wifiName=wifiTarget;const network=networks.find(n=>n.name===wifiTarget);network.saved=!network.open;network.auto=autoConnect;wifiStep='';scenario=wifiName==='Guest'?'portal':'ready';syncNetwork();notice=`Connected to ${wifiName}.`;}
      else {wifiStep='error';scenario='error';}
      queueMicrotask(()=>focus(correct?'network-info':'password'));
    });
  }
  function pairDevice() {
    stop();
    btStep='connecting';scenario='connecting';pin='';redraw('back');
    later(()=>{const d=devices.find(d=>d.name===btTarget);d.paired=true;d.connected=true;btStep='';scenario='ready';notice=`${d.name} is connected.`;});
  }
  function action(name,value) {
    notice='';
    if(name==='back'){open(null);return;}
    if(name==='radio'||name==='enable') {
      if(section==='nightlight') {shared.night=!shared.night;paused=false;redraw('radio');}
      else if(section==='airplane') {setAirplane(!airplane);redraw('radio');}
      else toggleRadio(section);
      return;
    }
    if(name==='detail'){open(value);return;}
    if(name==='appearance'){onAppearance(value);return;}
    if(name==='focus'){shared.dnd=!shared.dnd;redraw();root.querySelector('[data-qs="focus"]')?.focus({preventScroll:true});return;}
    if(name==='schedule'){schedule=value;scheduleError='';redraw(`schedule-${value}`);return;}
    if(name==='pause-night'){paused=!paused;redraw();return;}
    if(name==='theme'){applyTheme(value);redraw(`theme-${value}`);return;}
    if(name==='airplane-radio'){toggleRadio(value);focus(`airplane-radio-${value}`);return;}
    if(name==='hardware-clear'){setScenario('ready');return;}
    if(name==='profile'){profile=value;redraw(`profile-${value}`);return;}
    if(name==='save-power'){profile='saver';notice='Power saver is active.';redraw('detail-power');return;}

    if(name==='retry'){setScenario('ready');return;}
    if(name==='scan') {
      stop();scanning=true;redraw('scan');later(()=>{scanning=false;scenario='ready';btEmpty=false;notice=section==='wifi'?'Networks updated.':'Nearby devices updated.';});return;
    }
    if(name==='cancel'){stop();password='';pin='';wifiStep='';btStep='';scenario='ready';}
    if(name==='network') {
      stop();wifiTarget=value;password='';passwordVisible=false;
      const n=networks.find(n=>n.name===value);autoConnect=n.auto!==false;
      if(n.saved||n.open){connectWifi();return;}
      wifiStep='password';scenario='password';redraw('password');return;
    }
    if(name==='show-password'){passwordVisible=!passwordVisible;redraw('show-password');return;}
    if(name==='network-info')wifiInfo=!wifiInfo;
    if(name==='disconnect-wifi'){wifiName='';syncNetwork();scenario='ready';wifiInfo=false;}
    if(name==='portal')notice='The network sign-in page would open in your browser. This is a preview.';
    if(name==='bt-device') {
      stop();btTarget=value;const d=devices.find(d=>d.name===value);
      btStep=d.paired?'detail':d.name==='Pocket Speaker'?'pin':'pair';scenario=['pin','pair'].includes(btStep)?btStep:'ready';
    }
    if(name==='confirm-pair'){pairDevice();return;}
    if(name==='mismatch'){btStep='error';scenario='error';}
    if(name==='retry-pair'){btStep=btTarget==='Pocket Speaker'?'pin':'pair';scenario=btStep;}
    if(name==='forget')btStep='forget';
    if(name==='forget-confirm') {
      const d=devices.find(d=>d.name===btTarget);d.paired=false;d.connected=false;btStep='';notice=`${d.name} forgotten.`;
      if(d.name===shared.device){shared.device='Speakers';notice+=' Sound moved to Speakers.';}
    }
    if(name==='bt-connect') {
      const d=devices.find(d=>d.name===btTarget);d.connected=!d.connected;btStep='';
      if(!d.connected&&d.name===shared.device){shared.device='Speakers';notice='Headphones disconnected. Sound moved to Speakers.';}
    }
    if(name==='audio-tab'){audioTab=value;testing=false;if(['ready','input','apps','silent'].includes(scenario))scenario=value==='output'?'ready':value;redraw(`tab-${value}`);return;}
    if(name==='output') {
      if(value==='Headphones'&&fixtures.airplane==='blocked') {notice='Use your device’s wireless switch to connect Bluetooth audio.';redraw();return;}
      if(value==='Headphones'&&fixtures.airplane==='unavailable'&&!shared.bluetooth) {notice='The radio service is unavailable. Use a wired output or try again.';redraw();return;}
      shared.device=value;
      if(value==='Headphones'){shared.bluetooth=true;devices[0].connected=true;devices[0].paired=true;}
      redraw(`output-${value}`);return;
    }
    if(name==='input'){inputName=value;testing=false;redraw(`input-${value}`);return;}
    if(name==='mute') {
      if(value==='volume')shared.volumeMuted=!shared.volumeMuted;else if(value==='gain')inputMuted=!inputMuted;else appMuted[value]=!appMuted[value];
    }
    if(name==='test-output')notice=`Test sound previewed on ${shared.device}. No audio plays in this mockup.`;
    if(name==='test-input')testing=!testing;
    redraw(name==='cancel'?'back':btStep==='pin'?'pin':undefined);
  }
  root.addEventListener('click',e=>{
    const target=e.target.closest('[data-qs]');if(!target)return;
    e.stopPropagation();action(target.dataset.qs,target.dataset.name);
  });
  root.addEventListener('input',e=>{
    if(e.target.id==='qs-warmth') {
      warmth=Number(e.target.value);root.querySelector('#qs-warmth-value').textContent=`${warmth}%`;
      root.querySelector('.qs-warmth-preview').style.setProperty('--warmth',`${shared.night&&!paused?warmth:0}%`);
    }
    if(e.target.id==='qs-password'){password=e.target.value;root.querySelector('[data-qs-key="connect"]').disabled=password.length<8;}
    if(e.target.id==='qs-pin'){pin=e.target.value;root.querySelector('[data-qs-key="pair"]').disabled=!/^\d{4,16}$/.test(pin);}
    if(e.target.dataset.qsLevel) {
      const key=e.target.dataset.qsLevel,value=Number(e.target.value);
      if(key==='volume')shared.volume=value;else if(key==='gain')gain=value;else appLevels[key]=value;
      root.querySelector(`#qs-value-${key}`).textContent=`${value}%`;e.target.style.setProperty('--value',`${value}%`);
    }
  });
  root.addEventListener('change',e=>{
    if(e.target.hasAttribute('data-qs-auto'))autoConnect=e.target.checked;
    if(e.target.dataset.qsRoute)appRoutes[e.target.dataset.qsRoute]=e.target.value;
  });
  root.addEventListener('submit',e=>{
    if(!e.target.dataset.qsForm)return;e.preventDefault();
    if(e.target.dataset.qsForm==='schedule') {
      const start=e.target.elements.start.value,end=e.target.elements.end.value;
      if(!/^\d{2}:\d{2}$/.test(start)||!/^\d{2}:\d{2}$/.test(end)||start===end) {
        scheduleError='Choose different start and end times.';
        // Keep the draft in the form while reporting a validation error.
        e.target.querySelector('[role="alert"]')?.remove();
        e.target.querySelector('.qs-actions').insertAdjacentHTML('beforebegin',status(scheduleError,true));
      } else {startTime=start;endTime=end;scheduleError='';notice='Night light hours saved.';redraw('save-schedule');}
      return;
    }
    if(e.target.dataset.qsForm==='wifi'&&password.length>=8)connectWifi();
    if(e.target.dataset.qsForm==='pin'&&/^\d{4,16}$/.test(pin))pairDevice();
  });
  review.addEventListener('click',e=>{const target=e.target.closest('[data-qs-preview]');if(target)open(target.dataset.qsPreview==='overview'?null:target.dataset.qsPreview);});
  review.addEventListener('change',e=>{if(e.target.hasAttribute('data-qs-scenario'))setScenario(e.target.value);});
  function handleKey(e) {
    if(!active||!section)return false;
    if(e.key==='Escape'){e.preventDefault();if(wifiStep||btStep)action('cancel');else open(null);return true;}
    const target=e.target.closest('[role="radio"],[role="tab"]');
    if(!target||!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(e.key))return false;
    e.preventDefault();const group=target.parentElement,items=[...group.querySelectorAll('[role="radio"]:not(:disabled),[role="tab"]:not(:disabled)')],i=items.indexOf(target);
    const next=e.key==='Home'?0:e.key==='End'?items.length-1:(i+(['ArrowLeft','ArrowUp'].includes(e.key)?items.length-1:1))%items.length;
    action(items[next].dataset.qs,items[next].dataset.name);return true;
  }
  return {render,open,deactivate,toggleRadio,bluetoothSummary,handleKey,overview,batterySummary,powerSummary};
}};
