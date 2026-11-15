#ifndef PANEL_ESIM_CONTROL_H
#define PANEL_ESIM_CONTROL_H
/* The card owns profiles. This bridge projects the one-shot worker's readback
 * and submits allowlisted commands through stdin, using the stock login. */
static cJSON *esim_status_read(void) {
 if(fixture){cJSON*v=jget(fixture,"esim.status");return v?cJSON_Duplicate(v,1):cJSON_Parse("{\"installed\":false}");}
 char out[262144];char *av[]={"esim-control.sh","status",NULL};
 int ok=run_cmd("/data/u60-panel/esim-control.sh",av,NULL,out,sizeof(out));
 cJSON*r=ok?cJSON_Parse(out):NULL;
 if(!cJSON_IsObject(r)){cJSON_Delete(r);return cJSON_Parse("{\"installed\":false,\"view\":null,\"job\":null}");}
 return r;
}
static int esim_iccid(const char*s){size_t n=strlen(s);if(n<18||n>22)return 0;for(;*s;s++)if(!strchr("0123456789ABCDEFabcdef",*s))return 0;return 1;}
static void esim_copy_string(cJSON*body,const cJSON*args,const char*key){cJSON*v=jget(args,key);if(cJSON_IsString(v)&&*v->valuestring)cJSON_AddStringToObject(body,key,v->valuestring);}
static cJSON *esim_action(const char*action,const cJSON*args) {
 if(!strcmp(action,"esim.state")){cJSON*r=reply(1,"eSIM 状态已读取");cJSON_AddItemToObject(r,"esim",esim_status_read());return r;}
 if(!strcmp(action,"esim.imei")){cJSON*data=ubus_read("zwrt_zte_mdm.api","get_imei");const char*imei=jstr(data,"imei");size_t len=strlen(imei);int valid=firmware_success(data)&&(len==15||len==16);for(size_t i=0;i<len&&valid;i++)if(imei[i]<'0'||imei[i]>'9')valid=0;cJSON*r=reply(valid,valid?"已读取本机 IMEI":"本机 IMEI 无法读取，可手动填写");if(valid)cJSON_AddStringToObject(r,"imei",imei);cJSON_Delete(data);return r;}
 const char*kind=NULL;
 const char*allowed[]={"scan","enable","disable","nickname","download","notifications","delete"};
 for(size_t n=0;n<sizeof(allowed)/sizeof(*allowed);n++)if(!strcmp(action+5,allowed[n]))kind=allowed[n];
 if(!kind)return reply(0,"不支持的 eSIM 操作");
 if(strcmp(kind,"scan")&&!cJSON_IsTrue(jget(args,"confirmed")))return reply(0,"修改 eSIM 配置必须确认");
 if((!strcmp(kind,"enable")||!strcmp(kind,"disable")||!strcmp(kind,"nickname")||!strcmp(kind,"delete"))&&!esim_iccid(jstr(args,"iccid")))return reply(0,"ICCID 参数无效");
 cJSON*body=cJSON_CreateObject();cJSON_AddStringToObject(body,"action",kind);
 const char*fields[]={"expected_eid","iccid","nickname","activation_code","smdp","matching_id","confirmation_code","imei","route","operation","confirm_name"};
 for(size_t n=0;n<sizeof(fields)/sizeof(*fields);n++)esim_copy_string(body,args,fields[n]);
 if(!strcmp(kind,"nickname")&&cJSON_IsString(jget(args,"nickname"))&&!*jstr(args,"nickname"))cJSON_AddStringToObject(body,"nickname","");
 if(!strcmp(kind,"notifications")){cJSON*v=jget(args,"seq");if(cJSON_IsArray(v))cJSON_AddItemToObject(body,"seq",cJSON_Duplicate(v,1));}
 if(!fixture&&(!strcmp(kind,"download")||!strcmp(kind,"notifications")||!strcmp(kind,"delete"))){char proxy[160];cJSON*cfg=cc_get("/configs");if(diag_proxy_url(cfg,proxy,sizeof(proxy)))cJSON_AddStringToObject(body,"proxy_url",proxy);cJSON_Delete(cfg);}
 if(fixture){fixture_write_count++;cJSON_Delete(body);return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"测试：eSIM 请求已提交");}
 char*input=cJSON_PrintUnformatted(body);cJSON_Delete(body);char out[16384];char*av[]={"esim-control.sh","run",NULL};
 int ok=run_cmd("/data/u60-panel/esim-control.sh",av,input,out,sizeof(out));memset(input,0,strlen(input));free(input);
 cJSON*r=cJSON_Parse(out);if(cJSON_IsBool(jget(r,"ok"))&&(!cJSON_IsTrue(jget(r,"ok"))||ok))return r;
 cJSON_Delete(r);return reply(0,"eSIM 操作未提交，请刷新检查卡片状态");
}
static const char*esim_card_label(const cJSON*v){const char*c=jstr(v,"card");return !strcmp(c,"euicc")?"可管理 eUICC":!strcmp(c,"sim")?"普通 SIM（不能写入）":!strcmp(c,"absent")?"卡槽为空":!strcmp(c,"error")?"读取失败":"尚未读取卡片";}
static const char*esim_name(const cJSON*p){return *jstr(p,"nickname")?jstr(p,"nickname"):*jstr(p,"name")?jstr(p,"name"):"未命名配置";}
static const char*esim_tail(const char*s){size_t n=strlen(s);return n>6?s+n-6:s;}
static void esim_bind(cJSON*i,const cJSON*view,const cJSON*p){
 cJSON*a=jget(i,"args");cJSON_AddBoolToObject(a,"confirmed",1);cJSON_AddStringToObject(a,"expected_eid",jstr(view,"eid"));
 if(p)cJSON_AddStringToObject(a,"iccid",jstr(p,"iccid"));
}
static cJSON*esim_child(cJSON*root,cJSON*parent,const char*id,const char*title,const char*label,const char*value,int enabled){
 cJSON*s=section(root,id,title);cJSON_AddStringToObject(s,"parent",jstr(parent,"id"));
 cJSON*i=item(parent,id,label,"navigation",value,NULL,enabled,NULL);cJSON_AddStringToObject(i,"section",id);return s;
}
static cJSON*esim_report(cJSON*s,const char*id,const char*label,const char*value,const char*detail){
 cJSON*i=item(s,id,label,"report",value,NULL,1,NULL);cJSON_AddStringToObject(i,"detail",detail);return i;
}
static cJSON*esim_field(cJSON*i,const char*key,const char*label,const char*value,const char*kind,int required,int max){
 field(i,key,label,value,kind,required);cJSON*a=jget(i,"fields"),*f=cJSON_GetArrayItem(a,cJSON_GetArraySize(a)-1);cJSON_AddNumberToObject(f,"maxLength",max);return f;
}
static void esim_field_choice(cJSON*f,const char*label,const char*value){
 cJSON*a=jget(f,"choices");if(!a)a=cJSON_AddArrayToObject(f,"choices");cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddStringToObject(c,"value",value);cJSON_AddItemToArray(a,c);
}
static void esim_route_field(cJSON*i){
 cJSON*f=esim_field(i,"route","网络","auto","choice",1,6);
 esim_field_choice(f,"自动","auto");esim_field_choice(f,"直连","direct");esim_field_choice(f,"代理","proxy");
}
static void esim_download_menu(cJSON*root,cJSON*s,const cJSON*view,int ready){
 cJSON*d=esim_child(root,s,"esim-download","下载 eSIM","下载新 eSIM","激活码或服务器信息",ready),*i;
 const char*reason="将新配置写入当前卡片，保持停用。激活码通常只能用一次；失败后先读卡核对。自动网络会在代理运行时使用代理。";
 i=item(d,"activation","输入激活码","form","LPA:1$…","esim.download",ready,reason);esim_bind(i,view,NULL);
 esim_field(i,"activation_code","LPA 激活码","","password",1,767);
 esim_field(i,"confirmation_code","确认码","","password",0,64);esim_field(i,"imei","IMEI","","number",0,16);esim_route_field(i);
 i=item(d,"server","输入服务器信息","form","SM-DP+ 与 Matching ID","esim.download",ready,reason);esim_bind(i,view,NULL);
 esim_field(i,"smdp","SM-DP+ 地址","","text",1,255);esim_field(i,"matching_id","Matching ID（可选）","","password",0,255);
 esim_field(i,"confirmation_code","确认码","","password",0,64);esim_field(i,"imei","IMEI","","number",0,16);esim_route_field(i);
 esim_report(d,"help","二维码与输入帮助","手机或电脑网页输入",
  "小屏键盘支持英文、数字与符号，LPA 激活码中的 $ 在符号页。\n\n二维码图片、粘贴长激活码、读取本机 IMEI：连接 U60 热点后登录原厂管理页 → 增强功能 → eSIM。\n\n下载后保持停用。失败或中断先读取卡片核对，不自动重试。");
}
static const char*esim_notification_label(const char*op){return !strcmp(op,"install")?"安装":!strcmp(op,"enable")?"启用":!strcmp(op,"disable")?"停用":!strcmp(op,"delete")?"删除":"其他";}
static void esim_notification_choice(cJSON*i,const char*label,const cJSON*seq){
 cJSON*a=jget(i,"choices");if(!a)a=cJSON_AddArrayToObject(i,"choices");cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON*args=cJSON_AddObjectToObject(c,"args");cJSON_AddItemToObject(args,"seq",cJSON_Duplicate(seq,1));cJSON_AddItemToArray(a,c);
}
static void esim_notifications_menu(cJSON*root,cJSON*s,const cJSON*view,int ready){
 cJSON*notes=jget(view,"notifications");int count=cJSON_GetArraySize(notes);char summary[64];snprintf(summary,sizeof(summary),"%d 条待处理",count);
 cJSON*n=esim_child(root,s,"esim-notifications","运营商通知","运营商通知",summary,1);char detail[16384]="下载与删除会自动尝试发送新通知；正在发送表示进行中，处理结果与配置操作结果分别查看。失败后先重新读取待处理通知。\n\n迁移前先向运营商确认重新安装规则；删除、通知处理完成、新设备安装成功是不同步骤。仅移除通知不会告知运营商。\n";
 for(int op=0;op<2;op++){
  cJSON*i=item(n,op?"remove":"send",op?"移除通知":"发送通知","choice","选择一条或全部","esim.notifications",ready&&count>0,op?"移除后无法再向运营商报告此通知。":"通知将发送到卡片记录的运营商服务器；失败保留，勿重复提交。");esim_bind(i,view,NULL);
  cJSON_AddStringToObject(jget(i,"args"),"operation",op?"remove":"process");cJSON_AddStringToObject(jget(i,"args"),"route","auto");
  cJSON*all=cJSON_CreateArray(),*note;cJSON_ArrayForEach(note,notes){cJSON*seq=cJSON_CreateArray();cJSON_AddItemToArray(seq,cJSON_Duplicate(jget(note,"seq"),1));char label[128];snprintf(label,sizeof(label),"通知 %.0f · %s · %s",jget(note,"seq")?jget(note,"seq")->valuedouble:0,esim_notification_label(jstr(note,"operation")),esim_tail(jstr(note,"iccid")));esim_notification_choice(i,label,seq);cJSON_Delete(seq);cJSON_AddItemToArray(all,cJSON_Duplicate(jget(note,"seq"),1));}
  if(count>1&&count<=64){snprintf(summary,sizeof(summary),"全部 %d 条通知",count);esim_notification_choice(i,summary,all);}cJSON_Delete(all);
 }
 cJSON*note;cJSON_ArrayForEach(note,notes){size_t used=strlen(detail);if(used+1024>=sizeof(detail))break;snprintf(detail+used,sizeof(detail)-used,"\n通知 %.0f · %s\nICCID：%s\n服务器：%s\n",jget(note,"seq")?jget(note,"seq")->valuedouble:0,esim_notification_label(jstr(note,"operation")),jstr(note,"iccid"),jstr(note,"address"));}
 esim_report(n,"details","通知详情",count?"编号、服务器与 ICCID":"没有待处理通知",detail);
}
static void esim_sections(cJSON*root){
 cJSON*s=section(root,"esim","eSIM"),*status=esim_status_read(),*view=jget(status,"view"),*job=jget(status,"job");
 int running=!strcmp(jstr(job,"state"),"running"),interrupted=!strcmp(jstr(job,"state"),"interrupted"),installed=cJSON_IsTrue(jget(status,"installed"));
 int euicc=!strcmp(jstr(view,"card"),"euicc"),ready=installed&&!running&&!interrupted&&euicc&&strlen(jstr(view,"eid"))==32;
 cJSON_AddItemToObject(jget(root,"data"),"esim",cJSON_Duplicate(status,1));
 cJSON*i=item(s,"esim.scan","读取卡片与配置","action",running?"正在操作，请等待":"刷新卡片信息","esim.scan",installed&&!running,installed?"只读探测，不修改套餐":"eSIM 程序尚未安装");cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 if(euicc)esim_download_menu(root,s,view,ready);
 cJSON*p;cJSON_ArrayForEach(p,jget(view,"profiles")){
  const char*name=esim_name(p),*iccid=jstr(p,"iccid");char id[32],label[180],value[80],detail[2048];
  snprintf(id,sizeof(id),"esim.%s",iccid);snprintf(label,sizeof(label),"%s",name);int enabled=cJSON_IsTrue(jget(p,"enabled"));snprintf(value,sizeof(value),"%s · %s",enabled?"正在使用":"已停用",esim_tail(iccid));
  cJSON*d=esim_child(root,s,id,"配置管理",label,value,1);
  i=item(d,"switch",enabled?"停用配置":"启用配置","action",value,enabled?"esim.disable":"esim.enable",ready,enabled?"停用后蜂窝服务断开，直到启用另一配置。":"切换会短暂中断蜂窝连接；请等待完成后再操作。");esim_bind(i,view,p);
  i=item(d,"nickname","重命名","form",name,"esim.nickname",ready,"最多 64 字节；留空恢复原始名称。小屏可输入英文数字，中文可在网页输入。");esim_bind(i,view,p);esim_field(i,"nickname","新名称（留空恢复）",jstr(p,"nickname"),"text",0,64);
  i=item(d,"delete","删除配置","form",enabled?"请先停用此配置":"选择目标后确认删除","esim.delete",ready&&!enabled,"删除不可撤销，随后自动尝试发送通知。迁移前向运营商确认能否重新安装，可能需要新的激活码。");esim_bind(i,view,p);
  cJSON*f=esim_field(i,"confirm_name","确认删除的配置","","choice",1,767);snprintf(label,sizeof(label),"%s · %s",name,esim_tail(iccid));esim_field_choice(f,label,name);
  snprintf(detail,sizeof(detail),"名称：%s\n状态：%s\nICCID：%s\n运营商：%s\n原始名称：%s\n别名：%s\n类别：%s\n应用 AID：%s",name,enabled?"正在使用":"已停用",iccid,jstr(p,"provider"),jstr(p,"name"),*jstr(p,"nickname")?jstr(p,"nickname"):"未设置",jstr(p,"class"),jstr(p,"aid"));esim_report(d,"details","配置详情",name,detail);
 }
 if(euicc){if(!cJSON_GetArraySize(jget(view,"profiles")))item(s,"empty","卡中没有配置","info","可以下载新的 eSIM",NULL,0,NULL);esim_notifications_menu(root,s,view,ready);}
 char detail[4096],summary[160];const char*error=jstr(job,"error"),*state=jstr(job,"state");
 snprintf(summary,sizeof(summary),"%s",running?"正在操作，请等待回读":*error?"最近操作失败":!strcmp(state,"done")?"最近操作已完成":!strcmp(state,"interrupted")?"上次操作中断，结果未知":"尚无操作");
 snprintf(detail,sizeof(detail),"%s\n%s%s\n\n失败或中断先重新读卡核对；不要直接重复下载。",summary,*error?"详情：":"",error);
 cJSON*notification=jget(jget(job,"result"),"notifications");if(*jstr(notification,"error")){size_t n=strlen(detail);snprintf(detail+n,sizeof(detail)-n,"\n配置操作已完成，但自动通知处理未完成：%s\n请重新读取待处理通知后单独处理，无需重复配置操作。",jstr(notification,"error"));}
 esim_report(s,"job","最近操作",summary,detail);
 cJSON*info=jget(view,"info");snprintf(detail,sizeof(detail),"卡片：%s\nEID：%s\nSGP.22：%s\nSE 固件版本：%s\n剩余空间：%.0f 字节\n默认 SM-DP+：%s\n原厂 ICCID：%s\n\n%s\n\nSE 版本不能确定 eSTKme COS 固件版本，EID 前缀不能单独确定硬件代际。Companion 管理配置；卡片 COS 与读卡器固件须由官方工具分别升级。",esim_card_label(view),jstr(view,"eid"),jstr(info,"sgp22"),jstr(info,"firmware"),jget(info,"free_nvm")?jget(info,"free_nvm")->valuedouble:0,jstr(info,"default_smdp"),jstr(jget(view,"firmware"),"iccid"),jstr(view,"detail"));
 esim_report(s,"card","卡片详情",esim_card_label(view),detail);
 if(!running&&(!strcmp(jstr(view,"card"),"error")||!strcmp(jstr(view,"card"),"absent")))esim_report(s,"read-help","读卡排查","检查接触后重新读取","等待作业结束后检查卡片是否插稳、触点是否接触良好，再读取卡片。读取失败本身不能确定为软件问题；先核对卡上结果，再决定是否重做下载或删除。");
 if(cJSON_IsTrue(jget(status,"reboot_needed")))esim_report(s,"reboot","固件尚未读到新卡","查看恢复方法","卡片已切换，但原厂固件尚未识别新配置。请手动重新插卡或重启后读取卡片。");
 cJSON_Delete(status);
}
#endif
