#ifndef LITE
#include <stdint.h>
#include <set/set.h>
#include <mips.h>
#include "z2.h"
#include "state.h"
#include "zu.h"

void st_write(void **dst, void *src, size_t len){
    char *p = *dst;
    memcpy(p,src,len);
    p+=len;
    *dst = p;
}

void st_read(void **src, void *dst, size_t len){
    char *p = *src;
    memcpy(dst,p,len);
    p+=len;
    *src = p;
}

void st_skip(void **dest, size_t len){
    char *p = *dest;
    p += len;
    *dest = p;
}

_Bool addr_compare(void *a, void *b){
    uint32_t *a_32 = a;
    uint32_t *b_32 = b;
    return *a_32 < *b_32;
}

_Bool load_overlay(void **src, void **tab_addr, uint32_t vrom_start, uint32_t vrom_end, uint32_t vram_start, uint32_t vram_end){
    z2_file_table_t *file = NULL;
    for(int i=0;;i++){
        if(z2_file_table[i].vrom_start == vrom_start && z2_file_table[i].vrom_end == vrom_end){
            file = &z2_file_table[i];
            break;
        }
        if(z2_file_table[i].vrom_end == 0) return 0;
    }
    void *addr = NULL;
    st_read(src,&addr,sizeof(addr));
    if(*tab_addr != addr){
        *tab_addr = addr;
        z2_LoadOverlay(vrom_start,vrom_end,vram_start,vram_end,addr);
    }
    char *start = addr;
    char *end = start + (file->vrom_end - file->vrom_start);
    uint32_t *overlay_header_offset = (void*)(end - sizeof(*overlay_header_offset));
    if(overlay_header_offset == NULL){
        return 0;
    }
    z2_overlay_hdr_t *overlay_hdr = (void*)(end - *overlay_header_offset);
    char *data = start + overlay_hdr->text_size;
    char *bss = end;
    st_read(src,data,overlay_hdr->data_size);
    st_read(src,bss,overlay_hdr->bss_size);
    return 1;
}

void relocate_col_hdr(uint32_t hdr){
    z2_col_hdr_t *col_hdr = zu_segment_find(hdr);
    zu_segment_reloc(&col_hdr->vtx);
    zu_segment_reloc(&col_hdr->poly);
    zu_segment_reloc(&col_hdr->type);
    zu_segment_reloc(&col_hdr->camera);
    zu_segment_reloc(&col_hdr->water);
}

void load_state(void *state){
    void *p = state;

    osRecvMesg(&z2_ctxt.gfx->task_queue,NULL,OS_MESG_BLOCK);
    osSendMesg(&z2_ctxt.gfx->task_queue,NULL,OS_MESG_NOBLOCK);

    int scene_index = z2_game.scene_index;

    st_skip(&p,sizeof(kz_state_hdr_t));

    struct alloc{
        uint16_t    id;
        void       *ptr;
    };
    struct alloc objects[35];
    for(int i=0;i<35;i++){
        z2_obj_t *obj = &z2_game.obj_ctx.obj[i];
        objects[i].id = obj->id;
        objects[i].ptr = obj->data;
    }

    struct alloc rooms[2];
    for(int i=0;i<2;i++){
        z2_room_t *room = &z2_game.room_ctx.rooms[i];
        rooms[i].id = room->idx;
        rooms[i].ptr = room->file;
    }

    struct set ovl_set;
    set_init(&ovl_set,sizeof(uint32_t),addr_compare);

    int next_ent;
    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i=0;i<sizeof(z2_actor_ovl_table)/sizeof(*z2_actor_ovl_table);i++){
        z2_actor_ovl_table_t *ent = &z2_actor_ovl_table[i];
        if(i == next_ent){
            st_read(&p,&ent->loaded_cnt,sizeof(ent->loaded_cnt));
            load_overlay(&p,&ent->ram,ent->vrom_start,ent->vrom_end,ent->vram_start,ent->vram_end);
            st_read(&p,&next_ent,sizeof(next_ent));
            set_insert(&ovl_set,&ent->ram);
        }else{
            ent->ram = NULL;
            ent->loaded_cnt = 0;
        }
    }

    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i=0;i<sizeof(z2_player_ovl_table)/sizeof(*z2_player_ovl_table);i++){
        z2_player_ovl_table_t *ent = &z2_player_ovl_table[i];
        if(i == next_ent){
            load_overlay(&p,&ent->ram,ent->vrom_start,ent->vrom_end,ent->vram_start,ent->vram_end);
            st_read(&p,&next_ent,sizeof(next_ent));
            set_insert(&ovl_set,&ent->ram);
        }
    }

    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i=0;i<sizeof(z2_particle_ovl_table)/sizeof(*z2_particle_ovl_table);i++){
        z2_particle_ovl_table_t *ent = &z2_particle_ovl_table[i];
        if(i == next_ent){
            load_overlay(&p,&ent->ram,ent->vrom_start,ent->vrom_end,ent->vram_start,ent->vram_end);
            st_read(&p,&next_ent,sizeof(next_ent));
            set_insert(&ovl_set,&ent->ram);
        }
    }

    st_read(&p,&z2_game,sizeof(z2_game));
    st_read(&p,&z2_file,sizeof(z2_file));
    st_read(&p,&z2_static_ctxt,sizeof(z2_static_ctxt));
    st_read(&p,(void*)z2_disp_addr,Z2_DISP_SIZE);
    st_read(&p,(void*)(z2_disp_addr + Z2_DISP_SIZE),Z2_DISP_SIZE);
    st_read(&p,&z2_segment,sizeof(z2_segment));
    st_read(&p,&z2_game_arena,sizeof(z2_game_arena));
    z2_arena_node_t *node = z2_game_arena.first;
    st_read(&p,&next_ent,sizeof(next_ent));
    while(node){
        node->magic = 0x7373;
        st_read(&p,&node->free,sizeof(node->free));
        st_read(&p,&node->size,sizeof(node->size));
        char *data = node->data;
        if(set_get(&ovl_set,&data) == NULL && node->free == 0){
            st_read(&p,data,node->size);
        }
        if(node == z2_game_arena.first){
            node->prev = NULL;
        }
        st_read(&p,&next_ent,sizeof(next_ent));
        if(next_ent == 0){
            node->next = (void*)&node->data[node->size];
            node->next->prev = node;
        }else{
            node->next = NULL;
        }
        node = node->next;
    }

    st_read(&p,&z2_particle_info,sizeof(z2_particle_info));
    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i=0;i<z2_particle_info.part_max;i++){
        z2_particle_t *particle = &z2_particle_info.part_space[i];
        if(i == next_ent){
            st_read(&p,particle,sizeof(*particle));
            st_read(&p,&next_ent,sizeof(next_ent));
        }else{
            memset(particle,0,sizeof(*particle));
            particle->priority = 0x80;
            particle->id = 0x27;
            particle->time = -1;
        }
    }

    z2_static_particle_ctxt_t *spart_ctx = &z2_static_particle_ctxt;
    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i = 0;i<sizeof(spart_ctx->dots)/sizeof(*spart_ctx->dots);i++){
        z2_dot_t *dot = &spart_ctx->dots[i];
        if(i == next_ent){
            st_read(&p,dot,sizeof(*dot));
            st_read(&p,&next_ent,sizeof(next_ent));
        }else{
            dot->active = 0;
        }
    }

    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i = 0;i<sizeof(spart_ctx->trails)/sizeof(*spart_ctx->trails);i++){
        z2_trail_t *trail = &spart_ctx->trails[i];
        if(i == next_ent){
            st_read(&p,trail,sizeof(*trail));
            st_read(&p,&next_ent,sizeof(next_ent));
        }else{
            trail->active = 0;
        }
    }

    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i = 0;i<sizeof(spart_ctx->sparks)/sizeof(*spart_ctx->sparks);i++){
        z2_spark_t *spark = &spart_ctx->sparks[i];
        if(i == next_ent){
            st_read(&p,spark,sizeof(*spark));
            st_read(&p,&next_ent,sizeof(next_ent));
        }else{
            spark->active = 0;
        }
    }

    st_read(&p,&next_ent,sizeof(next_ent));
    for(int i = 0;i<sizeof(spart_ctx->unks)/sizeof(*spart_ctx->unks);i++){
        z2_unk_part_t *unk = &spart_ctx->unks[i];
        if(i == next_ent){
            st_read(&p,unk,sizeof(*unk));
            st_read(&p,&next_ent,sizeof(next_ent));
        }else{
            unk->active = 0;
        }
    }

    if(scene_index != z2_game.scene_index){
        z2_scene_table_ent_t *scene_ent = &z2_scene_table[z2_game.scene_index];
        size_t size = scene_ent->vrom_end - scene_ent->vrom_start;
        zu_file_get(scene_ent->vrom_start,z2_game.scene_addr,size);
        relocate_col_hdr((uint32_t)z2_game.col_ctxt.col_hdr);
        z2_game.col_ctxt.stc_list_pos = 0;
        z2_CreateStaticCollision(&z2_game.col_ctxt,&z2_game,z2_game.col_ctxt.stc_lut);
    }

    int trans_cnt = z2_game.room_ctx.transition_cnt;
    z2_room_trans_actor_t *trans = z2_game.room_ctx.transition_list;
    st_read(&p,trans,sizeof(*trans) * trans_cnt);

    for(int i=0;i<2;i++){
        struct alloc *room = &rooms[i];
        z2_room_t *c_room = &z2_game.room_ctx.rooms[i];
        int p_id = room->id;
        int c_id = c_room->idx;
        void *p_ptr = room->ptr;
        void *c_ptr = c_room->file;
        if(c_ptr && c_id != -1 && (p_id != c_id || z2_game.scene_index != scene_index || p_ptr != c_ptr)){
            uint32_t start = z2_game.room_list[c_id].vrom_start;
            uint32_t end = z2_game.room_list[c_id].vrom_end;
            zu_file_get(start, c_ptr, end - start);
        }
    }

    osSendMesg(&z2_file_msgqueue, &z2_game.room_ctx.loadfile, OS_MESG_NOBLOCK);

    for(int i = 0;i<35;i++){
        struct alloc *p_obj = &objects[i];
        z2_obj_t *c_obj = &z2_game.obj_ctx.obj[i];
        int p_id = p_obj->id;
        int c_id = c_obj->id;
        if(c_id<0){
            c_id = -c_id;
        }
        void *p_ptr = p_obj->ptr;
        void *c_ptr = c_obj->data;
        if(c_id != 0 && (c_id!=p_id || c_ptr!=p_ptr)){
            uint32_t start = z2_obj_table[c_id].vrom_start;
            uint32_t end = z2_obj_table[c_id].vrom_end;
            zu_file_get(start,c_ptr,end-start);
        }
        z2_segment.segments[6] = MIPS_KSEG0_TO_PHYS(c_ptr);
        switch(c_id){
            case 0x0197:
                relocate_col_hdr(0x060012B0);
                relocate_col_hdr(0x06001590);
                break;
            case 0x0C:
                relocate_col_hdr(0x060080E8);
                break;
            case 0x0205:
                relocate_col_hdr(0x06002D80);
                relocate_col_hdr(0x060026A0);
                break;
            case 0x01A4:
                relocate_col_hdr(0x06000968);
                break;
        }
    }

    set_destroy(&ovl_set);
}

_Bool save_overlay(void **dst, void *src, uint32_t vrom_start, uint32_t vrom_end){
    z2_file_table_t *file = NULL;
    for(int i=0;;i++){
        if(z2_file_table[i].vrom_start == vrom_start && z2_file_table[i].vrom_end == vrom_end){
            file = &z2_file_table[i];
            break;
        }
        if(z2_file_table[i].vrom_end == 0) return 0;
    }
    st_write(dst,&src,sizeof(src));
    char *start = src;
    char *end = start + (file->vrom_end - file->vrom_start);
    uint32_t *overlay_header_offset = (void*)(end - sizeof(*overlay_header_offset));
    if(overlay_header_offset == NULL){
        return 0;
    }
    z2_overlay_hdr_t *overlay_hdr = (void*)(end - *overlay_header_offset);
    char *data = start + overlay_hdr->text_size;
    char *bss = end;
    st_write(dst,data,overlay_hdr->data_size);
    st_write(dst,bss,overlay_hdr->bss_size);
    return 1;
}

size_t save_state(void *state){
    void *p = state;
    int ent_start = 0;
    int ent_end = -1;
    st_skip(&p,sizeof(kz_state_hdr_t));

    struct set node_set;
    set_init(&node_set,sizeof(uint32_t),addr_compare);
    for(int i=0;i<sizeof(z2_actor_ovl_table)/sizeof(*z2_actor_ovl_table);i++){
        z2_actor_ovl_table_t *ovl = &z2_actor_ovl_table[i];
        if(ovl->ram){
            st_write(&p,&i,sizeof(i));
            st_write(&p,&ovl->loaded_cnt,sizeof(ovl->loaded_cnt));
            save_overlay(&p,ovl->ram,ovl->vrom_start,ovl->vrom_end);
            set_insert(&node_set,&ovl->ram);
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    for(int i=0;i<sizeof(z2_player_ovl_table)/sizeof(*z2_player_ovl_table);i++){
        z2_player_ovl_table_t *ovl = &z2_player_ovl_table[i];
        if(ovl->ram){
            st_write(&p,&i,sizeof(i));
            save_overlay(&p,ovl->ram,ovl->vrom_start,ovl->vrom_end);
            set_insert(&node_set,&ovl->ram);
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    for(int i=0;i<sizeof(z2_particle_ovl_table)/sizeof(*z2_particle_ovl_table);i++){
        z2_particle_ovl_table_t *ovl = &z2_particle_ovl_table[i];
        if(ovl->ram){
            st_write(&p,&i,sizeof(i));
            save_overlay(&p,ovl->ram,ovl->vrom_start,ovl->vrom_end);
            set_insert(&node_set,&ovl->ram);
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    st_write(&p,&z2_game,sizeof(z2_game));
    st_write(&p,&z2_file,sizeof(z2_file));
    st_write(&p,&z2_static_ctxt,sizeof(z2_static_ctxt));
    st_write(&p,(void*)z2_disp_addr,Z2_DISP_SIZE);
    st_write(&p,(void*)(z2_disp_addr + Z2_DISP_SIZE),Z2_DISP_SIZE);
    st_write(&p,&z2_segment,sizeof(z2_segment));
    st_write(&p,&z2_game_arena,sizeof(z2_game_arena));
    for(z2_arena_node_t *node = z2_game_arena.first;node;node=node->next){
        st_write(&p,&ent_start,sizeof(ent_start));
        st_write(&p,&node->free,sizeof(node->free));
        st_write(&p,&node->size,sizeof(node->size));
        char *data = node->data;
        if(set_get(&node_set,&data) == NULL && node->free == 0){
            st_write(&p,data,node->size);
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));
    set_destroy(&node_set);

    st_write(&p,&z2_particle_info,sizeof(z2_particle_info));
    for(int i=0;i<z2_particle_info.part_max;i++){
        z2_particle_t *particle = &z2_particle_info.part_space[i];
        if(particle->time>=0){
            st_write(&p,&i,sizeof(i));
            st_write(&p,particle,sizeof(*particle));
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    z2_static_particle_ctxt_t *spart_ctx = &z2_static_particle_ctxt;
    for(int i=0;i<sizeof(spart_ctx->dots)/sizeof(*spart_ctx->dots);i++){
        z2_dot_t *dot = &spart_ctx->dots[i];
        if(dot->active){
            st_write(&p,&i,sizeof(i));
            st_write(&p,dot,sizeof(*dot));
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    for(int i=0;i<sizeof(spart_ctx->trails)/sizeof(*spart_ctx->trails);i++){
        z2_trail_t *trail = &spart_ctx->trails[i];
        if(trail->active){
            st_write(&p,&i,sizeof(i));
            st_write(&p,trail,sizeof(*trail));
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    for(int i=0;i<sizeof(spart_ctx->sparks)/sizeof(*spart_ctx->sparks);i++){
        z2_spark_t *spark = &spart_ctx->sparks[i];
        if(spark->active){
            st_write(&p,&i,sizeof(i));
            st_write(&p,spark,sizeof(*spark));
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    for(int i=0;i<sizeof(spart_ctx->unks)/sizeof(*spart_ctx->unks);i++){
        z2_unk_part_t *unk = &spart_ctx->unks[i];
        if(unk->active){
            st_write(&p,&i,sizeof(i));
            st_write(&p,unk,sizeof(*unk));
        }
    }
    st_write(&p,&ent_end,sizeof(ent_end));

    int trans_cnt = z2_game.room_ctx.transition_cnt;
    z2_room_trans_actor_t *trans = z2_game.room_ctx.transition_list;
    st_write(&p,trans,sizeof(*trans) * trans_cnt);
    
    return (char*)p - (char*)state;
}
#endif