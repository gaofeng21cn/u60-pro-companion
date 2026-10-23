/* B28 cellular accounting. This file is included after panel-advanced-control.h.
 * Only maintenance/actions save; sections are read-only. The controller's global
 * mutation lock must cover ledger_tick() and owned actions. No modem setters.
 * Values are local estimates since the initial baseline, never carrier quota. */
#ifndef PANEL_LEDGER_H
#define PANEL_LEDGER_H
#include <math.h>
#include <limits.h>

#define LEDGER_LIMIT 9007199254740991ULL
#define LEDGER_MAX_SIMS 8
#define LEDGER_FILE_CAP 16384
#ifndef LEDGER_PATH
#define LEDGER_PATH "/data/u60-panel/usage-ledger.json"
#endif
#ifndef LEDGER_DIR
#define LEDGER_DIR "/data/u60-panel"
#endif
#define LEDGER_SOURCE "zwrt_data.get_wwandst source_module=web cid=1 type=4 total_rx_bytes+total_tx_bytes"
/* China-local calendar independent of the firmware process TZ/DST settings. */
#define LEDGER_TZ_SECONDS 28800
#ifndef LEDGER_BUS
#define LEDGER_BUS ubus_call
#endif
#ifndef LEDGER_RUN
#define LEDGER_RUN run_cmd
#endif
#ifndef LEDGER_NOW
#define LEDGER_NOW() time(NULL)
#endif
#ifndef LEDGER_UPTIME
static uint64_t ledger_uptime(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec;}
#define LEDGER_UPTIME() ledger_uptime()
#endif

/* Return 1=read, 0=absent, -1=unreadable/oversized. Never replace a corrupt file. */
static int ledger_file_load(char *buf,size_t cap){
 int fd=open(LEDGER_PATH,O_RDONLY|O_NOFOLLOW);if(fd<0)return errno==ENOENT?0:-1;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<1||(uint64_t)st.st_size>=cap){close(fd);return -1;}
 size_t used=0;while(used<(size_t)st.st_size){ssize_t n=read(fd,buf+used,(size_t)st.st_size-used);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);return -1;}used+=(size_t)n;}buf[used]=0;close(fd);return 1;
}
#ifndef LEDGER_LOAD
#define LEDGER_LOAD ledger_file_load
#endif
static int ledger_file_save(const char *buf){
 char tmp[PATH_MAX];if(snprintf(tmp,sizeof(tmp),"%s/.usage-ledger.XXXXXX",LEDGER_DIR)>=(int)sizeof(tmp))return 0;size_t len=strlen(buf),used=0;if(!len||len>=LEDGER_FILE_CAP)return 0;
 int dir=open(LEDGER_DIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(dir<0)return 0;
 int fd=mkstemp(tmp);if(fd<0){close(dir);return 0;}int ok=fchmod(fd,0600)==0;
 while(ok&&used<len){ssize_t n=write(fd,buf+used,len-used);if(n<0&&errno==EINTR)continue;if(n<=0)ok=0;else used+=(size_t)n;}
 if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;if(ok&&rename(tmp,LEDGER_PATH))ok=0;if(ok&&fsync(dir))ok=0;close(dir);if(!ok)unlink(tmp);
 if(ok){char check[LEDGER_FILE_CAP];ok=ledger_file_load(check,sizeof(check))==1&&!strcmp(check,buf);}
 return ok;
}
#ifndef LEDGER_SAVE
#define LEDGER_SAVE ledger_file_save
#endif
#ifndef LEDGER_BOOT
static int ledger_boot_id(char out[65]){
 int fd=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;char b[80];ssize_t n=read(fd,b,sizeof(b)-1);close(fd);if(n<=0||n>=65)return 0;b[n]=0;b[strcspn(b,"\r\n")]=0;
 if(strlen(b)!=36)return 0;for(size_t i=0;i<36;i++)if(!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')||b[i]=='-'))return 0;snprintf(out,65,"%s",b);return 1;
}
#define LEDGER_BOOT ledger_boot_id
#endif
static void ledger_wipe(void *ptr,size_t len){volatile unsigned char *p=(volatile unsigned char*)ptr;while(len--)*p++=0;}
#ifndef LEDGER_HASH
static int ledger_hash_id(const char *id,char out[65]){
 /* ICCID travels over stdin only, never argv, JSON state, logs or a temp file. */
 char input[100],result[160]={0};snprintf(input,sizeof(input),"u60-panel-usage-v1:%s",id);char *av[]={"sha256sum",NULL};
 int ok=LEDGER_RUN("/usr/bin/sha256sum",av,input,result,sizeof(result));if(!ok)ok=LEDGER_RUN("/bin/sha256sum",av,input,result,sizeof(result));ledger_wipe(input,sizeof(input));
 if(ok&&strlen(result)<64)ok=0;if(ok){for(int i=0;i<64;i++)if(!((result[i]>='0'&&result[i]<='9')||(result[i]>='a'&&result[i]<='f')))ok=0;}
 if(ok){memcpy(out,result,64);out[64]=0;}ledger_wipe(result,sizeof(result));return ok;
}
#define LEDGER_HASH ledger_hash_id
#endif

struct ledger_sim {
 char key[65];uint64_t allowance,used,cycle,anchor_start,expires_at,baseline_at,last_at,resets;
 int configured,reset_day,period_days,warning_percent,gap;
};
struct ledger_db {
 struct ledger_sim sims[LEDGER_MAX_SIMS];int count;
 char active[65],boot[65];uint64_t rx,tx,wall,uptime;
};
struct ledger_sample {char key[65],boot[65];uint64_t rx,tx,wall,uptime;};
struct ledger_view {struct ledger_db db;struct ledger_sample sample;int index,ready,exists,saved;uint64_t persisted_at;const char *error,*event;};

static int ledger_hex(const char *s){if(strlen(s)!=64)return 0;for(int n=0;n<64;n++)if(!((s[n]>='0'&&s[n]<='9')||(s[n]>='a'&&s[n]<='f')))return 0;return 1;}
static int ledger_uint(const cJSON *v,uint64_t *out){
 if(cJSON_IsNumber(v)){double d=v->valuedouble;if(!isfinite(d)||d<0||d>(double)LEDGER_LIMIT||d!=(double)(uint64_t)d)return 0;*out=(uint64_t)d;return 1;}
 if(!cJSON_IsString(v)||!*v->valuestring||strlen(v->valuestring)>16)return 0;
 uint64_t n=0;for(const char*p=v->valuestring;*p;p++){if(*p<'0'||*p>'9'||n>(LEDGER_LIMIT-(unsigned)(*p-'0'))/10)return 0;n=n*10+(unsigned)(*p-'0');}*out=n;return 1;
}
static int ledger_int_field(const cJSON *o,const char *key,int min,int max,int *out){uint64_t v;if(!ledger_uint(jget(o,key),&v)||v<(uint64_t)min||v>(uint64_t)max)return 0;*out=(int)v;return 1;}
static int ledger_wall_valid(uint64_t t){return t>=1577836800ULL&&t<4133980800ULL;} /* 2020..2100 UTC */
static int ledger_leap(int y){return y%4==0&&(y%100!=0||y%400==0);}
static int ledger_days(int y,int m){static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};return d[m-1]+(m==2&&ledger_leap(y));}
/* Civil dates are Gregorian and require neither mktime nor the host timezone. */
static int64_t ledger_days_epoch(int y,int m,int d){y-=m<=2;int era=y/400;unsigned yo=(unsigned)(y-era*400),mp=(unsigned)(m+(m>2?-3:9));unsigned doy=(153*mp+2)/5+(unsigned)d-1;unsigned doe=yo*365+yo/4-yo/100+doy;return (int64_t)era*146097+(int64_t)doe-719468;}
static uint64_t ledger_cycle(uint64_t wall,int reset_day){
 time_t local=(time_t)(wall+LEDGER_TZ_SECONDS);struct tm t;if(!gmtime_r(&local,&t))return 0;
 int y=t.tm_year+1900,m=t.tm_mon+1,d=reset_day,limit=ledger_days(y,m);if(d>limit)d=limit;
 if(t.tm_mday<d){if(--m==0){m=12;y--;}limit=ledger_days(y,m);d=reset_day>limit?limit:reset_day;}
 return (uint64_t)(ledger_days_epoch(y,m,d)*86400-LEDGER_TZ_SECONDS);
}
static uint64_t ledger_fixed_cycle(uint64_t wall,uint64_t anchor_start,int period_days){
 if(!wall||!anchor_start||period_days<1||period_days>365||wall<anchor_start)return 0;
 uint64_t span=(uint64_t)period_days*86400ULL,steps=(wall-anchor_start)/span;
 if(steps>LEDGER_LIMIT/span)return 0;
 return anchor_start+steps*span;
}
static uint64_t ledger_sim_cycle(uint64_t wall,const struct ledger_sim*s){
 return s->period_days>0?ledger_fixed_cycle(wall,s->anchor_start,s->period_days):ledger_cycle(wall,s->reset_day);
}
static uint64_t ledger_sim_cycle_end(uint64_t cycle,const struct ledger_sim*s){
 if(s->period_days>0){uint64_t span=(uint64_t)s->period_days*86400ULL;return cycle<=LEDGER_LIMIT-span?cycle+span-1:cycle;}
 if(cycle>LEDGER_LIMIT-32ULL*86400ULL)return cycle;uint64_t next=ledger_cycle(cycle+32ULL*86400ULL,s->reset_day);return next>cycle?next-1:cycle;
}
static int ledger_date_parse(const cJSON*v,uint64_t*out,int end_of_day){
 const char*s=cJSON_IsString(v)?v->valuestring:"";int y=0,m=0,d=0;char tail=0;
 if(!s||strlen(s)!=10||sscanf(s,"%4d-%2d-%2d%c",&y,&m,&d,&tail)!=3||y<2020||y>2099||m<1||m>12||d<1||d>ledger_days(y,m))return 0;
 for(int i=0;i<10;i++)if(i==4||i==7){if(s[i]!='-')return 0;}else if(s[i]<'0'||s[i]>'9')return 0;
 uint64_t start=(uint64_t)(ledger_days_epoch(y,m,d)*86400-LEDGER_TZ_SECONDS);
 if(end_of_day){if(start>LEDGER_LIMIT-86399ULL)return 0;start+=86399ULL;}
 *out=start;return 1;
}
static void ledger_date(uint64_t wall,char *out,size_t cap,int timestamp){time_t local=(time_t)(wall+LEDGER_TZ_SECONDS);struct tm t;if(!wall||!gmtime_r(&local,&t)){snprintf(out,cap,"未知");return;}strftime(out,cap,timestamp?"%Y-%m-%d %H:%M:%S":"%Y-%m-%d",&t);}

static int ledger_parse(const char *json,struct ledger_db *db){
 const char *end=NULL;cJSON *r=cJSON_ParseWithOpts(json,&end,1);if(!r)return 0;int version=0,ok=0;uint64_t n;
 if(!ledger_int_field(r,"version",1,2,&version)||!ledger_uint(jget(r,"rx"),&db->rx)||!ledger_uint(jget(r,"tx"),&db->tx)||!ledger_uint(jget(r,"wall"),&db->wall)||!ledger_wall_valid(db->wall)||!ledger_uint(jget(r,"uptime"),&db->uptime)||!ledger_hex(jstr(r,"active"))||strlen(jstr(r,"boot"))<1||strlen(jstr(r,"boot"))>64)goto done;
 snprintf(db->active,sizeof(db->active),"%s",jstr(r,"active"));snprintf(db->boot,sizeof(db->boot),"%s",jstr(r,"boot"));
 cJSON *arr=jget(r,"sims"),*p;if(!cJSON_IsArray(arr)||(db->count=cJSON_GetArraySize(arr))<1||db->count>LEDGER_MAX_SIMS)goto done;
 int i=0,found=0;cJSON_ArrayForEach(p,arr){struct ledger_sim*s=&db->sims[i];
  if(!ledger_hex(jstr(p,"key"))||!ledger_int_field(p,"reset_day",1,31,&s->reset_day)||!ledger_int_field(p,"warning_percent",1,100,&s->warning_percent)||!ledger_uint(jget(p,"used"),&s->used)||!ledger_uint(jget(p,"cycle"),&s->cycle)||!ledger_uint(jget(p,"baseline_at"),&s->baseline_at)||!ledger_uint(jget(p,"last_at"),&s->last_at)||!ledger_uint(jget(p,"resets"),&s->resets)||!cJSON_IsBool(jget(p,"gap")))goto done;
  s->period_days=0;s->anchor_start=0;s->expires_at=0;
  if(version>=2){if(!ledger_int_field(p,"period_days",0,365,&s->period_days)||!ledger_uint(jget(p,"anchor_start"),&s->anchor_start)||!ledger_uint(jget(p,"expires_at"),&s->expires_at))goto done;if(s->period_days>0&&(!ledger_wall_valid(s->anchor_start)||s->anchor_start>db->wall||!ledger_wall_valid(s->expires_at)||s->expires_at<s->anchor_start))goto done;}
  if(!ledger_wall_valid(s->baseline_at)||!ledger_wall_valid(s->last_at)||s->last_at<s->baseline_at||s->last_at>db->wall||s->cycle!=ledger_sim_cycle(s->last_at,s))goto done;
  cJSON *allow=jget(p,"allowance");if(cJSON_IsNull(allow))s->configured=0;else {if(!ledger_uint(allow,&n)||!n||n>1000000000000000ULL)goto done;s->configured=1;s->allowance=n;}
  snprintf(s->key,sizeof(s->key),"%s",jstr(p,"key"));s->gap=cJSON_IsTrue(jget(p,"gap"));for(int j=0;j<i;j++)if(!strcmp(s->key,db->sims[j].key))goto done;found|=!strcmp(s->key,db->active);i++;
 }ok=found;
done:cJSON_Delete(r);return ok;
}
static cJSON *ledger_encode(const struct ledger_db *db){
 cJSON*r=cJSON_CreateObject();cJSON_AddNumberToObject(r,"version",2);cJSON_AddStringToObject(r,"active",db->active);cJSON_AddStringToObject(r,"boot",db->boot);cJSON_AddNumberToObject(r,"rx",(double)db->rx);cJSON_AddNumberToObject(r,"tx",(double)db->tx);cJSON_AddNumberToObject(r,"wall",(double)db->wall);cJSON_AddNumberToObject(r,"uptime",(double)db->uptime);cJSON*a=cJSON_AddArrayToObject(r,"sims");
 for(int i=0;i<db->count;i++){const struct ledger_sim*s=&db->sims[i];cJSON*p=cJSON_CreateObject();cJSON_AddItemToArray(a,p);cJSON_AddStringToObject(p,"key",s->key);if(s->configured)cJSON_AddNumberToObject(p,"allowance",(double)s->allowance);else cJSON_AddNullToObject(p,"allowance");cJSON_AddNumberToObject(p,"reset_day",s->reset_day);cJSON_AddNumberToObject(p,"period_days",s->period_days);cJSON_AddNumberToObject(p,"anchor_start",(double)s->anchor_start);cJSON_AddNumberToObject(p,"expires_at",(double)s->expires_at);cJSON_AddNumberToObject(p,"warning_percent",s->warning_percent);cJSON_AddNumberToObject(p,"used",(double)s->used);cJSON_AddNumberToObject(p,"cycle",(double)s->cycle);cJSON_AddNumberToObject(p,"baseline_at",(double)s->baseline_at);cJSON_AddNumberToObject(p,"last_at",(double)s->last_at);cJSON_AddNumberToObject(p,"resets",(double)s->resets);cJSON_AddBoolToObject(p,"gap",s->gap);}
 return r;
}
static int ledger_save(const struct ledger_db *db){cJSON*r=ledger_encode(db);char*text=cJSON_PrintUnformatted(r);cJSON_Delete(r);if(!text)return 0;int ok=strlen(text)<LEDGER_FILE_CAP&&LEDGER_SAVE(text);free(text);return ok;}

/* The SIM getter also returns PIN/PUK fields: wipe every returned string before
 * freeing its short-lived tree. Only a SHA-256 pseudonym survives this call. */
static void ledger_wipe_json(cJSON*r){if(!r)return;if(cJSON_IsString(r)&&r->valuestring)ledger_wipe(r->valuestring,strlen(r->valuestring));cJSON*p;cJSON_ArrayForEach(p,r)ledger_wipe_json(p);}
static int ledger_sim_key(char out[65]){
 cJSON*r=LEDGER_BUS("zwrt_zte_mdm.api","get_sim_info",NULL);const char*slot=jstr(r,"current_sim_slot"),*state=NULL,*id=NULL;
 if(!strcmp(slot,"1")){state=jstr(r,"sim_states");id=jstr(r,"sim_iccid");}
 else if(!strcmp(slot,"2")){state=jstr(r,"sim2_states");id=jstr(r,"sim2_iccid");}
 size_t n=id?strlen(id):0;int ok=firmware_success(r)&&state&&!strcmp(state,"sim ready")&&n>=18&&n<=24;
 /* SIM identifiers may contain an alphabetic filler/nibble on this modem;
  * accept only hexadecimal characters and retain only a SHA-256 pseudonym. */
 for(size_t i=0;i<n&&ok;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')||(id[i]>='A'&&id[i]<='F')))ok=0;
 if(ok)ok=LEDGER_HASH(id,out)&&ledger_hex(out);ledger_wipe_json(r);cJSON_Delete(r);return ok;
}
static int ledger_read_sample(struct ledger_sample *s){
 memset(s,0,sizeof(*s));if(!ledger_sim_key(s->key)||!LEDGER_BOOT(s->boot)||!*s->boot)return 0;
 cJSON*a=cJSON_CreateObject();cJSON_AddStringToObject(a,"source_module","web");cJSON_AddNumberToObject(a,"cid",1);cJSON_AddNumberToObject(a,"type",4);cJSON*r=LEDGER_BUS("zwrt_data","get_wwandst",a);cJSON_Delete(a);
 int cid=0;int ok=firmware_success(r)&&ledger_int_field(r,"cid",1,1,&cid)&&ledger_uint(jget(r,"total_rx_bytes"),&s->rx)&&ledger_uint(jget(r,"total_tx_bytes"),&s->tx);cJSON_Delete(r);
 char after[65];if(!ok||!ledger_sim_key(after)||strcmp(s->key,after))return 0;
 time_t wall=LEDGER_NOW();if(wall<0)return 0;s->wall=(uint64_t)wall;s->uptime=LEDGER_UPTIME();return 1;
}
static void ledger_prepare(struct ledger_view *v){
 memset(v,0,sizeof(*v));v->index=-1;char text[LEDGER_FILE_CAP];int read=LEDGER_LOAD(text,sizeof(text));v->exists=read==1;
 if(read<0||(read==1&&!ledger_parse(text,&v->db))){v->error="台账文件不可读或损坏，已保留原文件";return;}
 v->persisted_at=v->db.wall;
 if(!ledger_read_sample(&v->sample)){v->error="蜂窝累计计数或 SIM 状态未知，未更新台账";return;}
 struct ledger_sample*s=&v->sample;struct ledger_db*d=&v->db;
 if(!ledger_wall_valid(s->wall)||s->wall<d->wall){v->error="设备时间无效或发生回退，暂停记账与周期重置";return;}
 if(v->exists&&!strcmp(s->boot,d->boot)){
  if(s->uptime<d->uptime||s->wall-d->wall>s->uptime-d->uptime+86400){v->error="设备时间异常跳变，暂停记账与周期重置";return;}
 }
 for(int i=0;i<d->count;i++)if(!strcmp(d->sims[i].key,s->key))v->index=i;
 if(v->index<0){if(d->count>=LEDGER_MAX_SIMS){v->error="已保存 8 张 SIM 台账，请先在维护端处理存档";return;}v->index=d->count++;struct ledger_sim*r=&d->sims[v->index];memset(r,0,sizeof(*r));snprintf(r->key,sizeof(r->key),"%s",s->key);r->reset_day=1;r->warning_percent=80;r->baseline_at=s->wall;r->cycle=ledger_cycle(s->wall,1);v->event="已建立当前计数基线，之前的用量未知";}
 struct ledger_sim*r=&d->sims[v->index];if(r->expires_at&&s->wall>r->expires_at){v->error="套餐已到期，未继续累计用量";return;}uint64_t cycle=ledger_sim_cycle(s->wall,r);int rollover=cycle>r->cycle;
 if(cycle<r->cycle){v->error="周期日期回退，未重置台账";return;}
 if(rollover){r->used=0;r->gap=0;r->cycle=cycle;v->event="已进入新周期，跨界采样增量计入新周期";}
 int same=!strcmp(d->active,s->key)&&!strcmp(d->boot,s->boot);
 if(same&&s->rx>=d->rx&&s->tx>=d->tx){uint64_t rx=s->rx-d->rx,tx=s->tx-d->tx;if(rx>LEDGER_LIMIT-r->used||tx>LEDGER_LIMIT-r->used-rx){v->error="累计流量超出安全数值范围，未保存";return;}r->used+=rx+tx;}
 else if(v->exists){if(r->resets<LEDGER_LIMIT)r->resets++;r->gap=1;v->event="重启、换卡或计数回退，已重新建立采样基线";}
 if(v->exists&&s->wall-d->wall>300)r->gap=1;
 r->last_at=s->wall;snprintf(d->active,sizeof(d->active),"%s",s->key);snprintf(d->boot,sizeof(d->boot),"%s",s->boot);d->rx=s->rx;d->tx=s->tx;d->wall=s->wall;d->uptime=s->uptime;v->ready=1;
}

static int ledger_warning(const struct ledger_sim*s){return s->used>=s->allowance?2:s->used>=(s->allowance*(uint64_t)s->warning_percent+99)/100?1:0;}
static cJSON *ledger_usage(const struct ledger_view*v){
 cJSON*u=cJSON_CreateObject();cJSON_AddBoolToObject(u,"available",v->ready);cJSON_AddBoolToObject(u,"local_estimate",1);cJSON_AddStringToObject(u,"source",LEDGER_SOURCE);cJSON_AddStringToObject(u,"timezone","UTC+08:00");cJSON_AddStringToObject(u,"unit","GB = 1000000000 bytes");cJSON_AddStringToObject(u,"status",v->error?v->error:v->event?v->event:"本地估算，不代表运营商账单或真实剩余套餐");
 cJSON_AddBoolToObject(u,"persisted",v->saved);cJSON_AddBoolToObject(u,"stored_ledger_exists",v->exists);if(v->persisted_at)cJSON_AddNumberToObject(u,"persisted_at",(double)v->persisted_at);else cJSON_AddNullToObject(u,"persisted_at");if(!v->ready){const char*k[]={"used_bytes","remaining_bytes","allowance_bytes","cycle_start","sampled_at","warning"};for(size_t i=0;i<sizeof(k)/sizeof(k[0]);i++)cJSON_AddNullToObject(u,k[i]);return u;}
 const struct ledger_sim*s=&v->db.sims[v->index];cJSON_AddNumberToObject(u,"sim_record",v->index+1);cJSON_AddBoolToObject(u,"configured",s->configured);cJSON_AddNumberToObject(u,"used_bytes",(double)s->used);cJSON_AddNumberToObject(u,"reset_day",s->reset_day);cJSON_AddNumberToObject(u,"period_days",s->period_days);cJSON_AddNumberToObject(u,"anchor_start",(double)s->anchor_start);cJSON_AddNumberToObject(u,"expires_at",(double)s->expires_at);cJSON_AddNumberToObject(u,"warning_percent",s->warning_percent);cJSON_AddNumberToObject(u,"sampled_at",(double)s->last_at);cJSON_AddNumberToObject(u,"baseline_at",(double)s->baseline_at);cJSON_AddNumberToObject(u,"cycle_start",(double)s->cycle);uint64_t cycle_end=ledger_sim_cycle_end(s->cycle,s);if(s->expires_at&&cycle_end>s->expires_at)cycle_end=s->expires_at;if(s->period_days>0){char cycle_start_text[16],cycle_end_text[16],expiry_text[16];ledger_date(s->cycle,cycle_start_text,sizeof(cycle_start_text),0);ledger_date(cycle_end,cycle_end_text,sizeof(cycle_end_text),0);ledger_date(s->expires_at,expiry_text,sizeof(expiry_text),0);cJSON_AddStringToObject(u,"cycle_start_date",cycle_start_text);cJSON_AddStringToObject(u,"cycle_end_date",cycle_end_text);cJSON_AddStringToObject(u,"expiry_date",expiry_text);}cJSON_AddNumberToObject(u,"cycle_end",(double)cycle_end);cJSON_AddNumberToObject(u,"baseline_resets",(double)s->resets);cJSON_AddBoolToObject(u,"sampling_gap",s->gap);cJSON_AddBoolToObject(u,"historical_usage_known",0);
 if(s->configured){cJSON_AddNumberToObject(u,"allowance_bytes",(double)s->allowance);cJSON_AddNumberToObject(u,"remaining_bytes",s->used>=s->allowance?0:(double)(s->allowance-s->used));cJSON_AddNumberToObject(u,"overage_bytes",s->used>s->allowance?(double)(s->used-s->allowance):0);int warning=ledger_warning(s);cJSON_AddStringToObject(u,"warning",warning==2?"exceeded":warning==1?"threshold":"normal");}else {cJSON_AddNullToObject(u,"allowance_bytes");cJSON_AddNullToObject(u,"remaining_bytes");cJSON_AddNullToObject(u,"warning");}
 return u;
}
static cJSON *ledger_response(struct ledger_view *v,int save){
 int ok=v->ready;if(ok&&save){if(!ledger_save(&v->db)){ok=0;v->error="台账写入或持久化回读失败，未确认保存";v->ready=0;}else {v->exists=v->saved=1;v->persisted_at=v->db.wall;}}
 cJSON*r=reply(ok,v->error?v->error:v->event?v->event:save?"流量台账已持久化并回读":"已读取本地流量估算");cJSON_AddItemToObject(cJSON_AddObjectToObject(r,"data"),"usage",ledger_usage(v));return r;
}
static cJSON *ledger_tick(void){struct ledger_view v;ledger_prepare(&v);return ledger_response(&v,1);}

/* Accept strict decimal GB only (six decimal places = 1 KB resolution).
 * No exponent/NaN/sign/whitespace coercion, and zero means an actual zero. */
static int ledger_gb(const cJSON*v,uint64_t*out,int allow_blank,int *blank){
 *blank=0;if(allow_blank&&(cJSON_IsNull(v)||(cJSON_IsString(v)&&!*v->valuestring))){*blank=1;*out=0;return 1;}
 char number[64];const char*s;if(cJSON_IsString(v))s=v->valuestring;else if(cJSON_IsNumber(v)){if(!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>1000000)return 0;snprintf(number,sizeof(number),"%.6f",v->valuedouble);s=number;}else return 0;
 if(!*s||strlen(s)>32)return 0;uint64_t whole=0,frac=0,scale=1;int digits=0,seen=0;for(;*s;s++){if(*s=='.'&&!seen){if(!digits)return 0;seen=1;digits=0;continue;}if(*s<'0'||*s>'9')return 0;if(seen){if(++digits>6)return 0;frac=frac*10+(unsigned)(*s-'0');scale*=10;}else {digits++;whole=whole*10+(unsigned)(*s-'0');if(whole>1000000)return 0;}}
 if(!digits)return 0;if(whole==1000000&&frac)return 0;*out=whole*1000000000ULL+frac*(1000000000ULL/scale);return !cJSON_IsNumber(v)||(double)*out/1e9==v->valuedouble;
}
static cJSON *ledger_action(const char *action,const cJSON *args){
 int configure=!strcmp(action,"usage.configure"),adjust=!strcmp(action,"usage.adjust"),allowance=!strcmp(action,"usage.allowance"),cycle=!strcmp(action,"usage.cycle"),plan=!strcmp(action,"usage.plan"),warn=!strcmp(action,"usage.warning"),remaining=!strcmp(action,"usage.remaining");
 if(!configure&&!adjust&&!allowance&&!cycle&&!plan&&!warn&&!remaining&&strcmp(action,"usage.refresh"))return NULL;
 uint64_t bytes=0;int blank=0,day=0,warning=0;
 if((configure||allowance||plan)&&(!ledger_gb(jget(args,"allowance_gb"),&bytes,1,&blank)||(!blank&&!bytes)))return reply(0,"额度须为大于 0 至 1000000 GB，留空取消额度");
 if((configure||cycle)&&!ledger_int_field(args,"reset_day",1,31,&day))return reply(0,"每月结算日须为 1–31 的整数");
 if((configure||warn)&&!ledger_int_field(args,"warning_percent",1,100,&warning))return reply(0,"预警阈值须为 1–100% 的整数");
 uint64_t anchor=0,expires=0;int period=0;
 if(plan&&(!ledger_int_field(args,"period_days",1,365,&period)||!ledger_date_parse(jget(args,"anchor_start"),&anchor,0)||!ledger_date_parse(jget(args,"expires_date"),&expires,1)||expires<anchor))return reply(0,"周期需为 1–365 天，起始日和到期日须为有效日期，且到期日不得早于起始日");
 if(adjust&&!ledger_gb(jget(args,"used_gb"),&bytes,0,&blank))return reply(0,"本周期累计用量须为 0–1000000 GB，最多 6 位小数");
 if(remaining&&!ledger_gb(jget(args,"remaining_gb"),&bytes,0,&blank))return reply(0,"剩余流量须为 0–1000000 GB，最多 6 位小数");
 struct ledger_view v;ledger_prepare(&v);if(v.ready){
  struct ledger_sim*s=&v.db.sims[v.index];
  /* Each single-field edit merges into a freshly read ledger under the global
   * mutation lock. Never overwrite unrelated settings from a stale UI form. */
  if(remaining){if(!s->configured)return reply(0,"请先设置套餐额度，再校准剩余流量");if(bytes>s->allowance)return reply(0,"剩余流量不能大于套餐额度，请先核对额度");s->used=s->allowance-bytes;v.event="剩余流量已校准，已用量同步更新；之后继续累计";}
  if(plan){if(anchor>v.sample.wall)return reply(0,"周期起始日尚未到达，未保存套餐计划");if(v.sample.wall>expires)return reply(0,"套餐已超过有效期，未保存套餐计划");uint64_t next=ledger_fixed_cycle(v.sample.wall,anchor,period);if(!next)return reply(0,"当前日期不在所设周期内，未保存套餐计划");if(s->cycle!=next){s->used=0;s->gap=1;v.event="周期规则已更新；此前本周期用量未知，从当前采样建立基线";}s->configured=!blank;s->allowance=bytes;s->period_days=period;s->anchor_start=anchor;s->expires_at=expires;s->cycle=next;if(!v.event)v.event="套餐周期和额度已保存；历史用量未知，从当前采样建立基线";}
  if(configure||allowance){s->configured=!blank;s->allowance=bytes;v.event="套餐额度已保存，保留当前累计用量";}
  if(configure||cycle){s->reset_day=day;s->period_days=0;s->anchor_start=0;s->expires_at=0;s->cycle=ledger_cycle(v.sample.wall,day);v.event="结算日已保存，保留当前累计用量";}
  if(configure||warn){s->warning_percent=warning;v.event="预警阈值已保存";}
  if(configure)v.event="套餐参数已保存，保留当前累计用量；可单独校准";
  if(adjust){s->used=bytes;v.event="本周期用量已校准，之后继续累计蜂窝增量";}
 }
 return ledger_response(&v,1);
}
static void ledger_edit_value(uint64_t bytes,char*out,size_t n){
 snprintf(out,n,"%.6f",(double)bytes/1e9);char*dot=strchr(out,'.');if(dot){char*end=out+strlen(out)-1;while(end>dot&&*end=='0')*end--=0;if(end==dot)*end=0;}
}
static void ledger_sections(cJSON *root){
 struct ledger_view v;ledger_prepare(&v);cJSON*d=jget(root,"data");if(!d)d=cJSON_AddObjectToObject(root,"data");cJSON_DeleteItemFromObject(d,"usage");cJSON_AddItemToObject(d,"usage",ledger_usage(&v));
 cJSON*s=adv_section(root,"usage","套餐流量账本"),*i;char text[180],value[80];const struct ledger_sim*r=v.ready?&v.db.sims[v.index]:NULL;
 const char*unavailable=v.error?v.error:"当前 SIM 或计数不可用，请稍后重试";
 value[0]=0;if(r&&r->configured)ledger_edit_value(r->allowance,value,sizeof(value));snprintf(text,sizeof(text),*value?"%s GB":"未设置",value);
 i=item(s,"usage.allowance","套餐额度","form",text,"usage.allowance",v.ready,v.ready?"仅修改当前卡额度；留空取消额度，保留已用量":unavailable);field(i,"allowance_gb","额度 GB（留空不设）",value,"number",0);
 value[0]=0;if(r)ledger_edit_value(r->used,value,sizeof(value));snprintf(text,sizeof(text),r?"%.3f GB":"未知",r?(double)r->used/1e9:0);
 i=item(s,"usage.used","本周期已用","form",text,"usage.adjust",v.ready,v.ready?"按运营商数据校准已用量；之后继续累计":unavailable);field(i,"used_gb","本周期已用 GB",value,"number",1);
 value[0]=0;if(r&&r->configured)ledger_edit_value(r->used>=r->allowance?0:r->allowance-r->used,value,sizeof(value));snprintf(text,sizeof(text),*value?"%s GB":"先设置套餐额度",value);
 i=item(s,"usage.remaining","本周期剩余","form",text,"usage.remaining",r&&r->configured,v.ready?"按运营商数据校准剩余；已用量改为额度减剩余。请先设置额度":unavailable);field(i,"remaining_gb","本周期剩余 GB",value,"number",1);
 char anchor_text[16]="",expiry_text[16]="",period_text[16]="",allowance_text[80]="";
 if(r){if(r->period_days>0){ledger_date(r->anchor_start,anchor_text,sizeof(anchor_text),0);ledger_date(r->expires_at,expiry_text,sizeof(expiry_text),0);snprintf(period_text,sizeof(period_text),"%d",r->period_days);snprintf(text,sizeof(text),"%d 天 · 自 %s",r->period_days,anchor_text);}else {snprintf(value,sizeof(value),"%d",r->reset_day);snprintf(text,sizeof(text),"每月 %s 日",value);snprintf(period_text,sizeof(period_text),"30");}if(r->configured)ledger_edit_value(r->allowance,allowance_text,sizeof(allowance_text));}
 else snprintf(text,sizeof(text),"未设置");
 i=item(s,"usage.cycle","套餐周期","form",text,"usage.plan",v.ready,v.ready?"按固定天数和锚点自动滚动；到期日按本地日期含当天。修改周期时本周期历史用量重新未知":unavailable);field(i,"allowance_gb","套餐额度 GB",allowance_text,"number",1);field(i,"period_days","周期天数",period_text,"number",1);field(i,"anchor_start","首周期起始日 YYYY-MM-DD",anchor_text,"text",1);field(i,"expires_date","套餐到期日 YYYY-MM-DD",expiry_text,"text",1);
 snprintf(value,sizeof(value),"%d",r?r->warning_percent:80);snprintf(text,sizeof(text),"%s%%",value);
 i=item(s,"usage.warning","预警阈值","form",text,"usage.warning",v.ready,v.ready?"达到此已用比例时提示；不自动断网":unavailable);field(i,"warning_percent","已用比例 %（1–100）",value,"number",1);
 i=item(s,"usage.refresh","立即采样保存","button","更新本地用量","usage.refresh",v.ready,v.ready?"只读取设备计数并保存本地台账":unavailable);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 char detail[1600],start[64]="未知",sample[64]="未知",baseline[64]="未知",end[64]="未知",expiry[64]="未设置";if(r){ledger_date(r->cycle,start,sizeof(start),0);ledger_date(r->last_at,sample,sizeof(sample),1);ledger_date(r->baseline_at,baseline,sizeof(baseline),1);uint64_t end_at=ledger_sim_cycle_end(r->cycle,r);if(r->expires_at&&end_at>r->expires_at)end_at=r->expires_at;ledger_date(end_at,end,sizeof(end),0);if(r->expires_at)ledger_date(r->expires_at,expiry,sizeof(expiry),0);}
 snprintf(detail,sizeof(detail),"本地估算，不代表运营商账单。\n额度、已用或剩余请按运营商数据设置。\n当前 SIM 台账：%d\n周期：%s 至 %s\n周期规则：%s\n套餐到期：%s\n采样时间：%s\n开始记录：%s\n记录状态：%s\n用量预警：%s\n采样中断：%s\n数据来源：蜂窝 CID 1 累计收发字节\n单位：GB = 1000000000 字节\n结算时区：UTC+8；固定周期含首尾日期\n套餐周期更新后从当前采样建立基线，历史用量未知。",v.ready?v.index+1:0,start,end,r&&r->period_days>0?"固定天数滚动": "按月滚动",expiry,sample,baseline,v.error?v.error:v.event?v.event:"校准后继续累加设备流量",r&&r->configured?(ledger_warning(r)==2?"估算额度已用完":ledger_warning(r)==1?"已达预警阈值":"未达预警阈值"):"未设置套餐额度",r?(r->gap?"可能存在漏计，请按运营商数据校准":"未检测到"):"未知");
 i=item(s,"usage.details","记录详情与说明","report","周期、采样与统计口径",NULL,1,NULL);cJSON_AddStringToObject(i,"detail",detail);
}
#endif
