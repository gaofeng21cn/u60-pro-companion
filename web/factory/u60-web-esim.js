/* Authenticated eSIM projection; card state comes only from the eUICC worker. */
define(['jquery','u60-web-model'],function($,model){
 'use strict';
 var picked=[],selectionEid='';
 function n(tag,cls,text){var e=$('<'+tag+'>');if(cls)e.addClass(cls);if(text!==undefined)e.text(text);return e;}
 function card(title){return n('section','u60-card u60-wide-card').append(n('h3','u60-card-title',title));}
 function mask(s){s=String(s||'');return s.length>10?s.slice(0,6)+'…'+s.slice(-4):s;}
 function name(p){return p.nickname||p.name||'未命名配置';}
 function button(label,fn,disabled,cls){return n('button',cls||'u60-small-button',label).attr('type','button').prop('disabled',!!disabled).on('click',fn);}
 function row(label,value){return n('div','u60-setting').append(n('span','u60-setting-label',label),n('span','u60-setting-value',value));}
 function message(error){var s=String(error||'');if(/catBusy|\(5\)/i.test(s))return '卡片正在忙。请等待后重新读取；持续繁忙时手动重新插卡或重启，再读取确认，勿连续重试。';var messages={'Read the card first':'请先读取卡片','The card in the SIM tray is not an eUICC':'卡槽内为普通 SIM，不能下载 eSIM 配置','This profile is already enabled':'此配置已经启用','This profile is already disabled':'此配置已经停用','Disable this profile before deleting it':'请先停用此配置再删除','Invalid activation code':'激活码格式无效','This activation code needs a confirmation code':'此激活码需要填写确认码','Clash is not running':'代理未运行，请选择直连或先开启代理','The card refused the eUICC application (status 6A82)':'该卡不提供可管理的 eUICC 应用'};return messages[s]||s;}
 function step(s){return {queued:'等待设备设置锁',reading:'读取卡片',radio_offline:'暂停蜂窝网络',sim_power:'重新读取 SIM',sim_reading:'等待 SIM 就绪',firmware:'核对原厂固件状态',es10c_enable_profile:'启用配置',es10c_disable_profile:'停用配置',es10c_delete_profile:'删除配置',es10c_set_nickname:'修改名称',es9p_initiate_authentication:'连接运营商服务器',es9p_authenticate_client:'校验下载会话',es9p_get_bound_profile_package:'下载配置文件',es10b_load_bound_profile_package:'写入 eUICC',es9p_handle_notification:'发送运营商通知'}[s]||'正在处理';}
 function item(label,action,args,reason,fields){return {label:label,action:action,type:fields?'form':'action',enabled:true,confirm:true,args:Object.assign({confirmed:true},args),reason:reason,fields:fields};}
 function decodeImage(img,jsQR){
  var width=img.naturalWidth||img.width,height=img.naturalHeight||img.height;
  if(!width||!height||width*height>16000000)throw new Error('图片尺寸过大，请裁剪二维码后再试');
  var canvas=document.createElement('canvas'),scales=[1,.5];
  for(var i=0;i<scales.length;i++){
   canvas.width=Math.max(1,Math.round(width*scales[i]));canvas.height=Math.max(1,Math.round(height*scales[i]));
   var ctx=canvas.getContext('2d');ctx.fillStyle='#fff';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.drawImage(img,0,0,canvas.width,canvas.height);
   var pixels=ctx.getImageData(0,0,canvas.width,canvas.height),qr=jsQR(pixels.data,pixels.width,pixels.height);
   if(qr&&/^LPA:/i.test(qr.data))return qr.data;
  }
  throw new Error('图片中未找到 eSIM LPA 激活码');
 }
 function readQrFile(file,onCode,onError){
  if(file.size>10*1024*1024){onError('二维码图片超过 10 MB');return;}
  require(['jsQR'],function(jsQR){var url=URL.createObjectURL(file),img=new Image();
   img.onload=function(){try{onCode(decodeImage(img,jsQR));}catch(e){onError(e.message);}finally{URL.revokeObjectURL(url);}};
   img.onerror=function(){URL.revokeObjectURL(url);onError('无法读取二维码图片');};img.src=url;
  },function(){onError('二维码组件无法加载，请输入激活码');});
 }
 function pasteImage(event,onCode,onError){
  var clipboard=(event.originalEvent||event).clipboardData,items=clipboard&&clipboard.items;
  if(!items)return;for(var i=0;i<items.length;i++)if(/^image\//.test(items[i].type)){var file=items[i].getAsFile();if(file){event.preventDefault();readQrFile(file,onCode,onError);return;}}
 }
 function enhanceField(field,spec,api){
  function present(){return document.documentElement.contains(field[0]);}
  if(spec.key==='activation_code')field.attr('placeholder','可输入激活码，也可粘贴二维码截图').on('paste',function(e){pasteImage(e,function(code){if(present()){field.val(code);api.status('已读取二维码，请继续确认下载参数');}},function(error){if(present())api.status(error,true);});});
  if(spec.key==='imei')return button('使用本机 IMEI',function(){var control=$(this);control.prop('disabled',true);api.call('esim.imei',{}).then(function(r){if(!present())return;if(r&&r.ok&&/^\d{15,16}$/.test(r.imei)){field.val(r.imei);api.status('已读取本机 IMEI');}else api.status(r&&r.message||'本机 IMEI 无法读取，可手动填写',true);},function(e){if(present())api.status(e.message||'本机 IMEI 无法读取',true);}).always(function(){control.prop('disabled',false);});});
 }
 function render(host,data,api){
  host.empty().addClass('u60-advanced');var view=data.view||{},job=data.job||{},running=job.state==='running',ready=data.installed&&view.card==='euicc'&&!running,bind={expected_eid:view.eid};
  if(selectionEid!==view.eid){picked=[];selectionEid=view.eid;}
  var box=card('eSIM 概况'),body=n('div','u60-card-body').appendTo(box);
  var scan=item('读取卡片','esim.scan',{},'只读操作');scan.confirm=false;delete scan.args.confirmed;
  var label={euicc:'可管理 eUICC',sim:'普通 SIM',absent:'卡槽为空',error:'读取失败'}[view.card]||'尚未读取卡片';
  body.append(n('div','u60-status-head').append(n('strong','u60-status-title',label),button(running?'正在操作…':'读取卡片',function(){api.open(scan);},running||!data.installed)));
  body.append(n('p','u60-scope','SIM 卡槽插入 9eSIM、5ber、eSTK.me 等可插拔 eUICC；普通 SIM 不能下载配置。'));
  if(!data.installed)body.append(n('p','u60-help','eSIM 程序尚未安装，请使用含 eSIM 的安装包升级。'));
  if(view.eid||view.read_at){var details=n('details','u60-disclosure').attr('id','u60-esim-card').append(n('summary','','卡片详情'));
   if(view.eid)details.append(row('EID',view.eid));
   var info=view.info||{};[['sgp22','SGP.22 版本'],['firmware','eUICC 固件'],['profile_version','配置文件版本'],['default_smdp','默认 SM-DP+'],['root_smds','根 SM-DS'],['sas','SAS 认证编号']].forEach(function(f){if(info[f[0]]!=null&&info[f[0]]!=='')details.append(row(f[1],String(info[f[0]])));});
   if(info.free_nvm!=null)details.append(row('卡片剩余空间',model.bytes(Number(info.free_nvm),false)));
   if(info.installed_apps!=null)details.append(row('已安装应用数',String(info.installed_apps)));
   if(Array.isArray(info.ci_pkids)&&info.ci_pkids.length)details.append(row('CI 公钥标识',info.ci_pkids.join('、')));
   if(view.firmware)details.append(row('原厂当前 ICCID',view.firmware.iccid||'尚未识别'),row('原厂 SIM 状态',view.firmware.state||'未知'));
   if(view.read_at)details.append(row('最近读卡',new Date(view.read_at*1000).toLocaleString('zh-CN',{hour12:false})));body.append(details);
  }
  if(view.detail)body.append(n('p','u60-warning',message(view.detail)));
  if(running)body.append(n('p','u60-help',step(job.step)+'；请等待结果，勿重复提交。'));
  else if(job.error)body.append(n('p','u60-warning',message(job.error)));
  else if(job.state==='interrupted')body.append(n('p','u60-warning','上次操作中断，卡上结果未知；请重新读取后核对，勿直接重复下载。'));
  else if(job.state==='done')body.append(n('p','u60-help','最近操作已完成，以下为卡片回读。'));
  if(job.result&&job.result.notifications&&job.result.notifications.error)body.append(n('p','u60-warning','配置操作已完成，运营商通知未发送：'+message(job.result.notifications.error)));
  if(data.reboot_needed)body.append(n('p','u60-warning','卡片已切换，但原厂固件尚未识别新配置；请手动重新插卡或重启后再读取。'));
  else if(view.card==='euicc'&&view.firmware&&view.firmware.iccid){var active=(view.profiles||[]).find(function(p){return p.enabled;});if(active&&String(active.iccid).replace(/f+$/i,'')!==String(view.firmware.iccid).replace(/f+$/i,''))body.append(n('p','u60-warning','原厂 ICCID 与卡上启用配置不一致；蜂窝无法联网时，请手动重新插卡或重启后再读取。'));}
  host.append(box);
  if(view.card==='euicc'){
   box=card('配置文件');body=n('div','u60-card-body').appendTo(box);
   (view.profiles||[]).forEach(function(p){var row=n('div','u60-provider'),args=Object.assign({iccid:p.iccid},bind),actions=n('div','u60-inline-actions');
    row.append(n('div','u60-provider-heading').append(n('strong','',name(p)),n('span','u60-badge'+(p.enabled?' online':''),p.enabled?'正在使用':'已停用')),n('p','u60-help',(p.provider||'运营商未知')+' · ICCID '+mask(p.iccid)));
    var profileDetails=n('details','u60-disclosure').append(n('summary','','配置详情'),rowInfo('ICCID',p.iccid));
    if(p.name)profileDetails.append(rowInfo('原始名称',p.name));if(p.class)profileDetails.append(rowInfo('配置类别',({operational:'运营配置',test:'测试配置',provisioning:'初始化配置'}[p.class]||p.class)));if(p.aid)profileDetails.append(rowInfo('应用 AID',p.aid));row.append(profileDetails);
    actions.append(button(p.enabled?'停用':'启用',function(){api.open(item((p.enabled?'停用':'启用')+' '+name(p),'esim.'+(p.enabled?'disable':'enable'),args,p.enabled?'停用后蜂窝服务将断开，直到启用另一配置。':'切换会短暂中断蜂窝网络。'));},!ready));
    actions.append(button('改名',function(){api.open(item('修改 '+name(p)+' 的名称','esim.nickname',args,'最多 64 字节。',[{key:'nickname',label:'新名称',kind:'text',value:p.nickname||'',required:false}]));},!ready));
    actions.append(button('删除',function(){api.open(item('删除 '+name(p),'esim.delete',Object.assign({expected_name:name(p)},args),'此操作不可撤销；再次安装可能需要新的运营商激活码。请输入当前名称确认。',[{key:'confirm_name',label:'输入当前名称：'+name(p),kind:'text',required:true}]));},!ready||p.enabled,'u60-small-button u60-danger'));row.append(actions);body.append(row);
   });
   if(!(view.profiles||[]).length)body.append(n('p','u60-help','卡中没有配置文件，可以下载新配置。'));host.append(box);
   box=card('下载 eSIM 配置');body=n('div','u60-card-body').appendTo(box);
   body.append(n('p','u60-help','使用运营商提供的 LPA 激活码或二维码图片。下载完成后保持停用，再由你选择启用。激活码通常只能使用一次；失败后先读取卡片核对。'));
   function download(code,manual){var fields=manual?[{key:'smdp',label:'SM-DP+ 地址',kind:'text',required:true},{key:'matching_id',label:'Matching ID',kind:'password',required:false}]:[{key:'activation_code',label:'LPA 激活码',kind:'password',value:code||'',required:true}];
    fields=fields.concat([{key:'confirmation_code',label:'确认码（如运营商要求）',kind:'password'},{key:'imei',label:'可选 IMEI',kind:'text'},{key:'route',label:'下载网络',kind:'choice',value:'auto',choices:[{label:'自动（代理运行时使用代理）',value:'auto'},{label:'直连',value:'direct'},{label:'代理',value:'proxy'}]}]);
    api.open(item('下载 eSIM 配置','esim.download',bind,'将配置写入当前 eUICC；不会自动启用。',fields));
   }
   var file=n('input','').attr({type:'file',accept:'image/*','aria-label':'选择 eSIM 二维码图片'}).prop({hidden:true,disabled:!ready}).hide();
   file.on('change',function(){var f=this.files&&this.files[0];if(!f)return;this.value='';readQrFile(f,function(code){if(document.documentElement.contains(file[0]))download(code,false);},function(error){if(document.documentElement.contains(file[0]))api.status(error,true);});});
   body.append(n('div','u60-inline-actions').append(button('输入激活码',function(){download('',false);},!ready,'u60-primary'),button('读取二维码图片',function(){file[0].click();},!ready),button('手填服务器信息',function(){download('',true);},!ready)),file,n('p','u60-scope','二维码仅在当前浏览器内识别，图片不会上传；也可在激活码输入框中粘贴截图。'));host.append(box);
   box=card('运营商通知');body=n('div','u60-card-body').appendTo(box);
   var notifications=view.notifications||[];picked=picked.filter(function(seq){return notifications.some(function(note){return note.seq===seq;});});
   function notify(operation,seq){api.open(item(operation==='process'?'发送运营商通知':'移除运营商通知','esim.notifications',Object.assign({operation:operation,seq:seq,route:'auto'},bind),operation==='process'?'通知可能需要联网。':'移除后无法再向运营商报告此通知。'));}
   var batchLocked=!ready||!picked.length||picked.length>64,send=button('发送所选',function(){notify('process',picked.slice());},batchLocked),remove=button('移除所选',function(){notify('remove',picked.slice());},batchLocked,'u60-small-button u60-danger');
   if(notifications.length)body.append(n('p','u60-help','下载与删除后自动尝试发送通知；切换产生的通知可在此选择发送或移除。每次最多处理 64 条。'),n('div','u60-inline-actions').append(send,remove));
   notifications.forEach(function(note){var row=n('div','u60-provider'),checkbox=n('input','').attr('type','checkbox').prop({checked:picked.indexOf(note.seq)>=0,disabled:!ready});
    checkbox.on('change',function(){picked=picked.filter(function(seq){return seq!==note.seq;});if(this.checked)picked.push(note.seq);var locked=!ready||!picked.length||picked.length>64;send.prop('disabled',locked);remove.prop('disabled',locked);});
    row.append(n('label','u60-help').append(checkbox,document.createTextNode(' 通知 '+note.seq+' · '+({install:'安装',enable:'启用',disable:'停用',delete:'删除'}[note.operation]||note.operation))),n('details','u60-disclosure').append(n('summary','','通知详情'),rowInfo('SM-DP+',note.address),rowInfo('ICCID',note.iccid)),n('div','u60-inline-actions').append(button('发送通知',function(){notify('process',[note.seq]);},!ready),button('移除通知',function(){notify('remove',[note.seq]);},!ready,'u60-small-button u60-danger')));body.append(row);
   });
   if(!(view.notifications||[]).length)body.append(n('p','u60-help','没有待处理通知。'));host.append(box);
  }
 }
 function rowInfo(label,value){return row(label,value||'—');}
 return {render:render,message:message,enhanceField:enhanceField};
});
