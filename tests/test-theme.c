#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "cJSON.h"
static char path[400];
#define PANEL_THEME_FILE path
#include "../panel/panel-theme.h"
int main(void){
 char dir[]="/tmp/u60-theme-test.XXXXXX";assert(mkdtemp(dir));snprintf(path,sizeof(path),"%s/theme",dir);
 assert(panel_theme_load()==3);assert(panel_theme_save("paper"));assert(panel_theme_load()==1);
 assert(!panel_theme_save("amber"));assert(!panel_theme_save("../../bad"));assert(panel_theme_load()==1);
 assert(panel_theme_save("seaglass"));assert(panel_theme_load()==3);
 assert(panel_theme_save("beacon"));assert(panel_theme_load()==3);
 assert(panel_theme_save("classic"));assert(panel_theme_load()==3);
 FILE*f=fopen(path,"w");fputs("paperjunk",f);fclose(f);assert(panel_theme_load()==3);
 assert(panel_theme_save("paper"));char tmp[512];snprintf(tmp,sizeof(tmp),"%s.next",path);assert(!symlink(path,tmp));assert(!panel_theme_save("classic"));assert(panel_theme_load()==1);unlink(tmp);
 cJSON*i=panel_theme_item(1),*choices=cJSON_GetObjectItem(i,"choices");assert(cJSON_GetArraySize(choices)==2);assert(!strcmp(cJSON_GetObjectItem(i,"value")->valuestring,"浅色"));assert(!strcmp(cJSON_GetObjectItem(i,"action")->valuestring,"screen.theme"));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(choices,0),"label")->valuestring,"深色"));assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(choices,0),"args"),"theme")->valuestring,"beacon"));
 assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(choices,1),"label")->valuestring,"浅色"));assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(choices,1),"args"),"theme")->valuestring,"paper"));cJSON_Delete(i);
 i=panel_theme_item(3);assert(!strcmp(cJSON_GetObjectItem(i,"value")->valuestring,"深色"));cJSON_Delete(i);
 unlink(path);rmdir(dir);puts("PASS: two theme choices, persistence, legacy-key migration, rejected unknown/path, dark fallback, failed write retains saved theme");
}
