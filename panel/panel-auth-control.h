#ifndef PANEL_AUTH_CONTROL_H
#define PANEL_AUTH_CONTROL_H
/* Read-only adapter for stock B31 terminal-auth APIs. No raw response escapes. */
static int auth_stamp(const char *s){
 if(strlen(s)!=19)return 0;
 for(int n=0;n<19;n++){if(n==4||n==7){if(s[n]!='-')return 0;}else if(n==10){if(s[n]!=' ')return 0;}else if(n==13||n==16){if(s[n]!=':')return 0;}else if(s[n]<'0'||s[n]>'9')return 0;}return 1;
}
static const char *auth_path(const cJSON *o){
 static const char *const keys[]={"url","h5url","routerH5Url","router_h5_url","value"};
 if(!cJSON_IsObject(o))return "";
 for(unsigned n=0;n<sizeof(keys)/sizeof(*keys);n++){
  const char *v=jstr(o,keys[n]);
  if(v[0]=='/'&&v[1]!='/'&&!strchr(v,'\\')){
   int ok=1;for(const char *p=v;*p;p++)if((unsigned char)*p<0x20){ok=0;break;}
   if(ok)return v;
  }
 }
 return "";
}
static void auth_sections(cJSON *root){
 cJSON *succ=ubus_read("zwrt_redirect.api","get_second_auth_succ_list"),*fail=ubus_read("zwrt_redirect.api","get_second_auth_fail_list"),*result=ubus_read("zwrt_wireless_auth.manager","get_terminal_auth_result"),*h5=ubus_read("zwrt_wireless_auth.manager","get_h5url"),*h5uci=ubus_read("zwrt_wireless_auth.manager","get_h5url_uci");
 cJSON *sa=jget(succ,"second_auth_succ_list_info"),*fa=jget(fail,"second_auth_fail_list_info");
 int sk=cJSON_IsArray(sa)&&firmware_success(succ),fk=cJSON_IsArray(fa)&&firmware_success(fail);
 cJSON *d=cJSON_AddObjectToObject(jget(root,"data"),"second_auth");
 cJSON_AddBoolToObject(d,"available",sk||fk||cJSON_IsObject(result));cJSON_AddStringToObject(d,"status","unknown");
 cJSON_AddNullToObject(d,"needs_auth");
 const char *entry=auth_path(h5);if(!*entry)entry=auth_path(h5uci);if(*entry)cJSON_AddStringToObject(d,"entry_url",entry);else cJSON_AddNullToObject(d,"entry_url");
 cJSON_AddStringToObject(d,"scope","router_records_not_current_browser");
 if(sk)cJSON_AddNumberToObject(d,"success_count",cJSON_GetArraySize(sa));else cJSON_AddNullToObject(d,"success_count");
 if(fk)cJSON_AddNumberToObject(d,"failure_count",cJSON_GetArraySize(fa));else cJSON_AddNullToObject(d,"failure_count");
 /* B31 expire_time is a timezone-free stock local timestamp. Preserve it as
  * local display text; never compare it to a guessed server/UTC clock. */
 const char *latest="";cJSON *v;cJSON_ArrayForEach(v,sa){const char *s=jstr(v,"expire_time");if(auth_stamp(s)&&strcmp(s,latest)>0)latest=s;}
 if(*latest)cJSON_AddStringToObject(d,"latest_expiry",latest);else cJSON_AddNullToObject(d,"latest_expiry");
 cJSON_AddNullToObject(d,"server_time");
 const char *result_text=jstr(result,"result");
 if(!strcmp(result_text,"success")||!strcmp(result_text,"authorized")){cJSON_ReplaceItemInObject(d,"status",cJSON_CreateString("authorized"));cJSON_ReplaceItemInObject(d,"needs_auth",cJSON_CreateBool(0));}
 else if(!strcmp(result_text,"fail")||!strcmp(result_text,"failed")){cJSON_ReplaceItemInObject(d,"status",cJSON_CreateString("needs_auth"));cJSON_ReplaceItemInObject(d,"needs_auth",cJSON_CreateBool(1));}
 cJSON_AddStringToObject(d,"failure_reason",fk&&cJSON_GetArraySize(fa)>0?"原厂记录存在未通过认证的终端；接口未提供具体失败原因":"原厂接口未提供具体失败原因");
 cJSON_Delete(succ);cJSON_Delete(fail);cJSON_Delete(result);cJSON_Delete(h5);cJSON_Delete(h5uci);
}
#endif
