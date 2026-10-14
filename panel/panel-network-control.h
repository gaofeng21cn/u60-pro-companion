#ifndef PANEL_NETWORK_CONTROL_H
#define PANEL_NETWORK_CONTROL_H
/* The shell script is the sole network-policy writer. This header only adds
 * bounded readback and command descriptors to the existing panel owner. */
static cJSON *network_control_run(const char *command,const char *input){
 char out[32768];const char *path="/data/u60-panel/network-control.sh";
 if(fixture){
  cJSON *m=jget(fixture,command);if(m)return cJSON_Duplicate(m,1);
  return reply(0,"测试 fixture 未提供网络状态");
 }
 char *argv[]={(char*)path,(char*)command,NULL};
 int ok=run_cmd(path,argv,input,out,sizeof(out));cJSON *r=ok?cJSON_Parse(out):NULL;
 if(!cJSON_IsObject(r)){cJSON_Delete(r);return reply(0,"网络控制器未返回有效结果");}return r;
}
static cJSON *network_control_action(const char *action,const cJSON *args){
 (void)args;
 if(!strcmp(action,"network.status"))return network_control_run("status",NULL);
 if(!strcmp(action,"network.clients"))return network_control_run("clients",NULL);
 if(!strcmp(action,"network.refresh"))return network_control_run("refresh",NULL);
 if(!strcmp(action,"network.off"))return network_control_run("off",NULL);
 return NULL;
}
static void network_control_sections(cJSON *root){
 cJSON *s=section(root,"network-control","终端网络");
 /* Keep the screen as a read-only pointer. Live hooks and per-MAC policy
  * remain owned by the web network page, so there is no second writer on the
  * 320x480 display. */
 item(s,"network-status","网络能力","info","网页查看状态与启停",NULL,0,"网页网络页显示 TCP、UDP TPROXY、IPv6 旁路保护和真实流量观测");
 item(s,"network-clients","连接设备","info","网页按 MAC 管理",NULL,0,"网页网络页按 MAC 管理终端线路；小屏保留只读入口");
}
#endif
