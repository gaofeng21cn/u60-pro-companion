#ifndef PANEL_AUTH_BYPASS_H
#define PANEL_AUTH_BYPASS_H
/* Projection for panel/auth-bypass.sh. The script is the single writer and
 * every response is read back. No MAC, token or carrier identifier is
 * returned to the UI. */
static cJSON *auth_bypass_run(const char *command){
 char out[4096];const char *path="/data/u60-panel/auth-bypass.sh";
 if(fixture){
  cJSON *m=jget(fixture,command);if(m)return cJSON_Duplicate(m,1);
  if(!strcmp(command,"enable")||!strcmp(command,"disable")){fixture_write_count++;return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"测试：终端放行开关请求已提交");}
  return reply(0,"fixture auth bypass response missing");
 }
 char *argv[]={(char*)path,(char*)command,NULL};int ok=run_cmd(path,argv,NULL,out,sizeof(out));
 cJSON*r=ok?cJSON_Parse(out):NULL;if(!cJSON_IsObject(r)){cJSON_Delete(r);return reply(0,"终端放行控制器未返回有效结果");}return r;
}
static cJSON *auth_bypass_action(const char *action,const cJSON *args){
 (void)args;
 if(!strcmp(action,"auth.bypass.state"))return auth_bypass_run("status");
 if(!strcmp(action,"auth.bypass.enable"))return auth_bypass_run("enable");
 if(!strcmp(action,"auth.bypass.disable"))return auth_bypass_run("disable");
 return NULL;
}
static void auth_bypass_sections(cJSON *root){
 cJSON *r=auth_bypass_run("status");
 int enabled=cJSON_IsTrue(jget(r,"enabled"));
 int guard=cJSON_IsTrue(jget(r,"guard_running"));
 const char *valid=jstr(r,"valid"),*sw=jstr(r,"switch");
 const char *label=!enabled?"已关闭 · 原厂按终端认证":guard?"已开启 · 本机放行所有终端":"已开启 · 守护未运行";
 cJSON *s=adv_section(root,"auth-bypass","终端上网放行");
 cJSON *i=item(s,"auth.bypass.toggle","本机放行所有 Wi-Fi 终端","toggle",label,
   enabled?"auth.bypass.disable":"auth.bypass.enable",1,
   "关闭原厂在本机对未完成认证或认证过期终端的拦截与踢下线；不改动运营商侧上报与套餐。原厂关掉后在原厂首页「上网认证」对应终端仍可正常上网。");
 cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(1));
 cJSON_AddBoolToObject(jget(i,"args"),"enabled",!enabled);
 char summary[256];snprintf(summary,sizeof(summary),
   "认证门控=%s 踢终端门控=%s%s",*valid?valid:"未知",*sw?sw:"未知",
   cJSON_IsTrue(jget(r,"drift"))?" · 原厂已重新写入，守护将立即复位":"");
 item(s,"auth.bypass.state","本机执行状态","info",summary,NULL,0,NULL);
 cJSON_Delete(r);
}
#endif
