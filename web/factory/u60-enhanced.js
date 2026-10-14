define(['jquery','service_helper','config/config','u60-web-model','u60-web-advanced','u60-web-esim'],function($,helper,config,model,advanced,esim){
 'use strict';
 var cache=window.__u60EnhancedCache;
 if(!cache||cache.version!==2)cache={version:2,session:null,snapshot:null,snapshotAt:0,advanced:{},advancedAt:{}};
 window.__u60EnhancedCache=cache;
 var root,dialog,snapshot,tab='overview',busy=false,reading=false,alive=0,lastRead=0,lastInteraction=0,focusBack,advancedReading=false,advancedData=cache.advanced,advancedAt=cache.advancedAt,issues=cache.issues||(cache.issues={}),noticeText='';
 var hashTabs={"#u60_enhanced":"overview","#u60_enhanced_network":"network","#u60_enhanced_clash":"clash","#u60_enhanced_tailscale":"tailscale","#u60_enhanced_device":"device","#u60_enhanced_more":"more","#u60_enhanced_esim":"esim"};
 var pageMeta={overview:['总览','查看当前连接与服务状态，进入常用管理。'],network:['网络','Wi-Fi 上游、AUTO / WAN / LAN'],clash:['代理','Clash / Mihomo · 管理节点、订阅与分流规则'],tailscale:['组网','Tailscale · 连接远程设备，管理内网访问与出口'],device:['电源','充电上限、供电方向、待机服务'],more:['工具','诊断、版本与维护工具'],esim:['eSIM','管理卡槽中的可插拔 eUICC 配置文件']};
 function hashFor(value){return value==='overview'?'#u60_enhanced':'#u60_enhanced_'+value;}
 function tabFromHash(){return hashTabs[location.hash]||'overview';}
 function clearCache(){cache.snapshot=null;cache.snapshotAt=0;cache.advanced={};cache.advancedAt={};cache.issues={};issues=cache.issues;noticeText='';snapshot=null;lastRead=0;advancedData=cache.advanced;advancedAt=cache.advancedAt;if(present())root.find('.u60-content').empty();}
 function node(tag,cls,text){var n=$('<'+tag+'>');if(cls)n.addClass(cls);if(text!==undefined)n.text(model.readable(text));return n;}
 function present(){return root&&root[0]&&document.documentElement.contains(root[0])&&!!hashTabs[location.hash];}
 function updateFeedback(){
  if(!present())return;var keys=['state','advanced.'+tab,'action'],messages=keys.filter(function(k){return issues[k];}).map(function(k){return issues[k];}),failed=messages.length>0;
  if(noticeText&&messages.indexOf(noticeText)<0)messages.push(noticeText);
  var notice=root.find('.u60-notice'),nextNotice=messages.join('\n');if(notice.text()!==nextNotice)notice.text(nextNotice);notice.toggleClass('error',failed).attr({'role':failed?'alert':'status','aria-live':failed?'assertive':'polite'});
  var at=(tab==='clash'||tab==='tailscale')&&advancedData[tab]?advancedAt[tab]:cache.snapshotAt,state=model.stateFreshness(at,{busy:busy,reading:reading||advancedReading,failed:!!issues.state||!!issues['advanced.'+tab]}),received=state.receivedAt?new Date(state.receivedAt).toLocaleTimeString('zh-CN',{hour12:false}):null;
  var text=state.kind==='fresh'?'已同步 '+received+' · '+state.age+' 秒前':state.label+(received?' · 上次同步 '+received:'');
  root.find('.u60-sync').text(text).attr('data-state',state.kind);
  root.find('.u60-reading-state').text(state.label).attr('data-state',state.kind);
  root.find('.u60-received-at').text(received?'网页收到状态：'+received+' · '+state.age+' 秒前':'网页尚未收到状态');
 }
 function status(text,error,scope){scope=scope||'action';if(error){issues[scope]=text||'操作未确认';noticeText='';}else noticeText=text||'';updateFeedback();}
 function clearIssue(scope){delete issues[scope];updateFeedback();}
 function advancedApi(){return {items:((snapshot&&snapshot.sections||[]).find(function(s){return s.id===tab;})||{}).items||[],open:openItem,call:function(action,args){return call(action,args).then(function(result){if(action!=='state'&&!/\.state$/.test(action)&&result&&result.ok===true)clearIssue('action');return result;});},status:status,refresh:function(){lastRead=0;lastInteraction=0;advancedAt={};cache.advancedAt=advancedAt;refresh(false);}};}
 function rpc(method,args){
  if(!config.isLogin){clearCache();return $.Deferred().reject(new Error('登录已失效，请重新登录原厂后台')).promise();}
  var request=helper.createRequest('zwrt_u60_panel',method,args);request.params[3].session=request.params[0];
  return $.ajax({type:'POST',url:'/ubus/?t='+Date.now(),contentType:'application/json',dataType:'json',cache:false,timeout:12000,headers:{'Z-Mode':'0','Z-Tag':method},data:JSON.stringify([request])}).then(function(response){
   var reply=response&&response[0];
   if(!reply||reply.error||!reply.result||reply.result[0]!==0){if(reply&&(reply.error&&reply.error.code===-32002||reply.result&&reply.result[0]===6)){clearCache();throw new Error('会话无效或没有权限，请重新登录原厂后台');}throw new Error('后台请求未确认，请刷新状态后再操作');}
   var value=reply.result[1];if(!value||value.ok!==true)throw new Error(value&&value.message||'操作未成功');return value;
  },function(){throw new Error('连接中断或请求超时；请恢复连接并刷新，勿重复提交设置');});
 }
 function call(action,args){
  var deferred=$.Deferred(),promise=deferred.promise(),previous=cache.pending,generation=alive,session=cache.session;
  cache.pending=promise;
  function release(){if(cache.pending===promise)cache.pending=null;}
  promise.then(release,release);
  function start(){
   if(generation!==alive||cache.session!==session||!present()){deferred.reject(new Error('页面已切换，请在当前页面重新操作'));return;}
   var started=Date.now();
   rpc('start',{request:JSON.stringify({action:action,args:args||{}})}).then(function(start){
    function poll(){rpc('result',{id:start.id}).then(function(result){if(result.done){deferred.resolve(result.payload);return;}if(Date.now()-started>240000){deferred.reject(new Error('等待超时，请刷新并核对实际状态'));return;}setTimeout(poll,500);},deferred.reject);}
    poll();
   },deferred.reject);
  }
  if(previous)previous.then(start,start);else start();
  return promise;
 }
 function setBusy(value){busy=value;if(present()){root.find('.u60-refresh').prop('disabled',busy||reading||advancedReading);root.find('.u60-content').attr('aria-busy',String(busy));updateFeedback();}}
 function refresh(manual){
  if(!present()||busy||reading||advancedReading||dialog&&dialog[0].open)return;
  reading=true;var generation=alive;root.find('.u60-refresh').prop('disabled',true);noticeText=manual?'正在读取设备状态…':'';updateFeedback();
  call('state',{}).then(function(result){if(generation!==alive||!present())return;if(!result||!result.ok){status(result&&result.message||'状态读取失败',true,'state');return;}var first=!snapshot;snapshot=result;cache.snapshot=result;cache.snapshotAt=Date.now();lastRead=cache.snapshotAt;clearIssue('state');if(!(dialog&&dialog[0].open)&&(first||manual||Date.now()-lastInteraction>2000))render();if(first||manual)status('');},function(error){if(generation===alive)status(error.message||String(error),true,'state');}).always(function(){if(generation===alive){reading=false;if(present())root.find('.u60-refresh').prop('disabled',busy||advancedReading);updateFeedback();loadAdvanced(!!manual);}});
 }
 function closeDialog(){if(dialog&&dialog[0].open)dialog[0].close();if(dialog)dialog.empty();if(focusBack&&document.documentElement.contains(focusBack))focusBack.focus();}
 function openDialog(title){closeDialog();focusBack=document.activeElement;dialog.append(node('div','u60-dialog-head').append(node('h3','',title),node('button','u60-close','×').attr({'type':'button','aria-label':'关闭'}).on('click',closeDialog)));dialog[0].showModal();return node('div','u60-dialog-body').appendTo(dialog);}
 function showReport(report){var b=openDialog(report.title||'操作结果');(report.lines||[]).forEach(function(line){b.append(node('p','u60-report-line',line));});b.append(node('button','u60-primary','完成').on('click',closeDialog));}
 function execute(item,args){
  if(busy){status('另一个操作正在执行，请稍候',false);return;}
  closeDialog();setBusy(true);status('正在执行：'+item.label+'…');var generation=alive;
  function perform(){if(generation!==alive||!present())return;if(reading||advancedReading){setTimeout(perform,150);return;}
  call(item.action,args).then(function(result){if(generation!==alive||!present())return;if(!result||result.ok!==true){status(result&&result.message||'操作未确认，请刷新核对',true);return;}
   clearIssue('action');status(result.message||'操作已完成');if(result.picker)openItem(result.picker,true);else if(result.report)showReport(result.report);
  },function(error){if(generation===alive)status(error.message||String(error),true);}).always(function(){if(generation!==alive)return;setBusy(false);lastRead=0;lastInteraction=0;advancedAt={};cache.advancedAt=advancedAt;if(present()&&!(dialog&&dialog[0].open))setTimeout(function(){refresh(false);},600);});}
  perform();
 }
 function confirm(item,args){
  if(item.action==='esim.delete'&&args.confirm_name!==args.expected_name){status('输入的名称不一致，未删除',true);return;}
  if(item.confirm===false){execute(item,args);return;}
  var info=model.confirmationInfo(item,args),b=openDialog(item.label);b.append(node('p','','确认应用此设置？'));if(item.reason)b.append(node('p','u60-help',item.reason));
  var details=node('dl','u60-confirm-values'),chosen=(item.choices||[]).filter(function(c){return Object.keys(c.args||{}).every(function(k){return c.args[k]===args[k];});})[0];
  if(chosen)details.append(node('dt','','所选设置'),node('dd','',chosen.label));
  else if(item.fields)(item.fields||[]).forEach(function(f){if(f.kind==='password'||f.secret)return;var v=args[f.key],c=(f.choices||[]).filter(function(x){return String(x.value)===String(v);})[0];details.append(node('dt','',f.label),node('dd','',c?c.label:v));});
  else if(args.mode)details.append(node('dt','','分流模式'),node('dd','',({rule:'规则分流',global:'全局代理',direct:'直连'})[args.mode]||args.mode));else if(typeof args.enabled==='boolean')details.append(node('dt','','设置为'),node('dd','',args.enabled?'开启':'关闭'));if(details.children().length)b.append(details);
  if(info.consequence)b.append(node('p','u60-help u60-confirm-impact',info.consequence));if(info.recovery)b.append(node('p','u60-help','恢复方式：'+info.recovery));
  var cancel=node('button','u60-secondary','取消').attr({'type':'button','data-cancel':''}).on('click',closeDialog),apply=node('button','u60-primary'+(info.kind==='danger'?' u60-danger-confirm':''),'应用设置').attr('type','button').on('click',function(){execute(item,args);});b.append(node('div','u60-dialog-actions').append(cancel,apply));cancel[0].focus();}
 function openItem(item,internal){
  if(!internal&&busy){status('正在同步或执行操作，请稍候');return;}if(!model.interactive(item))return;
  if(item.type==='report'){showReport({title:item.label,lines:[item.detail||item.value]});return;}
  if(item.type==='choice'){
   var b=openDialog(item.label),list=node('div','u60-choices').appendTo(b),choices=item.choices||[];
   if(item.reason)b.prepend(node('p','u60-help',item.reason));
   if(item.description)b.prepend(node('p','u60-help',item.description));
   if(choices.length>8){var search=node('input','u60-input').attr({'type':'search','placeholder':'搜索选项','aria-label':'搜索选项'});b.prepend(search);search.on('input',function(){var q=this.value.toLowerCase();list.children().each(function(){$(this).toggle($(this).text().toLowerCase().indexOf(q)>=0);});});}
   choices.forEach(function(choice){list.append(node('button','u60-choice').append(node('span','',choice.label),choice.description?node('small','',choice.description):null).on('click',function(){confirm(item,model.argumentsFor(item,choice));}));});
   if(!choices.length)list.append(node('p','u60-help','暂无可用选项'));return;
  }
  if(item.type==='form'){
   var body=openDialog(item.label),form=node('form','u60-form').attr('autocomplete','off'),inputs={};if(item.reason)body.append(node('p','u60-help',item.reason));
   (item.fields||[]).forEach(function(f,index){var id='u60-field-'+index,field;
    if(f.kind==='choice'){field=node('select','u60-input');(f.choices||[]).forEach(function(c){field.append(node('option','',c.label).val(String(c.value)));});}
    else if(f.kind==='textarea')field=node('textarea','u60-input').attr('rows',4);
    else field=node('input','u60-input').attr('type',f.kind==='password'?'password':f.kind==='number'?'number':'text');
    field.attr({id:id,name:f.key,autocomplete:f.kind==='password'?'new-password':'off'}).prop('required',!!f.required);if(f.kind==='number')field.attr('step','any');field.val(f.value==null?'':String(f.value));inputs[f.key]=field;
    form.append(node('label','u60-field-label',f.label).attr('for',id),field);
    if(item.action==='esim.download'){var extra=esim.enhanceField(field,f,advancedApi());if(extra)form.append(extra);}
   });
   form.append(node('div','u60-dialog-actions').append(node('button','u60-secondary','取消').attr('type','button').on('click',closeDialog),node('button','u60-primary','继续').attr('type','submit')));
   form.on('submit',function(e){e.preventDefault();var values={};Object.keys(inputs).forEach(function(k){values[k]=inputs[k].val();});try{confirm(item,model.argumentsFor(item,null,values));}catch(error){body.find('.u60-form-error').remove();body.append(node('p','u60-form-error',error.message));}});body.append(form);return;
  }
  confirm(item,model.argumentsFor(item));
 }
 function renderSection(s){var card=node(s.collapsed?'details':'section','u60-card'+(s.collapsed?' u60-disclosure':'')).attr('id','u60-section-'+s.id).append(node(s.collapsed?'summary':'h3','u60-card-title',s.title));(s.items||[]).forEach(function(item){
  var enabled=model.interactive(item),row=node(enabled?'button':'div','u60-setting'+(enabled?' actionable':''));if(enabled)row.attr('type','button').on('click',function(){openItem(item);});
  row.append(node('span','u60-setting-label',item.label),node('span','u60-setting-value',model.displayValue(item.value)));if(enabled)row.append(node('span','u60-chevron','›').attr('aria-hidden','true'));else if(item.reason&&item.enabled===false){row.addClass('unavailable');row.append(node('small','u60-setting-reason',item.reason));}card.append(row);
 });return card;}
 function renderConnectionReadings(){
  var card=renderSection(model.cellSection(snapshot)).addClass('u60-wide-card'),body=node('div','u60-card-body u60-reading-meta');body.append(node('span','u60-badge u60-reading-state','正在读取'),node('p','u60-scope u60-received-at','网页尚未收到状态'),node('p','u60-scope','蜂窝信息来自原厂网络接口；设备采样时间未提供。已连接终端数量只表示接入状态，不表示已允许上网。'));
  var signal=((snapshot.sections||[]).find(function(s){return s.id==='signal';})||{}).items||[],details=signal.find(function(i){return i.id==='signal.serving';});if(details)body.append(node('button','u60-secondary','查看全部载波信号').attr('type','button').prop('disabled',!model.interactive(details)).on('click',function(){openItem(details);}));return card.append(body);
 }
 function authDate(value){if(!value)return '未知';if(/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d$/.test(value))return value;var date=new Date(value);return isNaN(date.getTime())?'未知':date.toLocaleString('zh-CN',{hour12:false});}
 function renderInternetAuth(){
  var auth=model.authProjection(snapshot),card=node('section','u60-card u60-wide-card u60-auth-card'),body=node('div','u60-card-body').appendTo(card),headline=node('div','u60-status-head'),statusLabel='接口不可用';
  if(auth.available){
   var expiry=auth.latestExpiry?Date.parse(auth.latestExpiry):NaN,clock=auth.serverTime?Date.parse(auth.serverTime):NaN;
   statusLabel=auth.needsAuth===true?'需要认证':auth.needsAuth===false?'不要求认证':auth.status==='expired'||isFinite(expiry)&&isFinite(clock)&&expiry<=clock?'已过期':auth.status==='authorized'||auth.status==='ok'?'已授权':'状态未知';
  }
  headline.append(node('strong','u60-status-title','互联网授权'),node('span','u60-badge u60-auth-status',statusLabel));body.append(headline);
  body.append(node('p','u60-help','Wi‑Fi 已连接只表示设备接入无线网络；它不等于已获得互联网访问授权。'));
  if(!auth.available){body.append(node('p','u60-scope u60-auth-unavailable','原厂认证接口不可用，未读取或显示设备标识。请打开原厂首页完成认证。'));return card;}
  var details=node('dl','u60-confirm-values');details.append(node('dt','','已认证终端记录'),node('dd','',auth.successCount==null?'未知':String(auth.successCount)),node('dt','','未通过认证记录'),node('dd','',auth.failureCount==null?'未知':String(auth.failureCount)),node('dt','','最近记录到期（原厂时间）'),node('dd','',auth.latestExpiry?authDate(auth.latestExpiry):'未知'),node('dt','','认证需求'),node('dd','',auth.needsAuth==null?'未知':auth.needsAuth?'需要认证':'不需要认证'));body.append(details);
  body.append(node('p','u60-help','此处汇总原厂终端记录，不代表当前浏览器或全部终端已获准上网。'));
  if(auth.entryUrl)body.append(node('div','u60-inline-actions').append(node('a','u60-small-button','打开原厂认证入口').attr({href:auth.entryUrl,target:'_self'})));
  else body.append(node('p','u60-scope','未提供已验证的原厂认证入口，请打开原厂首页完成认证。'));
  return card;
 }
 function render(){if(!present())return;var meta=pageMeta[tab]||pageMeta.overview;root.find('.u60-head h1').text(meta[0]);root.find('.u60-page-description').text(meta[1]);if(!snapshot)return;var openDetails={};root.find('details').each(function(){if(this.id)openDetails[this.id]=this.open;});var content=root.find('.u60-content').empty(),d=snapshot.data||{};
  if(tab==='overview'){
   var metrics=node('div','u60-metrics'),relay=d.wifi_relay||{},clash=d.clash||{};
   var verdict=clash.verdict||'',relayItem=(((snapshot.sections||[]).find(function(s){return s.id==='wifi';})||{}).items||[]).find(function(i){return i.id==='relay';}),relayValue=relayItem&&model.valueKnown(relayItem.value)?relayItem.value:relay.enabled===true?'已开启':relay.enabled===false?'未开启':'未知';
   var values=[['代理',model.proxyStatus(clash),({clash:'Clash 代理',direct:'直连',tailscale:'Tailscale 出口'})[d.network_profile]||model.displayValue(d.network_profile),'clash'],['组网',({Running:'已连接',Stopped:'已停止',NeedsLogin:'待登录',NeedsMachineAuth:'待管理端批准'})[d.tailscale_status]||model.displayValue(d.tailscale_status),'Tailscale 私网连接','tailscale'],['Wi-Fi 中继',relayValue,'连接上游 Wi-Fi','network'],['USB 连接',model.displayValue(d.usb_status),'区分数据线与外接网卡','network']];
   values.forEach(function(x){metrics.append(node('button','u60-metric').attr('type','button').append(node('span','u60-metric-label',x[0]),node('strong','',x[1]),node('span','u60-metric-detail',x[2]+' · 查看 ›')).on('click',function(){selectTab(x[3]);}));});content.append(metrics,renderConnectionReadings(),renderInternetAuth());
   if(verdict==='partial'||verdict==='unverified'||verdict==='core_down')content.append(node('p','u60-notice error','代理尚未完整生效：'+model.readable(clash.verdict_text||'请检查转发规则')));
   var shortcuts=node('div','u60-shortcuts');[['device','电源管理','充电上限、供电方向与待机策略'],['more','工具与诊断','连通测试、无线信号与高级工具'],['esim','eSIM','管理卡槽中的可插拔 eUICC 配置文件']].forEach(function(x){shortcuts.append(node('button','u60-shortcut').append(node('strong','',x[1]),node('span','',x[2]),node('b','','›')).on('click',function(){selectTab(x[0]);}));});content.append(shortcuts);
   content.append(node('p','u60-help','热点、蜂窝网络、流量套餐、短信及路由设置，请使用原厂菜单。'));

  }else{
   if(tab!=='clash'&&tab!=='esim'){var grid=node('div','u60-grid');if(tab==='network')grid.append(renderConnectionReadings(),renderInternetAuth());model.filterSections(snapshot.sections,tab).forEach(function(s){grid.append(renderSection(s));});content.append(grid);}
   if(tab==='clash'&&!advancedData.clash){var basic=(snapshot.sections||[]).find(function(s){return s.id==='clash';});if(basic)content.append(renderSection({id:'clash-basic',title:'代理开关与模式',items:basic.items.filter(function(i){return i.id==='service'||i.id==='mode';})}));}
   if(tab==='esim'){var host=node('div','u60-esim');content.append(host);esim.render(host,d.esim||{},advancedApi());}
   if(tab==='clash'||tab==='tailscale'||tab==='network'){
    if(advancedData[tab])content.append(advanced.render(tab,advancedData[tab],advancedApi()));
    else content.append(node('p','u60-help','正在读取'+(tab==='clash'?'代理':tab==='network'?'终端网络':'组网')+'状态与配置…'));
   }
  }root.find('details').each(function(){if(Object.prototype.hasOwnProperty.call(openDetails,this.id))this.open=openDetails[this.id];});updateFeedback();}
 function loadAdvanced(force){
  if(!present()||busy||reading||advancedReading||(tab!=='clash'&&tab!=='tailscale'&&tab!=='network')||dialog&&dialog[0].open)return;
  /* Connections are the one live view: refresh faster there, and keep the
   * slower cadence everywhere else so routine state reads stay cheap. */
  var ttl=advanced.intervalFor?advanced.intervalFor(tab):20000;
  if(!force&&advancedData[tab]&&(Date.now()-(advancedAt[tab]||0)<ttl||Date.now()-lastInteraction<8000))return;var kind=tab,generation=alive;advancedReading=true;root.find('.u60-refresh').prop('disabled',true);updateFeedback();
  call('web.'+kind+'.state',{}).then(function(r){if(generation!==alive||!present())return;if(!r||!r.ok){status(r&&r.message||'高级状态读取失败',true,'advanced.'+kind);return;}advancedData[kind]=r;cache.advanced[kind]=r;advancedAt[kind]=Date.now();cache.advancedAt[kind]=advancedAt[kind];clearIssue('advanced.'+kind);if(!(dialog&&dialog[0].open)&&tab===kind&&(force||Date.now()-lastInteraction>2000||!root.find('.u60-advanced').length))render();},function(e){if(generation===alive)status(e.message||String(e),true,'advanced.'+kind);}).always(function(){if(generation!==alive)return;advancedReading=false;root.find('.u60-refresh').prop('disabled',busy||reading);updateFeedback();if(tab!==kind)loadAdvanced(false);});
 }
 function pollEsim(){
  if(!present()||tab!=='esim'||busy||reading||advancedReading||dialog&&dialog[0].open)return;
  var data=snapshot&&snapshot.data&&snapshot.data.esim;
  if(!data||!data.job||data.job.state!=='running')return;
  var generation=alive;advancedReading=true;
  call('esim.state',{}).then(function(r){if(generation!==alive||!present()||!r.ok)return;snapshot.data.esim=r.esim;cache.snapshot=snapshot;render();},function(e){status(e.message,true,'action');}).always(function(){if(generation===alive){advancedReading=false;updateFeedback();}});
 }
 function selectTab(value){tab=value;var target=hashFor(value);if(location.hash!==target)location.hash=target;render();root[0].scrollIntoView({block:'start'});loadAdvanced(false);}
 function init(){alive++;busy=false;reading=false;advancedReading=false;lastInteraction=0;var session=config.isLogin?helper.createRequest('zwrt_u60_panel','start',{}).params[0]:null;if(!session||cache.session!==session)clearCache();cache.session=session;advancedData=cache.advanced;advancedAt=cache.advancedAt;snapshot=cache.snapshot||null;lastRead=cache.snapshotAt||0;tab=tabFromHash();root=$('#u60-enhanced');root.on('pointerdown keydown',function(){lastInteraction=Date.now();});dialog=root.find('dialog');dialog.on('close',function(){if(!dialog[0].open)dialog.empty();});dialog.on('click',function(e){if(e.target===dialog[0]){var r=dialog[0].getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)closeDialog();}});
  $(window).off('hashchange.u60Enhanced').on('hashchange.u60Enhanced',function(){if(!hashTabs[location.hash])return;tab=tabFromHash();render();loadAdvanced(false);});
  root.find('.u60-refresh').on('click',function(){refresh(true);});render();if(snapshot)status('');refresh(false);
  addInterval(function(){updateFeedback();pollEsim();if(present()&&!document.hidden&&Date.now()-lastInteraction>8000&&!/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)&&Date.now()-lastRead>20000)refresh(false);},5000);
 }
 return {init:init};
});
