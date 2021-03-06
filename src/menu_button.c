#include <stdlib.h>

#include "menu.h"

struct item_data{
    menu_generic_callback   callback;
    void                   *callback_data;
};

struct gfx_item_data{
    menu_generic_callback   callback;
    void                   *callback_data;
    draw_info_t             draw_info;
};

static void button_activate(struct menu_item *item){
    struct item_data *data = item->data;
    if(data->callback){
        data->callback(item,MENU_CALLBACK_ACTIVATE,data->callback_data);
    }
}

static void draw_gfx_button(struct menu_item *item){
    struct gfx_item_data *data = item->data;
    z2_rgba32_t color = data->draw_info.enabled_color;
    if(item->owner->selected_item == item){
        color = MENU_SELECTED_COLOR;
    }
    gfx_draw_sprite_scale_color(data->draw_info.texture,get_item_x_pos(item),get_item_y_pos(item),data->draw_info.on_tile,data->draw_info.width,data->draw_info.height,data->draw_info.x_scale,data->draw_info.y_scale,color.color);
}

static void button_update(struct menu_item *item){
    struct item_data *data = item->data;
    if(data->callback){
        data->callback(item,MENU_CALLBACK_UPDATE,data->callback_data);
    }
}

void menu_button_set_tile(struct menu_item *item, int tile){
    struct gfx_item_data *data = item->data;
    data->draw_info.on_tile = tile;
}

void menu_button_cb_data_set(struct menu_item *item, void *data){
    struct item_data *idata = item->data;
    if(idata){
        idata->callback_data = data;
    }
}

struct menu_item *menu_add_button(struct menu *menu, uint16_t x, uint16_t y, char *name, menu_generic_callback callback, void *data){
    struct menu_item *item = menu_add(menu,x,y,name);
    if(item){
        struct item_data *idata = malloc(sizeof(*idata));
        idata->callback = callback;
        idata->callback_data = data;
        item->activate_proc = button_activate;
        item->update_proc = button_update;
        item->data = idata;
        item->interactive = 1;
    }
    return item;
}

struct menu_item *menu_add_gfx_button(struct menu *menu, uint16_t x, uint16_t y, menu_generic_callback callback, void *data, draw_info_t *drawinfo){
    struct menu_item *item = menu_add(menu,x,y,NULL);
    if(item){
        struct gfx_item_data *idata = malloc(sizeof(*idata));
        idata->callback = callback;
        idata->callback_data = data;
        memcpy(&idata->draw_info,drawinfo,sizeof(*drawinfo));
        item->data = data;
        item->draw_proc = draw_gfx_button;
        item->activate_proc = button_activate;
        item->update_proc = button_update;
        item->interactive = 1;
        item->data = idata;

    }
    return item;
}