#pragma once
// The whole web interface in one page. Tabs are built from /api/tabs, so
// any app with tab.label= in its app.ini gets its own tab automatically.

static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>InkDeck</title>
<style>
:root{--desk:#a0a0a0;--card:#fff;--ink:#000;--mute:#555;--line:#000;--soft:#c6c6c6;--sys:Chicago,ChicagoFLF,Charcoal,"Geneva",Verdana,sans-serif}
*{box-sizing:border-box}
body{margin:0;padding:0 10px 30px;background:var(--desk);color:var(--ink);font:13px/1.45 Geneva,Verdana,"Segoe UI",sans-serif}
/* Menu bar */
.menubar{position:sticky;top:0;z-index:5;margin:0 -10px;display:flex;align-items:center;gap:18px;height:22px;padding:0 14px;background:#fff;border-bottom:1px solid #000;font:bold 13px var(--sys)}
.menubar .clock{margin-left:auto}
/* Window */
.win{max-width:860px;margin:20px auto 0;background:#fff;border:1px solid #000;box-shadow:2px 2px 0 #000}
.titlebar{position:relative;height:20px;border-bottom:1px solid #000;display:flex;align-items:center;justify-content:center}
.titlebar::before{content:"";position:absolute;left:2px;right:2px;top:4px;bottom:4px;background:repeating-linear-gradient(to bottom,#000 0,#000 1px,#fff 1px,#fff 2px)}
.ttl{position:relative;background:#fff;padding:0 8px;font:bold 13px var(--sys)}
.close,.zoom{position:absolute;top:4px;width:12px;height:12px;border:1px solid #000;background:#fff;box-shadow:0 0 0 2px #fff}
.close{left:9px}.zoom{right:9px}.zoom::after{content:"";position:absolute;left:0;top:0;width:6px;height:6px;border-right:1px solid #000;border-bottom:1px solid #000}
.infobar{display:flex;justify-content:space-between;flex-wrap:wrap;gap:4px 16px;padding:3px 10px;border-bottom:3px double #000;font-size:12px}
nav{display:flex;gap:8px;padding:12px 16px 0;flex-wrap:wrap}
main{padding:14px 16px 18px}
/* Controls: rounded push buttons, square fields */
button{font:inherit;color:#000;background:#fff;border:1px solid #000;border-radius:7px;cursor:pointer}
button:active,button.on{background:#000;color:#fff}
nav button{padding:3px 14px;font:bold 13px var(--sys)}
button.b{padding:3px 14px;box-shadow:0 0 0 2px #fff,0 0 0 3px #000;margin:3px}
button.x{padding:1px 9px;font-size:12px}
button:disabled{color:#999;border-color:#999;cursor:default;box-shadow:none}
input[type=text],input[type=password],select{font:inherit;padding:3px 6px;border:1px solid #000;border-radius:0;background:#fff;color:#000;min-width:200px}
input[type=file]{font:inherit;font-size:12px}
input[type=file]::file-selector-button{font:inherit;color:#000;background:#fff;border:1px solid #000;border-radius:7px;padding:1px 10px;margin-right:8px;cursor:pointer}
input[type=checkbox]{accent-color:#000}
a.x{display:inline-block;color:#000;background:#fff;border:1px solid #000;border-radius:7px;padding:1px 9px;font-size:12px;text-decoration:none}
a.x:active{background:#000;color:#fff}
.row .mute{margin:0}
a{color:#000}
h2{margin:0 0 4px;font:bold 14px var(--sys)}.mute{color:var(--mute);font-size:12px;margin:0 0 12px}
.box{background:#fff;border:1px solid #000;padding:12px;margin:0 0 16px}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #000}
td{padding:5px 10px;border-bottom:1px solid var(--soft);vertical-align:middle}td.r{text-align:right;white-space:nowrap}
#st{font-size:12px}.err{color:#a00000;font-weight:bold}.warn{color:#a00000}
/* Editor */
.ed{display:grid;grid-template-columns:190px 1fr;gap:14px}
.edside ul{list-style:none;margin:8px 0 0;padding:0;border:1px solid #000;background:#fff;max-height:60vh;overflow:auto}
.edside li{padding:4px 8px;border-bottom:1px solid var(--soft);cursor:pointer;word-break:break-all}
.edside li.on{background:#000;color:#fff}.edside li:hover:not(.on){background:#e4e4e4}
.edside label input{display:block;margin-top:4px;max-width:100%}
.edmain{min-width:0}.edmain .row{margin-bottom:8px}
#ename{min-width:0;flex:1 1 160px}
.tb{display:flex;flex-wrap:wrap;gap:4px;margin-bottom:6px}
.tb button{font-size:12px;padding:1px 9px;min-width:32px}
.split{display:grid;grid-template-columns:1fr 1fr;gap:10px}.single{display:block}
#etext{width:100%;height:60vh;min-height:300px;font:13px/1.5 Monaco,Menlo,Consolas,monospace;padding:8px;border:1px solid #000;border-radius:0;background:#fff;color:#000;resize:vertical;tab-size:2}
.prev{height:60vh;min-height:300px;overflow:auto;border:1px solid #000;background:#fff;padding:8px 14px;line-height:1.45}
.prev h1,.prev h2,.prev h3{margin:.3em 0 .2em;line-height:1.2;font-family:var(--sys)}.prev h1{font-size:1.6em;border-bottom:2px solid #000}
.prev h2{font-size:1.25em;border-bottom:1px solid #000}.prev h3{font-size:1.05em}
.prev p{margin:0 0 .15em}.prev .gap{height:.6em}.prev ul,.prev ol{margin:0;padding-left:1.4em}.prev li.task{list-style:none;margin-left:-1.3em}
.prev blockquote{margin:.1em 0;padding-left:10px;border-left:3px solid #000}.prev hr{border:0;border-top:2px solid #000}
.prev code{background:#000;color:#fff;padding:0 3px}.prev pre{background:#fff;border-left:3px solid #000;padding:6px 10px;margin:.2em 0;white-space:pre-wrap;font-family:Monaco,Menlo,monospace}
@media(max-width:760px){.ed{grid-template-columns:1fr}.split{grid-template-columns:1fr}.prev,#etext{height:40vh}.win{margin-top:10px}}
</style></head><body>
<div class="menubar"><span>&#9632; InkDeck</span><span class="clock" id="mclock"></span></div>
<div class="win">
 <div class="titlebar"><span class="close"></span><span class="ttl" id="wtitle">InkDeck</span><span class="zoom"></span></div>
 <div class="infobar"><span id="hdr">connecting...</span><span id="hdr2"></span></div>
 <nav id="tabs"></nav>
 <main id="main"></main>
</div>
<script>
const $=s=>document.querySelector(s),enc=encodeURIComponent;
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const size=b=>b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(1)+' MB';
let tabs=[],cur=null;
async function j(u,o){const r=await fetch(u,o);const t=await r.text();if(!r.ok)throw new Error(t||r.status);return t?JSON.parse(t):{}}
async function up(path,file){const fd=new FormData();fd.append('file',file,file.name);
 const r=await fetch('/api/upload?path='+enc(path),{method:'POST',body:fd});if(!r.ok)throw new Error(await r.text())}
function status(msg,bad){const s=$('#st');if(s){s.textContent=msg;s.className=bad?'err':''}}

async function loadTabs(){
 tabs=await j('/api/tabs');const nav=$('#tabs');nav.innerHTML='';
 for(const t of tabs){const b=document.createElement('button');b.textContent=t.label;b.id='t-'+t.id;b.onclick=()=>show(t);nav.appendChild(b)}
 show((cur&&tabs.find(t=>t.id==cur.id))||tabs[0]);
}
let leaveGuard=null;             // set by the editor while it has unsaved changes
window.addEventListener('beforeunload',e=>{if(leaveGuard&&leaveGuard()){e.preventDefault();e.returnValue=''}});
function show(t){
 if(leaveGuard&&leaveGuard()&&!confirm('You have unsaved changes. Leave without saving?'))return;
 leaveGuard=null;$('.win').style.maxWidth=t.type==='editor'?'1300px':'860px';$('#wtitle').textContent=t.label;
 cur=t;document.querySelectorAll('#tabs button').forEach(b=>b.classList.toggle('on',b.id=='t-'+t.id));
 ({apps:appsTab,folder:folderTab,page:pageTab,editor:editorTab,system:systemTab})[t.type](t).catch(e=>$('#main').innerHTML='<p class=err>'+esc(e.message)+'</p>')}

// ---------- Apps ----------
async function appsTab(){
 $('#main').innerHTML=`<h2>Apps</h2><p class=mute>Each app is a folder with app.ini and main.lua. Installing a folder with the same name updates that app.</p>
 <div class=box><div class=row><input type=file id=f webkitdirectory><button class=b id=u>Install folder</button></div><div id=st></div></div>
 <table id=ls></table>`;
 $('#u').onclick=async()=>{
  const fs=[...$('#f').files];if(!fs.length)return status('Pick an app folder first',1);
  const root=fs[0].webkitRelativePath.split('/')[0];
  if(!fs.some(f=>f.webkitRelativePath==root+'/app.ini'))return status('That folder has no app.ini',1);
  const id=root.toLowerCase().replace(/[^a-z0-9_-]/g,'');if(!id)return status('Folder name needs letters or numbers',1);
  try{for(const[i,f]of fs.entries()){const rel=f.webkitRelativePath.split('/').slice(1).join('/');
    if(rel.split('/').some(p=>p.startsWith('.')))continue;
    status(`Uploading ${i+1}/${fs.length}: ${rel}`);await up('/apps/'+id+'/'+rel,f)}
   status('Installed "'+id+'". Exit the Uploader on the device to see it on the home screen.');await loadTabs()}
  catch(e){status('Upload failed: '+e.message,1)}};
 const apps=await j('/api/apps'),tb=$('#ls');
 tb.innerHTML=apps.length?'':'<tr><td class=mute>No apps installed yet</td></tr>';
 for(const a of apps){const tr=document.createElement('tr');
  const ic=a.image?`<img src="${esc(a.image)}" width=32 height=32 style="image-rendering:pixelated;vertical-align:middle;margin-right:8px">`:`<span style="display:inline-block;width:32px;margin-right:8px;text-align:center">[${esc(a.icon)}]</span>`;
  tr.innerHTML=`<td>${ic}<b>${esc(a.name)}</b>${a.version?' <span class=mute>v'+esc(a.version)+'</span>':''}${a.description?'<div class=mute style="margin:2px 0 0 40px">'+esc(a.description)+'</div>':''}</td><td class=mute>/apps/${esc(a.id)}${a.tab?'<br>tab: '+esc(a.tab):''}</td><td class=r><button class=x>delete</button></td>`;
  tr.querySelector('button').onclick=async()=>{if(!confirm('Delete app '+a.name+' and all its data?'))return;
   await j('/api/delete?path='+enc('/apps/'+a.id),{method:'POST'});await loadTabs()};
  tb.appendChild(tr)}
}

// ---------- Folder (Notes + any app tab) ----------
async function folderTab(t){
 $('#main').innerHTML=`<h2>${esc(t.label)}</h2><p class=mute>${esc(t.path)}</p>
 <div class=box><div class=row><input type=file id=f multiple accept="${esc(t.accept||'')}"><button class=b id=u>Upload</button></div><div id=st></div></div>
 <table id=ls></table>`;
 $('#u').onclick=async()=>{const fs=[...$('#f').files];if(!fs.length)return status('Pick some files first',1);
  try{for(const[i,f]of fs.entries()){status(`Uploading ${i+1}/${fs.length}: ${f.name}`);await up(t.path+'/'+f.name,f)}
   status('Done');$('#f').value='';await listFolder(t)}catch(e){status('Upload failed: '+e.message,1)}};
 await listFolder(t);
}
async function listFolder(t){
 const d=(await j('/api/list?dir='+enc(t.path))).filter(e=>!e.dir),tb=$('#ls');
 tb.innerHTML=d.length?'':'<tr><td class=mute>(empty)</td></tr>';
 for(const e of d){const p=t.path+'/'+e.name,tr=document.createElement('tr');
  tr.innerHTML=`<td>${esc(e.name)}</td><td class=r>${size(e.size)}</td><td class=r><a href="/api/download?path=${enc(p)}">download</a> &nbsp;<button class=x>delete</button></td>`;
  tr.querySelector('button').onclick=async()=>{if(!confirm('Delete '+e.name+'?'))return;await j('/api/delete?path='+enc(p),{method:'POST'});listFolder(t)};
  tb.appendChild(tr)}
}

// ---------- An app's own page ----------
async function pageTab(t){
 $('#main').innerHTML=`<iframe src="${esc(t.url)}" title="${esc(t.label)}" style="width:100%;height:calc(100vh - 150px);min-height:420px;border:1px solid var(--line);background:var(--card)"></iframe>`;
}

// ---------- Editor: Notes (plain text) and markdown apps ----------
// The device fonts are plain ASCII, so typographic characters are converted on save
function asciiClean(t){
 return t.replace(/\r\n?/g,'\n').replace(/\t/g,'  ')
  .replace(/[\u2018\u2019\u201A\u201B\u2032]/g,"'").replace(/[\u201C\u201D\u201E\u201F\u2033]/g,'"')
  .replace(/[\u2014\u2015]/g,'--').replace(/[\u2013\u2212]/g,'-').replace(/\u2026/g,'...')
  .replace(/[\u00A0\u2007\u202F]/g,' ').replace(/[\u200B\uFEFF]/g,'');
}
function suggestName(text){
 let src=(text.split('\n').find(l=>/\S/.test(l))||'untitled');
 src=src.replace(/^\s*#+\s*/,'').replace(/^\s*[-*+]\s+\[[ xX]\]\s*/,'').replace(/^\s*[-*+>]\s+/,'').replace(/^\s*\d+[.)]\s+/,'').replace(/[*_~`\[\]()>#]/g,' ');
 const w=(src.match(/[A-Za-z0-9'-]+/g)||[]).filter(x=>/[A-Za-z0-9]/.test(x)).slice(0,4);
 return w.length?w.join('_'):'untitled';
}

// Markdown preview, following the same rules as the device editor
function mdInline(s){
 s=esc(s);const codes=[];
 s=s.replace(/`([^`]+)`/g,(_,c)=>{codes.push(c);return '\u0000'+(codes.length-1)+'\u0000'});
 s=s.replace(/\[([^\]]+)\]\(([^)\s]*)\)/g,(_,t,u)=>`<a href="${/^\s*javascript:/i.test(u)?'#':u}" target="_blank" rel="noopener">${t}</a>`);
 s=s.replace(/\*\*\*(?=\S)(.*?\S)\*\*\*/g,'<b><i>$1</i></b>')
  .replace(/\*\*(?=\S)(.*?\S)\*\*/g,'<b>$1</b>')
  .replace(/(^|[\s\p{P}])__(?=\S)(.*?\S)__(?=$|[\s\p{P}])/gu,'$1<b>$2</b>')
  .replace(/~~(?=\S)(.*?\S)~~/g,'<s>$1</s>')
  .replace(/\*(?=\S)(.*?\S)\*/g,'<i>$1</i>')
  .replace(/(^|[\s\p{P}])_(?=\S)(.*?\S)_(?=$|[\s\p{P}])/gu,'$1<i>$2</i>');
 return s.replace(/\u0000(\d+)\u0000/g,(_,n)=>'<code>'+codes[n]+'</code>');
}
function mdRender(src){
 let h='',inCode=false,list=null;
 const close=()=>{if(list){h+='</'+list+'>';list=null}};
 const open=tag=>{if(list!==tag){close();h+='<'+tag+'>';list=tag}};
 src.split('\n').forEach((ln,i)=>{
  let m;
  if(/^\s*```/.test(ln)){close();h+=inCode?'</pre>':'<pre>';inCode=!inCode;return}
  if(inCode){h+=esc(ln)+'\n';return}
  if(m=ln.match(/^(#+)\s(.*)$/)){close();const n=Math.min(m[1].length,3);h+=`<h${n}>${mdInline(m[2])}</h${n}>`;return}
  if(/^\s*(-{3,}|\*{3,}|_{3,})\s*$/.test(ln)){close();h+='<hr>';return}
  if(m=ln.match(/^(\s*)[-*+] \[([ xX])\] (.*)$/)){open('ul');h+=`<li class="task" style="margin-left:${m[1].length/2-1.3}em"><input type="checkbox" data-line="${i}"${m[2]!==' '?' checked':''}> ${mdInline(m[3])}</li>`;return}
  if(m=ln.match(/^(\s*)[-*+] (.*)$/)){open('ul');h+=`<li style="margin-left:${m[1].length/2}em">${mdInline(m[2])}</li>`;return}
  if(m=ln.match(/^(\s*)(\d+)[.)] (.*)$/)){open('ol');h+=`<li value="${m[2]}" style="margin-left:${m[1].length/2}em">${mdInline(m[3])}</li>`;return}
  if(m=ln.match(/^> ?(.*)$/)){close();h+=`<blockquote>${mdInline(m[1])}</blockquote>`;return}
  close();h+=ln.trim()?`<p>${mdInline(ln)}</p>`:'<div class="gap"></div>';
 });
 close();if(inCode)h+='</pre>';
 return h;
}

async function editorTab(t){
 const md=t.format==='markdown', DEF=t.ext||(md?'.md':'.txt');
 const okExt=md?/\.(md|txt)$/i:/\.txt$/i;
 const st={name:null,ext:DEF,orig:'',origBase:'',files:[]};
 $('#main').innerHTML=`<div class="ed">
  <aside class="edside"><button class="b" id="enew">+ New</button><ul id="elist"></ul>
   <label class="mute" style="display:block;margin-top:12px">Upload files<input type="file" id="eup" multiple accept="${esc(t.accept||DEF)}"></label></aside>
  <section class="edmain">
   <div class="row"><input type="text" id="ename" placeholder="file name" maxlength="60"><span class="mute" id="eext"></span>
    <button class="b" id="esave">Save</button><a class="x" id="edl" style="text-decoration:none">download</a><button class="x" id="edel">delete</button></div>
   ${md?'<div class="tb" id="etb"></div>':''}
   <div class="${md?'split':'single'}"><textarea id="etext" spellcheck="true"></textarea>${md?'<div class="prev" id="eprev"></div>':''}</div>
   <div class="row" style="margin-top:6px"><span id="st"></span><span class="mute" id="einfo" style="margin-left:auto"></span></div>
  </section></div>`;
 const ta=$('#etext'),nm=$('#ename');
 const dirty=()=>ta.value!==st.orig||nm.value!==st.origBase;
 leaveGuard=dirty;

 function info(){
  const v=ta.value,words=(v.match(/\S+/g)||[]).length;
  const bad=[...new Set((asciiClean(v).match(/[^\x00-\x7F]/g)||[]))];
  let s=`${v.length} chars, ${words} words`;
  if(t.maxChars&&v.length>t.maxChars)s+=` <span class="warn">- the device opens only the first ${t.maxChars}</span>`;
  if(bad.length)s+=` <span class="warn">- can't show on the device: ${esc(bad.slice(0,8).join(' '))}</span>`;
  $('#einfo').innerHTML=s;
  status(dirty()?'Unsaved changes':(st.name?'Saved':''));
 }
 let timer=null;
 function changed(){info();if(md){clearTimeout(timer);timer=setTimeout(()=>{$('#eprev').innerHTML=mdRender(ta.value)},120)}}
 ta.addEventListener('input',changed);nm.addEventListener('input',info);

 async function refresh(){
  let l=[];try{l=await j('/api/list?dir='+enc(t.path))}catch(e){}
  st.files=l.filter(e=>!e.dir&&okExt.test(e.name));
  const ul=$('#elist');ul.innerHTML='';
  if(!st.files.length)ul.innerHTML='<li class="mute" style="cursor:default">No files yet</li>';
  for(const f of st.files){const li=document.createElement('li');li.textContent=f.name;li.title=size(f.size);
   if(f.name===st.name)li.className='on';li.onclick=()=>openFile(f.name);ul.appendChild(li)}
 }
 function load(name,text){
  st.name=name;st.orig=text;
  const m=name&&name.match(/^(.*?)(\.(md|txt))$/i);
  st.ext=m?m[2]:DEF;st.origBase=name?(m?m[1]:name):'';
  nm.value=st.origBase;$('#eext').textContent=st.ext;ta.value=text;
  $('#edl').href=name?'/api/download?path='+enc(t.path+'/'+name):'#';
  $('#edl').style.visibility=$('#edel').style.visibility=name?'visible':'hidden';
  document.querySelectorAll('#elist li').forEach(li=>li.classList.toggle('on',li.textContent===name));
  changed();
 }
 const ok=()=>!dirty()||confirm('You have unsaved changes. Discard them?');
 async function openFile(name){
  if(name===st.name||!ok())return;
  try{const r=await fetch('/api/download?path='+enc(t.path+'/'+name));if(!r.ok)throw new Error(await r.text());load(name,(await r.text()).replace(/\r/g,''))}
  catch(e){status('Could not open: '+e.message,1)}
 }
 async function save(){
  const cleaned=asciiClean(ta.value);
  if(cleaned!==ta.value){const p=ta.selectionStart;ta.value=cleaned;ta.selectionStart=ta.selectionEnd=Math.min(p,cleaned.length)}
  let base=(nm.value.trim()||suggestName(cleaned)).replace(/\s+/g,'_').replace(/[^A-Za-z0-9_\-.']/g,'');
  if(!base)return status('Give it a name first',1);
  const fname=okExt.test(base)?base:base+st.ext, path=t.path+'/'+fname;
  if(t.maxChars&&cleaned.length>t.maxChars&&!confirm(`This is ${cleaned.length} characters but the device's Notes app only opens the first ${t.maxChars}. Save anyway?`))return;
  const exists=st.files.some(f=>f.name===fname);
  if(fname!==st.name&&exists&&!confirm(fname+' already exists. Replace it?'))return;
  try{
   status('Saving...');
   if(st.name&&fname!==st.name){
    if(exists)await j('/api/delete?path='+enc(path),{method:'POST'});
    await j('/api/rename',{method:'POST',body:new URLSearchParams({from:t.path+'/'+st.name,to:path})});
   }
   await up(path,new File([cleaned],fname,{type:'text/plain'}));
   await refresh();load(fname,cleaned);status('Saved '+fname);
  }catch(e){status('Save failed: '+e.message,1)}
 }
 $('#esave').onclick=save;
 $('#enew').onclick=()=>{if(ok()){load(null,'');nm.focus()}};
 $('#edel').onclick=async()=>{
  if(!st.name||!confirm('Delete '+st.name+'?'))return;
  try{await j('/api/delete?path='+enc(t.path+'/'+st.name),{method:'POST'});st.orig=ta.value='';st.origBase=nm.value='';load(null,'');await refresh();status('Deleted')}
  catch(e){status('Delete failed: '+e.message,1)}
 };
 $('#eup').onchange=async e=>{
  const fs=[...e.target.files];
  try{for(const f of fs){status('Uploading '+f.name+'...');await up(t.path+'/'+f.name.replace(/\s+/g,'_'),f)}status('Uploaded '+fs.length+' file(s)')}
  catch(err){status('Upload failed: '+err.message,1)}
  e.target.value='';refresh();
 };

 // ---- keyboard: Ctrl/Cmd+S save, Tab indents, markdown shortcuts and list continuation
 function wrap(m){
  let a=ta.selectionStart,b=ta.selectionEnd;const v=ta.value,L=m.length;
  while(a<b&&v[a]===' ')a++;while(b>a&&v[b-1]===' ')b--;
  if(a===b){
   if(v.slice(a-L,a)===m&&v.slice(a,a+L)===m){ta.setRangeText('',a-L,a+L,'end')}
   else{ta.setRangeText(m+m,a,a,'start');ta.selectionStart=ta.selectionEnd=a+L}
  }else if(v.slice(a-L,a)===m&&v.slice(b,b+L)===m){ta.setRangeText(v.slice(a,b),a-L,b+L,'select')}
  else{ta.setRangeText(m+v.slice(a,b)+m,a,b,'select');ta.selectionStart=a+L;ta.selectionEnd=b+L}
  ta.focus();changed();
 }
 function lines(fn){                      // apply fn to every selected line
  const v=ta.value,a=v.lastIndexOf('\n',ta.selectionStart-1)+1;
  let b=v.indexOf('\n',ta.selectionEnd);if(b<0)b=v.length;
  const out=v.slice(a,b).split('\n').map(fn).join('\n');
  ta.setRangeText(out,a,b,'end');ta.focus();changed();
 }
 const strip=l=>l.replace(/^#+\s/,'').replace(/^(\s*)[-*+]\s+\[[ xX]\]\s/,'$1').replace(/^(\s*)[-*+>]\s/,'$1').replace(/^(\s*)\d+[.)]\s/,'$1');
 const TOOLS=[
  ['B','Bold (Ctrl+B)',()=>wrap('**'),'font-weight:bold'],['I','Italic (Ctrl+I)',()=>wrap('*'),'font-style:italic'],
  ['S','Strike',()=>wrap('~~'),'text-decoration:line-through'],['<>','Code',()=>wrap('`'),''],
  ['H1','Heading 1',()=>lines(l=>/^# /.test(l)?l.slice(2):'# '+strip(l)),''],
  ['H2','Heading 2',()=>lines(l=>/^## /.test(l)?l.slice(3):'## '+strip(l)),''],
  ['H3','Heading 3',()=>lines(l=>/^### /.test(l)?l.slice(4):'### '+strip(l)),''],
  ['&bull; List','Bullet list',()=>lines(l=>/^\s*[-*+] (?!\[)/.test(l)?strip(l):'- '+strip(l)),''],
  ['1. List','Numbered list',()=>{let n=0;lines(l=>/^\s*\d+[.)] /.test(l)?strip(l):(++n)+'. '+strip(l))},''],
  ['&#9744; Task','Checkbox',()=>lines(l=>/^\s*[-*+] \[[ xX]\] /.test(l)?strip(l):'- [ ] '+strip(l)),''],
  ['&gt; Quote','Quote',()=>lines(l=>/^> /.test(l)?l.slice(2):'> '+strip(l)),''],
  ['Link','Link (Ctrl+K)',()=>{const a=ta.selectionStart,b=ta.selectionEnd,txt=ta.value.slice(a,b)||'text';
    ta.setRangeText('['+txt+'](https://)',a,b,'end');ta.selectionStart=a+txt.length+3;ta.selectionEnd=a+txt.length+11;ta.focus();changed()},''],
  ['&mdash;','Horizontal rule',()=>{const a=ta.selectionStart;ta.setRangeText((a&&ta.value[a-1]!=='\n'?'\n':'')+'---\n',a,a,'end');ta.focus();changed()},''],
 ];
 if(md)for(const[lbl,tip,fn,css]of TOOLS){const b=document.createElement('button');b.innerHTML=lbl;b.title=tip;b.style.cssText=css;b.onmousedown=e=>e.preventDefault();b.onclick=fn;$('#etb').appendChild(b)}

 ta.addEventListener('keydown',e=>{
  const mod=e.ctrlKey||e.metaKey,k=e.key.toLowerCase();
  if(mod&&k==='s'){e.preventDefault();save();return}
  if(md&&mod&&k==='b'){e.preventDefault();wrap('**');return}
  if(md&&mod&&k==='i'){e.preventDefault();wrap('*');return}
  if(md&&mod&&k==='k'){e.preventDefault();TOOLS[11][2]();return}
  if(e.key==='Tab'&&!mod){e.preventDefault();const a=ta.selectionStart;ta.setRangeText('  ',a,ta.selectionEnd,'end');changed();return}
  if(md&&e.key==='Enter'&&!e.shiftKey&&!mod&&ta.selectionStart===ta.selectionEnd){
   const v=ta.value,p=ta.selectionStart,ls=v.lastIndexOf('\n',p-1)+1,line=v.slice(ls,p);let m,pre=null,body='';
   if(m=line.match(/^(\s*)([-*+]) \[[ xX]\] (.*)$/)){pre=m[1]+m[2]+' [ ] ';body=m[3]}
   else if(m=line.match(/^(\s*)([-*+]) (.*)$/)){pre=m[1]+m[2]+' ';body=m[3]}
   else if(m=line.match(/^(\s*)(\d+)([.)]) (.*)$/)){pre=m[1]+(+m[2]+1)+m[3]+' ';body=m[4]}
   else if(m=line.match(/^> ?(.*)$/)){pre='> ';body=m[1]}
   if(pre===null)return;
   e.preventDefault();
   if(!body.trim())ta.setRangeText('',ls,p,'end');       // empty item ends the list
   else ta.setRangeText('\n'+pre,p,p,'end');
   changed();
  }
 });
 // Tick checkboxes straight from the preview
 if(md)$('#eprev').addEventListener('change',e=>{
  const i=+e.target.dataset.line;if(isNaN(i))return;
  const ls=ta.value.split('\n');ls[i]=ls[i].replace(/\[([ xX])\]/,(_,c)=>c===' '?'[x]':'[ ]');
  const p=ta.selectionStart;ta.value=ls.join('\n');ta.selectionStart=ta.selectionEnd=p;changed();
 });

 await refresh();
 if(st.files.length)await openFile(st.files[0].name);else load(null,'');
}

// ---------- System ----------
async function systemTab(){
 const i=await j('/api/info');
 $('#main').innerHTML=`<h2>System</h2><p class=mute>Firmware ${esc(i.firmware)}</p>
 <table>${Object.entries({Chip:i.chip,'Free RAM':i.heap,PSRAM:i.psram,Display:i.display,'SD card':i.sd,Network:i.network,Address:i.ip,'Lua apps':i.apps}).map(([k,v])=>`<tr><td>${k}</td><td>${esc(v)}</td></tr>`).join('')}</table>
 <h2 style="margin-top:22px">WiFi</h2><p class=mute>Save your home WiFi so the device joins it next time you open the Uploader. Leave the name empty to always use the device's own hotspot.</p>
 <div class=box><div class=row><input type=text id=ss placeholder="WiFi name" value="${esc(i.saved_ssid)}"><input type=password id=pw placeholder="Password"><button class=b id=sv>Save</button></div><div id=st></div></div>`;
 $('#sv').onclick=async()=>{try{await j('/api/wifi',{method:'POST',body:new URLSearchParams({ssid:$('#ss').value,password:$('#pw').value})});
  status('Saved. Takes effect the next time you open the Uploader.')}catch(e){status('Failed: '+e.message,1)}};
 await timeSection();
 fwSection(i);
}

// ---------- Firmware update ----------
function fwSection(i){
 const box=document.createElement('div');
 const mb=n=>(n/1048576).toFixed(1)+' MB';
 box.innerHTML=`<h2 style="margin-top:22px">Firmware</h2>
 <p class=mute>Running <b>${esc(i.firmware)}</b>.</p>
 <div class=box id=fwbox></div>`;
 $('#main').appendChild(box);
 if(!i.ota){
  $('#fwbox').innerHTML=`<p style="margin:0 0 6px">This device can't update over WiFi yet. One last time over USB, in Arduino IDE:</p>
  <ol style="margin:0;padding-left:20px"><li>Tools &rarr; Flash Size &rarr; <b>16MB (128Mb)</b></li>
  <li>Tools &rarr; Partition Scheme &rarr; <b>16M Flash (3MB APP/9.9MB FATFS)</b></li><li>Upload</li></ol>
  <p class=mute style="margin:8px 0 0">After that, updates can be installed from here.</p>`;
  return;
 }
 $('#fwbox').innerHTML=`<p style="margin:0 0 8px">In Arduino IDE use <b>Sketch &rarr; Export Compiled Binary</b>, then pick
  <b>InkDeck.ino.bin</b> from the sketch's <b>build</b> folder (not the <i>merged</i>, <i>bootloader</i> or <i>partitions</i> file).</p>
  <div class=row><input type=file id=fwf accept=".bin"><button class=b id=fwu disabled>Upload update</button></div>
  <div class=bar id=fwbar style="display:none;height:10px;border:1px solid #000;margin-top:10px;background:#fff">
   <div style="height:100%;width:0;background:repeating-linear-gradient(45deg,#000 0 2px,#fff 2px 4px)"></div></div>
  <p id=fwst class=mute style="margin:8px 0 0">Space for firmware up to ${mb(i.ota_slot)}.</p>`;
 const f=$('#fwf'),b=$('#fwu'),st=$('#fwst'),bar=$('#fwbar'),fill=bar.firstElementChild;
 f.onchange=()=>{b.disabled=!f.files.length;
  const n=f.files[0]&&f.files[0].name||'';
  st.textContent=/merged|bootloader|partitions/i.test(n)?'That looks like the wrong file: pick InkDeck.ino.bin.':(n?n+' ('+mb(f.files[0].size)+')':'')};
 b.onclick=()=>{
  const file=f.files[0];if(!file)return;
  const fd=new FormData();fd.append('file',file,file.name);
  const x=new XMLHttpRequest();
  x.open('POST','/api/firmware');
  x.upload.onprogress=e=>{if(e.lengthComputable){bar.style.display='';fill.style.width=(e.loaded*100/e.total)+'%';st.textContent='Uploading... '+Math.round(e.loaded*100/e.total)+'%'}};
  x.onload=()=>{
   b.disabled=false;
   if(x.status==200){const r=JSON.parse(x.responseText);
    st.innerHTML='<b>'+esc(r.version)+' is on the device.</b> Confirm on InkDeck to install it (Right, then Enter). It restarts when done, so this page will stop responding for a minute.'}
   else{bar.style.display='none';st.innerHTML='<span class=err>'+esc(x.responseText||'Upload failed')+'</span>'}};
  x.onerror=()=>{b.disabled=false;st.innerHTML='<span class=err>Upload failed: lost the connection</span>'};
  b.disabled=true;x.send(fd);
 };
}

// ---------- Clock ----------
const ZONES=[['Pacific (Vancouver, Los Angeles)','PST8PDT,M3.2.0,M11.1.0'],['Mountain (Edmonton, Denver)','MST7MDT,M3.2.0,M11.1.0'],
 ['Mountain, no DST (Arizona)','MST7'],['Central (Winnipeg, Chicago)','CST6CDT,M3.2.0,M11.1.0'],['Saskatchewan','CST6'],
 ['Eastern (Toronto, New York)','EST5EDT,M3.2.0,M11.1.0'],['Atlantic (Halifax)','AST4ADT,M3.2.0,M11.1.0'],
 ['Newfoundland','NST3:30NDT,M3.2.0,M11.1.0'],['UK / Ireland','GMT0BST,M3.5.0/1,M10.5.0'],['Central Europe','CET-1CEST,M3.5.0,M10.5.0/3'],
 ['Eastern Australia (Sydney)','AEST-10AEDT,M10.1.0,M4.1.0/3'],['UTC','UTC0']];
async function timeSection(){
 const t=await j('/api/time');
 const known=ZONES.some(z=>z[1]===t.tz);
 const box=document.createElement('div');
 box.innerHTML=`<h2 style="margin-top:22px">Clock</h2>
 <p class=mute>${t.rtc?`RTC module found (${esc(t.rtc)}): the time is kept while the device is off.`:'No RTC module: the device forgets the time at power-off and gets it again from the internet over WiFi (or from this page).'}</p>
 <div class=box>
  <div id=tnow style="margin-bottom:8px"></div>
  <div class=row><select id=tz style="font:inherit;padding:4px">${ZONES.map(z=>`<option value="${esc(z[1])}"${z[1]===t.tz?' selected':''}>${esc(z[0])}</option>`).join('')}
   <option value="custom"${known?'':' selected'}>Custom...</option></select>
   <input type=text id=tzc placeholder="POSIX TZ string" value="${known?'':esc(t.tz)}" style="${known?'display:none':''}">
   <button class=b id=tzs>Save time zone</button></div>
  <div class=row style="margin-top:8px"><select id=tfmt style="font:inherit;padding:4px"><option value=0${t.h12?'':' selected'}>24-hour clock (14:05)</option><option value=1${t.h12?' selected':''}>12-hour clock (2:05 PM)</option></select></div>
  <div class=row style="margin-top:8px"><button class=b id=tset>Set time from this browser</button><span id=tst class=mute></span></div>
 </div>`;
 $('#main').appendChild(box);
 const show=x=>$('#tnow').innerHTML=x.valid?`Device time: <b>${esc(x.local)}</b> <span class=mute>(from ${esc(x.source)})</span>`:'Device time: <b>not set</b>';
 show(t);
 $('#tz').onchange=()=>{$('#tzc').style.display=$('#tz').value==='custom'?'':'none'};
 $('#tfmt').onchange=async()=>{try{show(await j('/api/time',{method:'POST',body:new URLSearchParams({h12:$('#tfmt').value})}));clockTick();$('#tst').textContent='Clock format saved'}catch(e){$('#tst').textContent='Failed: '+e.message}};
 $('#tzs').onclick=async()=>{const v=$('#tz').value==='custom'?$('#tzc').value.trim():$('#tz').value;
  try{show(await j('/api/time',{method:'POST',body:new URLSearchParams({tz:v})}));$('#tst').textContent='Time zone saved'}catch(e){$('#tst').textContent='Failed: '+e.message}};
 $('#tset').onclick=async()=>{
  try{show(await j('/api/time',{method:'POST',body:new URLSearchParams({epoch:Math.floor(Date.now()/1000)})}));$('#tst').textContent='Clock set'}catch(e){$('#tst').textContent='Failed: '+e.message}};
}

j('/api/info').then(i=>{$('#hdr').textContent=i.apps+' apps';$('#hdr2').textContent=i.sd+' \u00b7 '+i.network}).catch(()=>{});
// Device clock in the menu bar
async function clockTick(){try{const t=await j('/api/time');$('#mclock').textContent=t.valid?t.local.slice(0,3)+' '+t.clock:'--:--'}catch(e){}}
clockTick();setInterval(clockTick,30000);
loadTabs();
</script></body></html>)HTML";
