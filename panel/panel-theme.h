#ifndef PANEL_THEME_H
#define PANEL_THEME_H
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef PANEL_THEME_FILE
#define PANEL_THEME_FILE "/data/u60-panel/theme"
#endif
#define PANEL_THEME_COUNT 4
static const char *panel_theme_key(int id){static const char*keys[]={"classic","paper","seaglass","beacon"};return id>=0&&id<PANEL_THEME_COUNT?keys[id]:keys[0];}
/* Old keys remain readable; the product exposes the two redesigned variants. */
static int panel_theme_id(const char *name){for(int i=0;name&&i<PANEL_THEME_COUNT;i++)if(!strcmp(name,panel_theme_key(i)))return i==0||i==2?3:i;return -1;}
static const char *panel_theme_name(int id){return id==1?"浅色":"深色";}
static int panel_theme_load(void){
 char b[24];FILE*f=fopen(PANEL_THEME_FILE,"r");if(!f)return 3;
 size_t n=fread(b,1,sizeof(b)-1,f);int complete=feof(f);fclose(f);if(!complete)return 3;
 while(n&&(b[n-1]=='\n'||b[n-1]=='\r'))n--;b[n]=0;int id=panel_theme_id(b);return id<0?3:id;
}
static int panel_theme_save(const char *name){
 int id=panel_theme_id(name);if(id<0)return 0;
 char tmp[512],b[24];if(snprintf(tmp,sizeof(tmp),"%s.next",PANEL_THEME_FILE)>=(int)sizeof(tmp))return 0;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
 int n=snprintf(b,sizeof(b),"%s\n",panel_theme_key(id)),ok=write(fd,b,n)==n&&fsync(fd)==0;if(close(fd))ok=0;
 if(ok)ok=rename(tmp,PANEL_THEME_FILE)==0;if(!ok){unlink(tmp);return 0;}return panel_theme_load()==id;
}
static cJSON *panel_theme_item(int id){
 cJSON*i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id","theme");cJSON_AddStringToObject(i,"label","界面主题");cJSON_AddStringToObject(i,"value",panel_theme_name(id));cJSON_AddStringToObject(i,"type","choice");cJSON_AddStringToObject(i,"action","screen.theme");cJSON_AddBoolToObject(i,"enabled",1);cJSON_AddBoolToObject(i,"confirm",0);
 static const int variants[]={3,1};cJSON*ch=cJSON_AddArrayToObject(i,"choices");for(int n=0;n<2;n++){int variant=variants[n];cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",panel_theme_name(variant));cJSON*args=cJSON_AddObjectToObject(c,"args");cJSON_AddStringToObject(args,"theme",panel_theme_key(variant));cJSON_AddItemToArray(ch,c);}return i;
}
#endif
