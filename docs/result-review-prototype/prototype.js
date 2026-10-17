// Deliberate UI fixtures. No audio, ASR, rewrite, target discovery or OS input.
const plain = '请把 HyprVoice 的识别结果保留下来，先做一版原型。确认以后再实现，今天不要重启服务。';
const prefix = '请把 ';
const suffix = ' 的识别结果保留下来，先做一版原型。确认以后再实现，今天不要重启服务。';
const interrupted = '请把 HyprVoice 的识别结果保留下来，先做一版原型。确认以后再实现';
const key = 'hyprvoice-review-prototype-v1';
let saved = {};
try { saved = JSON.parse(localStorage.getItem(key) || '{}'); } catch {}
const states = [
 {id:'view',title:'默认查看',subtitle:'一份正文，三个常用动作',text:plain},
 {id:'candidates',title:'候选选择',subtitle:'只展开不同片段',text:plain,candidate:0},
 {id:'edit',title:'整段编辑',subtitle:'自由修改，完成后再确认',text:plain,editing:true,draft:plain},
 {id:'error',title:'录音异常',subtitle:'尽力收尾，保留结果',text:interrupted,errorKind:'device',recovery:'done'}
].map(s => ({revision:1,target:true,...s,...saved[s.id],notice:''}));
function persist(){const data={}; for(const s of states){const {notice,...rest}=s;data[s.id]=rest;}try{localStorage.setItem(key,JSON.stringify(data));}catch{}}
function escapeHtml(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function button(action,label,cls='',disabled=false){return `<button data-action="${action}" class="${cls}" ${disabled?'disabled':''}>${label}</button>`;}
function render(){document.querySelector('#board').innerHTML=states.map((s,i)=>{
 const error=s.id==='error';const editing=s.editing;const waiting=error&&s.recovery!=='done';const empty=!s.text;
 const title=editing?'编辑正文':waiting?'正在保留结果':error?'录音中断 · 请检查':'检查后输入';
 let body=escapeHtml(s.text);
 if(s.id==='candidates'&&!s.manual){const a=s.candidate===0?['HyprVoice','Hyper Voice']:['Hyper Voice','HyprVoice']; const sources=s.candidate===0?['精修识别','实时识别']:['实时识别','精修识别'];body=escapeHtml(prefix)+`<span class="candidate" role="group" aria-label="识别差异"><span class="choice current">${a[0]}<small>采用 · ${sources[0]}</small></span><button class="choice alternative" data-action="candidate" aria-label="采用备选 ${a[1]}">${a[1]}<small>${sources[1]}<br>点击采用</small></button></span>`+escapeHtml(suffix);}
 if(empty)body='<span class="voice-empty">未恢复到可用文字。</span>';
 const text=editing?`<textarea class="editor" aria-label="${s.title}的正文" spellcheck="false">${escapeHtml(s.draft??s.text)}</textarea><p class="draft-note">草稿已在本机保存 · 完成编辑后再确认输入</p>`:`<div class="text-body" aria-label="${s.title}的当前正文">${body}</div>${s.manual?'<div class="badge">已手动编辑</div>':''}${s.paused?button('edit','继续编辑草稿'):''}`;
 let warning='';
 if(error){const message=s.errorKind==='backlog'?'音频处理积压，部分声音可能未被记录。':s.errorKind==='finish'?'收尾识别失败，已退回最后可用预览。':s.errorKind==='no-preview'?'该后端没有实时预览，收尾识别也未成功。':'麦克风连接中断，已保留可识别的内容。'; warning=`<aside class="voice-warning"><strong>${waiting?(s.recovery==='stopped'?'已停止采集，准备处理已收到的音频。':'正在处理已收到的有效音频…'):message}</strong><p>${waiting?'预览文字保留在上方；本次不会自动输入。':empty?'没有可回退的文字，请重新录音。':'结果可能不完整，请检查后再确认输入。'}</p></aside>`;}
 const actions=editing?`${button('pause','稍后继续')}<span class="spacer"></span>${button('save','完成编辑','primary',!s.draft?.trim())}`:`${button('copy','复制','',empty||waiting)}${button('edit','编辑','',empty||waiting)}${button('voice','语音修改','',empty||waiting)}<span class="spacer"></span>${button('insert','确认输入','primary',empty||waiting||!s.target)}`;
 const targetControl=`<div class="demo-controls"><span>原型交互</span>${button('target',s.target?'模拟目标变化':'恢复原目标')}</div>`;
 const controls=error?`<div class="demo-controls"><select aria-label="异常演示类型"><option value="device" ${s.errorKind==='device'?'selected':''}>设备中断 · 收尾成功</option><option value="backlog" ${s.errorKind==='backlog'?'selected':''}>队列积压 · 保留有效音频结果</option><option value="finish" ${s.errorKind==='finish'?'selected':''}>收尾失败 · 回退最后预览</option><option value="no-preview" ${s.errorKind==='no-preview'?'selected':''}>无预览 + 收尾失败</option></select>${button('recover',waiting?'继续收尾演示':'查看收尾过程')}</div>`:'';
 return `<section class="specimen" data-id="${s.id}"><div class="specimen-heading"><span class="index">0${i+1}</span><h2>${s.title}</h2><p>${s.subtitle}</p></div><article class="voice-panel">${s.closed?`<div class="closed"><p>已关闭浮层演示，文字和草稿仍保留。</p>${button('reopen','重新打开')}</div>`:`<header class="panel-header"><img class="mic" src="assets/microphone.svg" alt=""><div class="identity"><p class="voice-heading">语音输入</p><p class="voice-status">${title}</p></div>${button('close','关闭','close')}</header>${text}${s.id==='candidates'&&!s.manual?'<p class="candidate-hint">仅展示两次音频识别的差异 · 点击下方候选切换</p>':''}${warning}<div class="divider"></div><div class="target"><span>${s.target?'输入到：文本编辑器 · 迭代笔记':'请回到录音时的输入窗口'}</span></div><div class="actions">${actions}</div>${s.notice?`<p class="notice" role="status">${escapeHtml(s.notice)}</p>`:''}`}</article>${controls}${!error?targetControl:''}<p class="description">${i===0?'检查期间浮层可以获得焦点；输入目的地仍绑定录音时的窗口。':i===1?'共用文字只出现一次。上下两项均有来源，不显示猜测的置信度。':i===2?'暂离或刷新后可继续草稿；候选与后台结果不能覆盖手动修改。':'无文字对齐依据时只显示整体提示，不把故障时间标成错字位置。'}</p></section>`;
 }).join('');}
document.querySelector('#board').addEventListener('input',e=>{if(!e.target.matches('textarea'))return;const s=states.find(s=>s.id===e.target.closest('[data-id]').dataset.id);s.draft=e.target.value;persist();const save=e.target.closest('article').querySelector('[data-action="save"]');if(save)save.disabled=!s.draft.trim();});
document.querySelector('#board').addEventListener('change',e=>{if(!e.target.matches('select'))return;const s=states.find(s=>s.id==='error');s.errorKind=e.target.value;s.recovery='done';s.text=s.errorKind==='no-preview'?'':interrupted;s.manual=false;s.paused=false;s.editing=false;s.draft=undefined;s.notice='';persist();render();});
document.querySelector('#board').addEventListener('click',async e=>{
 const b=e.target.closest('[data-action]');if(!b)return;const s=states.find(s=>s.id===b.closest('[data-id]').dataset.id);const action=b.dataset.action;s.notice='';
 if(action==='candidate'){s.candidate=1-s.candidate;s.text=prefix+(s.candidate===0?'HyprVoice':'Hyper Voice')+suffix;s.revision++;}
 if(action==='edit'){s.editing=true;s.draft=s.draft??s.text;s.paused=false;}
 if(action==='pause'){s.editing=false;s.paused=true;}
 if(action==='save'&&s.draft.trim()){s.text=s.draft;s.manual=true;s.editing=false;s.paused=false;s.revision++;s.notice='修改已保留，请检查后再确认输入。';}
 if(action==='target'){s.target=!s.target;s.notice=s.target?'已回到原目标窗口。':'目标窗口已变化，文字仍保留；复制仍可用。';}
 if(action==='voice'){s.notice=s.paused?'请先完成草稿，语音修改将使用保存后的正文。':'语音修改接口预留：F9 将修改这份当前正文，原型不调用模型。';}
 if(action==='close')s.closed=true;if(action==='reopen')s.closed=false;
 if(action==='recover'){s.recovery=s.recovery==='done'?'stopped':s.recovery==='stopped'?'finishing':'done';if(s.errorKind==='no-preview')s.text='';else if(!s.manual)s.text=interrupted;}
 if(action==='insert'||action==='copy'){
   document.querySelector('#output-text').textContent=s.text;
   document.querySelector('#output-status').textContent=(action==='insert'?'确认输入演示':'复制内容')+' · 仅正文 · '+s.title;
   if(action==='copy'){try{await navigator.clipboard.writeText(s.text);s.notice='已复制正文。';}catch{s.notice='复制被浏览器阻止；正文已留在下方输出检查区，可手动复制。';}}
   else s.notice='已在下方展示待输入正文（演示，未向真实窗口输入）。';
 }
 persist();render();
});
document.querySelector('#reset').addEventListener('click',()=>{try{localStorage.removeItem(key);}catch{}location.reload();});
render();
