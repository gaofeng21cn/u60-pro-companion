/* Shared, side-effect-free schema handling; strings are rendered with text(). */
(function(root,factory){if(typeof define==='function'&&define.amd)define([],factory);else if(typeof module==='object'&&module.exports)module.exports=factory();else root.U60Model=factory();}(this,function(){
 'use strict';
 function interactive(item){return !!item&&item.enabled!==false&&item.type!=='info'&&(item.type==='report'||!!item.action);}
 function argumentsFor(item,choice,fields){
  var result=Object.assign({},item.args||{},choice&&choice.args||{});
  if(fields)(item.fields||[]).forEach(function(f){var v=String(fields[f.key]==null?'':fields[f.key]);if(f.required&&!v)throw new Error('请填写 '+f.label);if(f.kind==='number'&&v&&!isFinite(Number(v)))throw new Error('请输入有效数字');result[f.key]=v;});
  return result;
 }
 function readable(value){if(value===null||typeof value==='undefined'||value==='')return '—';if(typeof value==='boolean')return value?'开启':'关闭';return String(value);}
 function valueKnown(value){return value!==null&&typeof value!=='undefined'&&value!==''&&value!=='—'&&value!=='--'&&!(typeof value==='number'&&!isFinite(value));}
 function displayValue(value){return valueKnown(value)?readable(value):'未知';}
 function stateFreshness(at,options){
  options=options||{};var now=typeof options.now==='number'?options.now:Date.now(),valid=typeof at==='number'&&isFinite(at)&&at>0&&at<=now,age=valid?Math.floor((now-at)/1000):null;
  var kind=options.busy?'busy':options.reading?'reading':options.failed?'error':!valid?'unknown':age>60?'stale':'fresh';
  var label={busy:'操作进行中',reading:'正在读取',error:'读取失败',unknown:'尚未读取',stale:'状态已过期',fresh:'已更新'}[kind];
  return {kind:kind,label:label,age:age,receivedAt:valid?at:null};
 }
 function proxyStatus(clash){
  clash=clash||{};var labels={takeover:'代理已生效',partial:'转发不完整',unverified:'未核验',core_down:'核心未运行',tailscale:'Tailscale 出口',error:'异常'};
  if(labels[clash.verdict])return labels[clash.verdict];
  if(clash.online===false)return '已停止';
  if(clash.online===true)return {rule:'规则分流',global:'全局代理',direct:'直连'}[clash.mode]||'运行中 · 模式未知';
  return '未知';
 }
 function safeInternalPath(value){
  if(typeof value!=='string'||!value||value.slice(0,1)!=='/'||value.slice(0,2)==='//'||value.indexOf('\\')>=0||/[\u0000-\u001f]/.test(value))return null;
  return value;
 }
 function authExpiry(value){
  if(typeof value==='number'&&isFinite(value)&&value>0){var ms=value<1e12?value*1000:value;return new Date(ms).toISOString();}
  if(typeof value==='string'&&/^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d$/.test(value))return value;
  if(typeof value==='string'&&value){var time=Date.parse(value);if(isFinite(time))return new Date(time).toISOString();}
  return null;
 }
 function authProjection(snapshot){
  var data=snapshot&&snapshot.data||{},raw=data.second_auth||data.internet_auth||data.auth||{},available=raw.available===true||raw.api_available===true;
  function count(keys){for(var i=0;i<keys.length;i++){var value=raw[keys[i]];if(typeof value==='number'&&isFinite(value)&&value>=0)return Math.floor(value);if(Array.isArray(value))return value.length;}return null;}
  var success=count(['success_count','succ_count','authenticated_count','success_list','succ_list']),failed=count(['failure_count','fail_count','unauthorized_count','failure_list','fail_list']),expiry=authExpiry(raw.latest_expiry||raw.expires_at||raw.expire_at),serverTime=authExpiry(raw.server_time||raw.server_at||raw.server_now),needs=typeof raw.needs_auth==='boolean'?raw.needs_auth:typeof raw.required==='boolean'?raw.required:null;
  var status=typeof raw.status==='string'&&/^(ok|authorized|needs_auth|expired|unknown)$/.test(raw.status)?raw.status:null;
  return {available:available,needsAuth:needs,status:status,successCount:success,failureCount:failed,latestExpiry:expiry,serverTime:serverTime,entryUrl:safeInternalPath(raw.entry_url||raw.auth_url||raw.portal_url)};
 }
 function cellSection(snapshot){
  var data=snapshot&&snapshot.data||{},source=(snapshot&&snapshot.sections||[]).find(function(s){return s.id==='cell';}),items=(source&&source.items||[]).filter(function(i){return i.type==='info';}).map(function(i){return Object.assign({},i,{action:null,enabled:false});});
  if(!items.length)items=[{id:'operator',label:'运营商',value:data.operator,type:'info'},{id:'network',label:'网络制式',value:data.network,type:'info'},{id:'band',label:'频段',value:data.band,type:'info'}];
  items.push({id:'wifi-status',label:'热点运行状态',value:data.wifi_status,type:'info'},{id:'clients',label:'已连接终端',value:valueKnown(data.clients)?displayValue(data.clients)+' 台':'未知',type:'info'});
  return {id:'connection-readings',title:'蜂窝与热点终端',items:items};
 }
 function confirmationInfo(item,args){
  item=item||{};args=args||{};var action=item.action||'',danger=item.danger===true||action==='esim.delete'||action==='system.power'||action==='sms.delete'||action==='clash.rule_edit'&&args.operation==='delete';
  var connection=['network.profile','network.tailscale_mode','tailscale.connected','tailscale.exit','tailscale.accept_routes','tailscale.accept_dns','tailscale.allow_lan','wifi.power','wifi.ap','wifi.ssid','wifi.password','wifi.channel','usb.role','usb.macnet.mode','usb.power_role','power.standby','esim.enable','esim.disable'].indexOf(action)>=0||action.indexOf('wifi.relay.')===0&&action!=='wifi.relay.scan'&&action!=='wifi.relay.health';
  return {kind:danger?'danger':connection?'connection':'default',consequence:item.consequence||(connection?'当前网络连接可能短暂中断，请等待服务恢复后刷新核对。':''),recovery:item.recovery||''};
 }
 function bytes(v,speed){if(typeof v!=='number'||!isFinite(v)||v<0)return '—';var units=speed?['B/s','KB/s','MB/s','GB/s']:['B','KB','MB','GB','TB'],i=0;while(v>=1024&&i<units.length-1){v/=1024;i++;}return v.toFixed(v>=100||i===0?0:1)+' '+units[i];}
 function selectableNodes(group){
  if(!group)return [];
  var source=Array.isArray(group.selectable_nodes)?group.selectable_nodes:group.nodes||[];
  return source.filter(function(name){return typeof name==='string'&&!['DIRECT','REJECT','REJECT-DROP','PASS','PASS-RULE','COMPATIBLE'].includes(name);});
 }
 function defaultNodeGroup(groups,active){
  groups=groups||[];
  var current=groups.find(function(g){return g.name===active&&selectableNodes(g).length>0;});
  return current||groups.find(function(g){return selectableNodes(g).length>0;})||groups[0]||null;
 }
 // Web-only presentation: keep additions; stock controls stay in the stock pages.
 var additions={
  wifi:['relay','relay-scan','relay-health','relay-profiles','relay-fallback','relay-autostart','relay-forget'],usb:['role','status','adapter_info','macnet.mode','macnet.link','macnet.help','macnet.enabled','wiring'],
  battery:['charge.manual','charge.policy','usb.power_role','power.standby','usb.charge_state','charge.connected','charge.temp','charge.voltage','charge.current','charge.policy_status','power.standby_state'],
  band:['band.lte','band.sa','band.nsa','wan_active_band','nr5g_action_band','nr5g_pci','nr5g_action_channel'],
  esim:['card','eid','profiles','job','esim.scan','esim.enable','esim.disable','esim.nickname','esim.delete','esim.download'],
  signal:['signal.serving','signal.neighbors'],diagnostics:['diag.network','diag.web']
 };
 function filterSections(sections,tab){
  var groups={network:['wifi','usb','auth-bypass'],clash:['clash'],tailscale:['tailscale'],device:['battery'],more:['band','signal','diagnostics'],esim:['esim']};
  var filtered=(sections||[]).filter(function(s){return groups[tab]&&groups[tab].indexOf(s.id)>=0;}).map(function(s){
   var items=(s.items||[]).filter(function(i){
    if(s.id==='auth-bypass')return true;
    if(s.id==='clash')return ['clash.select','clash.delay','clash.provider','clash.rule_add','clash.rule_edit','diag.connections','clash.close_connections'].indexOf(i.action)<0;
    if(s.id==='tailscale')return i.action!=='tailscale.peer_test'&&i.id!=='self-ip'&&i.id!=='backend'&&(i.id||'').indexOf('peer_')!==0;
    if(s.id==='esim')return true;
    return (additions[s.id]||[]).indexOf(i.id)>=0;
   });
   if(additions[s.id])items.sort(function(a,b){return additions[s.id].indexOf(a.id)-additions[s.id].indexOf(b.id);});
   return Object.assign({},s,{title:s.id==='wifi'?'Wi-Fi 中继':s.id==='battery'?'充电与深待机':s.title,items:items});
  }).filter(function(s){return s.items.length>0;});
  function split(section,parts){return parts.map(function(p){return {id:p[0],title:p[1],collapsed:!!p[3],items:section.items.filter(function(i){return p[2].indexOf(i.id)>=0;})};}).filter(function(s){return s.items.length;});}
  return filtered.reduce(function(out,s){
   if(s.id==='wifi')return out.concat(split(s,[['wifi-relay','Wi-Fi 中继',['relay','relay-scan','relay-health']],['wifi-relay-policy','重连与流量策略',['relay-profiles','relay-fallback','relay-autostart','relay-forget']]]));
   if(s.id==='usb')return out.concat(split(s,[['usb-adapter','外接 USB 网卡',['role','status','adapter_info','wiring']],['usb-cable','USB 数据线直连',['macnet.mode','macnet.link','macnet.help','macnet.enabled']]]));
   if(s.id==='tailscale')return out.concat(split(s,[['tailscale','组网与访问',['connected','lan-gateway','advertise_lan']],['tailscale-exit','互联网出口',['exit','offer_exit']],['tailscale-options','高级组网设置',['mode','RouteAll','CorpDNS','ExitNodeAllowLANAccess'],true]]));
   if(s.id==='battery')return out.concat(split(s,[['power-controls','充电与待机设置',['charge.manual','charge.policy','usb.power_role','power.standby']],['power-status','电池与供电状态',['usb.charge_state','charge.connected','charge.temp','charge.voltage','charge.current','charge.policy_status','power.standby_state']]]));
   return out.concat(s);
  },[]);
 }
 return {interactive:interactive,argumentsFor:argumentsFor,readable:readable,valueKnown:valueKnown,displayValue:displayValue,stateFreshness:stateFreshness,proxyStatus:proxyStatus,authProjection:authProjection,cellSection:cellSection,confirmationInfo:confirmationInfo,bytes:bytes,selectableNodes:selectableNodes,defaultNodeGroup:defaultNodeGroup,filterSections:filterSections};
}));
