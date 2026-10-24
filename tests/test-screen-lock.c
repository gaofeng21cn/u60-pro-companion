#include <assert.h>
#include <stdio.h>
#include "cJSON.h"
#include "../panel/panel-shell-model.h"

int main(void){
 struct panel_shell s={0};
 panel_screen_lock_initial(&s,1);assert(s.locked);
 panel_screen_lock_unlock(&s);assert(!s.locked);
 panel_screen_lock_wake(&s,0,1);assert(!s.locked);
 panel_screen_lock_wake(&s,1,1);assert(s.locked);
 panel_screen_lock_wake(&s,1,0);assert(s.locked);
 panel_screen_lock_initial(&s,0);assert(!s.locked);
 panel_screen_lock_wake(&s,1,0);assert(!s.locked);
 puts("PASS: supported themes start and wake into the visual lock; unlocking and unsupported themes stay explicit");
 return 0;
}
