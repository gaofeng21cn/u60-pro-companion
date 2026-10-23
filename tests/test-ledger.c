#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "cJSON.h"

static cJSON *jget(const cJSON *,const char *);
static const char *jstr(const cJSON *,const char *);
static cJSON *reply(int,const char *);
static int firmware_success(const cJSON *);
static cJSON *test_bus(const char *,const char *,cJSON *);
static int test_run(const char *,char *const [],const char *,char *,size_t);
static int test_load(char *,size_t);
static int test_save(const char *);
static int test_boot(char [65]);
static int test_hash(const char *,char [65]);
static time_t test_now;
static uint64_t test_uptime;
static uint64_t test_rx,test_tx;
static char saved[16384];
static cJSON *adv_section(cJSON *,const char *,const char *);
static cJSON *item(cJSON *,const char *,const char *,const char *,const char *,const char *,int,const char *);
static void field(cJSON *,const char *,const char *,const char *,const char *,int);

#define LEDGER_BUS test_bus
#define LEDGER_RUN test_run
#define LEDGER_LOAD test_load
#define LEDGER_SAVE test_save
#define LEDGER_BOOT test_boot
#define LEDGER_HASH test_hash
#define LEDGER_NOW() test_now
#define LEDGER_UPTIME() test_uptime
#include "../panel/panel-ledger.h"

static cJSON *jget(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static const char *jstr(const cJSON *o,const char *key){cJSON*v=jget(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *reply(int ok,const char *message){cJSON*r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",ok);cJSON_AddStringToObject(r,"message",message);return r;}
static int firmware_success(const cJSON*r){return cJSON_IsObject(r)&&!jget(r,"error");}
static int test_run(const char*p,char *const av[],const char*in,char*out,size_t cap){(void)p;(void)av;(void)in;if(cap)out[0]=0;return 1;}
static int test_load(char*out,size_t cap){if(!saved[0])return 0;if(strlen(saved)>=cap)return -1;snprintf(out,cap,"%s",saved);return 1;}
static int test_save(const char*text){if(strlen(text)>=sizeof(saved))return 0;snprintf(saved,sizeof(saved),"%s",text);return 1;}
static int test_boot(char out[65]){snprintf(out,65,"01234567-89ab-cdef-0123-456789abcdef");return 1;}
static int test_hash(const char*id,char out[65]){assert(id&&strchr(id,'D'));memset(out,'a',64);out[64]=0;return 1;}
static cJSON *test_bus(const char*obj,const char*method,cJSON*args){
 (void)args;
 if(!strcmp(obj,"zwrt_zte_mdm.api")&&!strcmp(method,"get_sim_info"))return cJSON_Parse("{\"sim_states\":\"sim undetected\",\"sim_iccid\":\"\",\"current_sim_slot\":\"2\",\"sim2_states\":\"sim ready\",\"sim2_iccid\":\"898608741525D0125864\"}");
 if(!strcmp(obj,"zwrt_data")&&!strcmp(method,"get_wwandst")){cJSON*r=cJSON_CreateObject();cJSON_AddNumberToObject(r,"cid",1);cJSON_AddNumberToObject(r,"total_rx_bytes",(double)test_rx);cJSON_AddNumberToObject(r,"total_tx_bytes",(double)test_tx);return r;}
 return NULL;
}
static cJSON *adv_section(cJSON*root,const char*id,const char*title){cJSON*s=cJSON_CreateObject();cJSON_AddStringToObject(s,"id",id);cJSON_AddStringToObject(s,"title",title);cJSON_AddArrayToObject(s,"items");cJSON_AddItemToArray(jget(root,"sections"),s);return s;}
static cJSON *item(cJSON*s,const char*id,const char*label,const char*type,const char*value,const char*action,int enabled,const char*reason){(void)action;(void)enabled;(void)reason;cJSON*i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id",id);cJSON_AddStringToObject(i,"label",label);cJSON_AddStringToObject(i,"type",type);cJSON_AddStringToObject(i,"value",value?value:"");cJSON_AddItemToArray(jget(s,"items"),i);return i;}
static void field(cJSON*i,const char*key,const char*label,const char*value,const char*kind,int required){(void)i;(void)key;(void)label;(void)value;(void)kind;(void)required;}
static uint64_t local_midnight(int y,int m,int d){return (uint64_t)(ledger_days_epoch(y,m,d)*86400-LEDGER_TZ_SECONDS);}
static cJSON *plan_args(void){return cJSON_Parse("{\"allowance_gb\":1500,\"period_days\":30,\"anchor_start\":\"2026-10-17\",\"expires_date\":\"2028-05-09\"}");}
static cJSON *usage_from(cJSON*r){return jget(jget(r,"data"),"usage");}

int main(void){
 test_rx=1000;test_tx=2000;test_now=(time_t)(local_midnight(2026,10,24)+12*3600);test_uptime=20000;
 cJSON*args=plan_args(),*r=ledger_action("usage.plan",args);assert(cJSON_IsTrue(jget(r,"ok")));cJSON*u=usage_from(r);
 assert(cJSON_IsTrue(jget(u,"persisted"))&&cJSON_IsFalse(jget(u,"historical_usage_known")));
 assert(jget(u,"used_bytes")->valuedouble==0&&jget(u,"allowance_bytes")->valuedouble==1500000000000.0);
 assert(!strcmp(jstr(u,"cycle_start_date"),"2026-10-17"));assert(!strcmp(jstr(u,"cycle_end_date"),"2026-11-15"));assert(!strcmp(jstr(u,"expiry_date"),"2028-05-09"));
 assert(!strstr(saved,"898608741525D0125864"));
 struct ledger_db db={0};assert(ledger_parse(saved,&db));assert(db.sims[0].period_days==30&&db.sims[0].cycle==local_midnight(2026,10,17));cJSON_Delete(r);cJSON_Delete(args);

 uint64_t elapsed=(uint64_t)(local_midnight(2026,11,16)-((uint64_t)test_now));test_now+=(time_t)(elapsed+300);test_uptime+=elapsed+300;test_rx+=125;test_tx+=75;
 r=ledger_tick();assert(cJSON_IsTrue(jget(r,"ok")));u=usage_from(r);assert(jget(u,"used_bytes")->valuedouble==200);assert(!strcmp(jstr(u,"cycle_start_date"),"2026-11-16"));cJSON_Delete(r);

 db=(struct ledger_db){0};assert(ledger_parse(saved,&db));uint64_t expiry=db.sims[0].expires_at;
 test_uptime+=(expiry-(uint64_t)test_now);test_now=(time_t)expiry;test_rx++;r=ledger_tick();assert(cJSON_IsTrue(jget(r,"ok")));cJSON_Delete(r);
 test_uptime++;test_now++;r=ledger_tick();assert(cJSON_IsFalse(jget(r,"ok")));assert(strstr(jstr(r,"message"),"已到期"));cJSON_Delete(r);
 args=cJSON_Parse("{\"allowance_gb\":1500,\"period_days\":30,\"anchor_start\":\"2026/10/17\",\"expires_date\":\"2028-05-09\"}");r=ledger_action("usage.plan",args);assert(cJSON_IsFalse(jget(r,"ok")));cJSON_Delete(r);cJSON_Delete(args);

 char legacy[2048];uint64_t wall=(uint64_t)(local_midnight(2026,10,24)+12*3600),monthly=ledger_cycle(wall,1);
 snprintf(legacy,sizeof(legacy),"{\"version\":1,\"active\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"boot\":\"boot\",\"rx\":1,\"tx\":2,\"wall\":%llu,\"uptime\":1,\"sims\":[{\"key\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"allowance\":null,\"reset_day\":1,\"warning_percent\":80,\"used\":0,\"cycle\":%llu,\"baseline_at\":%llu,\"last_at\":%llu,\"resets\":0,\"gap\":false}]}",(unsigned long long)wall,(unsigned long long)monthly,(unsigned long long)wall,(unsigned long long)wall);
 db=(struct ledger_db){0};assert(ledger_parse(legacy,&db));assert(db.sims[0].period_days==0&&db.sims[0].cycle==monthly);assert(ledger_sim_cycle_end(monthly,&db.sims[0])==local_midnight(2026,11,1)-1);
 puts("PASS: active SIM2 identity, private pseudonym, 30-day anchor/boundaries, quota/expiry, unknown history and v1 migration");return 0;
}
