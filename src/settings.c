#include <stdlib.h>
#include "settings.h"
#include "io.h"
#include "kz.h"
#include "input.h"

static _Alignas(128) struct settings settings_info;
struct settings_data *settings = &settings_info.data;

void load_default_settings(){
    settings_info.header.magic[0] = 'k';
    settings_info.header.magic[1] = 'z';
    settings_info.header.magic[2] = 'k';
    settings_info.header.magic[3] = 'z';
    settings_info.header.version = SETTINGS_VER;
    list_destroy(&kz.watches);
    list_init(&kz.watches,sizeof(watch_t));
    settings->binds[Z2_CMD_TOGGLE_MENU] = make_bind(2, BUTTON_R, BUTTON_L);
    settings->binds[Z2_CMD_LEVITATE] = make_bind(1, BUTTON_L);
    settings->binds[Z2_CMD_TURBO] = make_bind(1, BUTTON_D_LEFT);
    settings->binds[Z2_CMD_VOID] = make_bind(2, BUTTON_D_LEFT, BUTTON_A);
    settings->binds[Z2_CMD_BREAK] = make_bind(2, BUTTON_D_RIGHT, BUTTON_L);
    settings->binds[Z2_CMD_PAUSE] = make_bind(1, BUTTON_D_UP);
    settings->binds[Z2_CMD_ADVANCE] = make_bind(1, BUTTON_D_DOWN);
    settings->binds[Z2_CMD_RETURN] = make_bind(2, BUTTON_L, BUTTON_D_RIGHT);
    settings->binds[Z2_CMD_RESET_LAG] = make_bind(1,BUTTON_D_RIGHT);
    settings->binds[Z2_CMD_TIMER] = make_bind(2,BUTTON_A, BUTTON_D_DOWN);
    settings->binds[Z2_CMD_TIMER_RESET] = make_bind(2,BUTTON_A, BUTTON_D_UP);
    settings->input_display = 1;
    settings->id_x = 16;
    settings->id_y = Z2_SCREEN_HEIGHT - 16;
    settings->lag_counter = 0;
    settings->lag_x = Z2_SCREEN_WIDTH - 38;
    settings->lag_y = 20;
    settings->timer = 0;
    settings->timer_x = Z2_SCREEN_WIDTH - 100;
    settings->timer_y = 30;
}

void save_settings_to_flashram(int profile){
    settings->watch_cnt = kz.watches.size;
    int i=0;
    for(watch_t *watch = kz.watches.first;watch;watch = list_next(watch)){
        settings->watch_addresses[i] = (uint32_t)watch->address;
        settings->watch_x[i] = watch->x;
        settings->watch_y[i] = watch->y;
        settings->watch_info[i].floating = watch->floating;
        settings->watch_info[i].type = watch->type;
        i++;
    }
    kz_io(&settings_info,SETTINGS_ADDR + (profile * sizeof(settings_info)),sizeof(*settings),OS_WRITE);
}

void load_settings_from_flashram(int profile){
    struct settings settings_temp;
    kz_io(&settings_temp,SETTINGS_ADDR + (profile * sizeof(settings_temp)),sizeof(settings_temp),OS_READ);
    if(settings_temp.header.magic[0] == 'k' && settings_temp.header.magic[1]=='z' && settings_temp.header.magic[2] == 'k' && settings_temp.header.magic[3] == 'z'){
        memcpy((void*)&settings_info,(void*)&settings_temp,sizeof(settings_temp));
    }else{
        load_default_settings();
    }
}

void kz_apply_settings(){
    for(int i=0;i<settings->watch_cnt;i++){
        watch_t *watch = list_push_back(&kz.watches,NULL);
        watch->address = (void*)settings->watch_addresses[i];
        watch->x = settings->watch_x[i];
        watch->y = settings->watch_y[i];
        watch->floating = settings->watch_info[i].floating;
        watch->type = settings->watch_info[i].type;
    }
}
