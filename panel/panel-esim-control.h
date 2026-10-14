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
static void esim_sections(cJSON*root) {
 cJSON*s=section(root,"esim","eSIM"),*status=esim_status_read(),*view=jget(status,"view"),*job=jget(status,"job");
 int running=!strcmp(jstr(job,"state"),"running"),installed=cJSON_IsTrue(jget(status,"installed"));
 int ready=installed&&!running&&!strcmp(jstr(view,"card"),"euicc");
 cJSON_AddItemToObject(jget(root,"data"),"esim",cJSON_Duplicate(status,1));
 item(s,"card","卡片状态","info",esim_card_label(view),NULL,0,NULL);
 if(job){const char*error=jstr(job,"error");item(s,"job","最近操作","info",*error?error:!strcmp(jstr(job,"state"),"running")?"正在操作，请等待回读":!strcmp(jstr(job,"state"),"done")?"已完成并回读":"操作未确认",NULL,0,NULL);}
 if(cJSON_IsTrue(jget(status,"reboot_needed")))item(s,"reboot","固件尚未读到新卡","info","请手动重新插卡或重启后读取",NULL,0,NULL);
 cJSON*i=item(s,"esim.scan","读取卡片与配置文件","action","不修改配置文件","esim.scan",installed&&!running,installed?"只读探测；普通 SIM 不会被修改":"eSIM 程序尚未安装");cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 cJSON*p;cJSON_ArrayForEach(p,jget(view,"profiles")){
  const char*name=*jstr(p,"nickname")?jstr(p,"nickname"):*jstr(p,"name")?jstr(p,"name"):"未命名配置",*iccid=jstr(p,"iccid");
  char id[60],label[180];size_t len=strlen(iccid);snprintf(id,sizeof(id),"profile.%s",iccid);snprintf(label,sizeof(label),"%s · %.4s",name,len>=4?iccid+len-4:iccid);
  int enabled=cJSON_IsTrue(jget(p,"enabled"));i=item(s,id,label,enabled?"info":"choice",enabled?"正在使用":"点此启用","esim.enable",ready&&!enabled,"切换会中断蜂窝连接；卡繁忙时请勿连续重试");
  cJSON_AddBoolToObject(jget(i,"args"),"confirmed",1);cJSON_AddStringToObject(jget(i,"args"),"expected_eid",jstr(view,"eid"));choice(i,"启用此配置","iccid",iccid);
 }
 item(s,"web","下载与管理","info","网页：增强功能 → eSIM",NULL,0,"下载、停用、改名、删除与通知在网页操作");
 cJSON_Delete(status);
}
#endif
