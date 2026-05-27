#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "color_theory.h"

// ---------- Canvas / capacity (dynamic) ----------
#define MAX_CW         48
#define MAX_CH         48
#define MAX_LAYERS      4
#define MAX_FRAMES      4
#define MAX_HISTORY    12

#define PALETTE_SIZE   16
#define TRANSPARENT    0xFF

#define BOT_W 320
#define BOT_H 240
#define TOP_W 400
#define TOP_H 240
#define SAVE_DIR "sdmc:/3ds/pixelforge"

#define C(r,g,b) C2D_Color32(r,g,b,0xFF)
#define CA(r,g,b,a) C2D_Color32(r,g,b,a)

// ---------- Palettes ----------
typedef struct { const char *name; u32 colors[PALETTE_SIZE]; } PaletteDef;

static const PaletteDef PALETTES[] = {
    {"PICO-8", {
        0xFF000000,0xFF532B1D,0xFF53257E,0xFF518700,0xFF3652AB,0xFF4F575F,0xFFC7C3C2,0xFFE8F1FF,
        0xFF4D00FF,0xFF00A3FF,0xFF27ECFF,0xFF36E400,0xFFFFAD29,0xFF9C7683,0xFFA877FF,0xFFAACCFF}},
    {"NES", {
        0xFF000000,0xFF7C7C7C,0xFFBCBCBC,0xFFFCFCFC,0xFFA40000,0xFFF87858,0xFFFCA044,0xFFFCE0A8,
        0xFF005800,0xFF00B800,0xFF58F898,0xFF00A0FC,0xFF6888FC,0xFF7800F8,0xFFD800CC,0xFFF878F8}},
    {"GB DMG", {
        0xFF0F380F,0xFF306230,0xFF8BAC0F,0xFF9BBC0F,0xFF0F380F,0xFF306230,0xFF8BAC0F,0xFF9BBC0F,
        0xFF0F380F,0xFF306230,0xFF8BAC0F,0xFF9BBC0F,0xFF0F380F,0xFF306230,0xFF8BAC0F,0xFF9BBC0F}},
    {"GB Pocket", {
        0xFF000000,0xFF555555,0xFFAAAAAA,0xFFFFFFFF,0xFF000000,0xFF555555,0xFFAAAAAA,0xFFFFFFFF,
        0xFF000000,0xFF555555,0xFFAAAAAA,0xFFFFFFFF,0xFF000000,0xFF555555,0xFFAAAAAA,0xFFFFFFFF}},
    {"CGA", {
        0xFF000000,0xFFAA0000,0xFF00AA00,0xFFAAAA00,0xFF0000AA,0xFFAA00AA,0xFF0055AA,0xFFAAAAAA,
        0xFF555555,0xFFFF5555,0xFF55FF55,0xFFFFFF55,0xFF5555FF,0xFFFF55FF,0xFF55FFFF,0xFFFFFFFF}},
    {"C64", {
        0xFF000000,0xFFFFFFFF,0xFF2B3768,0xFFB2A470,0xFF863D6F,0xFF438D58,0xFF792835,0xFF6FC7B8,
        0xFF254F6F,0xFF003943,0xFF59679A,0xFF444444,0xFF6C6C6C,0xFF84D29A,0xFFB55E6C,0xFF959595}},
    {"ZX Spectrum", {
        0xFF000000,0xFFD80000,0xFF00D800,0xFFD8D800,0xFF0000D8,0xFFD800D8,0xFF00D8D8,0xFFD8D8D8,
        0xFF000000,0xFFFF0000,0xFF00FF00,0xFFFFFF00,0xFF0000FF,0xFFFF00FF,0xFF00FFFF,0xFFFFFFFF}},
    {"Apple II", {
        0xFF000000,0xFF722640,0xFF40337F,0xFFE434FE,0xFF0E5940,0xFF808080,0xFF1B9AFE,0xFFBFB3FF,
        0xFF404C00,0xFFE46501,0xFF808080,0xFFF1A6BF,0xFF1BCB01,0xFFBFCC80,0xFF8DD9BF,0xFFFFFFFF}},
    {"Sweetie 16", {
        0xFF1A1C2C,0xFF5D275D,0xFFB13E53,0xFFEF7D57,0xFFFFCD75,0xFFA7F070,0xFF38B764,0xFF257179,
        0xFF29366F,0xFF3B5DC9,0xFF41A6F6,0xFF73EFF7,0xFFF4F4F4,0xFF94B0C2,0xFF566C86,0xFF333C57}},
    {"Endesga 16", {
        0xFFE4A672,0xFFB86F50,0xFF743F39,0xFF3F2832,0xFF9E2835,0xFFE53B44,0xFFFB922B,0xFFFFE762,
        0xFF63C64D,0xFF327345,0xFF193D3F,0xFF4F6781,0xFFAFBFD2,0xFFFFFFFF,0xFF2CE8F4,0xFF0484D1}},
};
#define NUM_PALETTES ((int)(sizeof(PALETTES)/sizeof(PALETTES[0])))

static int current_palette_idx = 0;
#define PAL(i) (PALETTES[current_palette_idx].colors[i])

// ---------- Tools ----------
typedef enum {
    TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_PICKER, TOOL_LINE, TOOL_RECT,
    TOOL_SHADE, TOOL_LIGHT, TOOL_OUTLINE,
    NUM_TOOLS
} Tool;
static const char *TOOL_NAMES[NUM_TOOLS]  = {"Pencil","Eraser","Fill","Pick","Line","Rect","Shade","Light","Outline"};
static const char *TOOL_GLYPHS[NUM_TOOLS] = {"PN","ER","FL","PK","LN","RC","SH","LT","OL"};

// ---------- Data model ----------
typedef struct { u8 pixels[MAX_CH][MAX_CW]; } Frame;
typedef struct { Frame frames[MAX_FRAMES]; bool visible; char name[16]; } Layer;
typedef struct {
    Layer layers[MAX_LAYERS];
    u8 cw, ch;
    u8 num_layers, num_frames, current_layer, current_frame;
} Project;

static Project proj;
static Project past[MAX_HISTORY];
static int past_count = 0;
static Project future[MAX_HISTORY];
static int future_count = 0;
static Project pre_stroke;

// ---------- State ----------
static Tool current_tool = TOOL_PENCIL;
static u8   current_color = 0;
static int  cursor_x = 8, cursor_y = 8;
static bool show_grid = true, show_onion = false, playing = false;
static int  playback_frame = 0, fps = 8;
static u64  next_frame_at = 0;

static bool was_touching = false, drawing = false;
static int  last_px = -1, last_py = -1;
static int  shape_x0 = -1, shape_y0 = -1, shape_x1 = -1, shape_y1 = -1;

static char flash_msg[64] = "";
static u64  flash_until = 0;
static C2D_TextBuf text_buf;

typedef enum { SCREEN_SPLASH, SCREEN_EDIT, SCREEN_SETTINGS } ScreenMode;
static ScreenMode screen = SCREEN_SPLASH;

// ---------- Layout helpers (recomputed for canvas size) ----------
static int PIXEL_SIZE   = 7;
static int CANVAS_X     = 72;
static int CANVAS_Y     = 8;
static int CANVAS_W_PX  = 224;
static int CANVAS_H_PX  = 224;

static void recompute_layout(void) {
    // Fit canvas in 224x224 region centered at x=72
    int maxw = 224, maxh = 224;
    int psx = maxw / proj.cw;
    int psy = maxh / proj.ch;
    PIXEL_SIZE = psx < psy ? psx : psy;
    if (PIXEL_SIZE < 1) PIXEL_SIZE = 1;
    CANVAS_W_PX = proj.cw * PIXEL_SIZE;
    CANVAS_H_PX = proj.ch * PIXEL_SIZE;
    CANVAS_X = 72 + (224 - CANVAS_W_PX) / 2;
    CANVAS_Y = 8 + (224 - CANVAS_H_PX) / 2;
}

// ---------- Util ----------
static u64 now_ms(void) { return osGetTime(); }
static void flash(const char *m) { strncpy(flash_msg,m,63); flash_msg[63]=0; flash_until=now_ms()+1800; }

static Frame *cur_frame(void) { return &proj.layers[proj.current_layer].frames[proj.current_frame]; }
static u8 get_pixel(int x,int y){ if(x<0||x>=proj.cw||y<0||y>=proj.ch) return TRANSPARENT; return cur_frame()->pixels[y][x]; }
static void set_pixel(int x,int y,u8 v){ if(x<0||x>=proj.cw||y<0||y>=proj.ch) return; cur_frame()->pixels[y][x]=v; }

static u8 composite_at(int fi,int x,int y){
    u8 r=TRANSPARENT;
    for(int li=0;li<proj.num_layers;li++){
        if(!proj.layers[li].visible) continue;
        u8 v=proj.layers[li].frames[fi].pixels[y][x];
        if(v!=TRANSPARENT) r=v;
    }
    return r;
}

// ---------- Color theory glue ----------
// The const PALETTES[] are stored as ABGR8888 (citro2d). The color_theory
// module works in 0xRRGGBB. These helpers bridge the two and snap any
// computed color back to the nearest entry in the active palette, since
// the palette here is fixed and not growable.
static inline u32 abgr_to_rgb_hex(u32 abgr) {
    u8 r =  abgr        & 0xFF;
    u8 g = (abgr >> 8)  & 0xFF;
    u8 b = (abgr >> 16) & 0xFF;
    return ((u32)r << 16) | ((u32)g << 8) | b;
}

static u8 snap_to_palette(u32 target_rgb_hex) {
    int tr = (target_rgb_hex >> 16) & 0xFF;
    int tg = (target_rgb_hex >> 8)  & 0xFF;
    int tb =  target_rgb_hex        & 0xFF;
    int best = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < PALETTE_SIZE; i++) {
        u32 p = PAL(i);
        int pr =  p        & 0xFF;
        int pg = (p >> 8)  & 0xFF;
        int pb = (p >> 16) & 0xFF;
        int dr = tr - pr, dg = tg - pg, db = tb - pb;
        int d  = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return (u8)best;
}

// Shade a single cell, reading the BASE value from the pre-stroke snapshot
// so dragging across the same cell repeatedly doesn't accumulate steps.
static void smart_shade_cell(int x, int y) {
    if (x<0||x>=proj.cw||y<0||y>=proj.ch) return;
    u8 idx = pre_stroke.layers[proj.current_layer]
                       .frames[proj.current_frame]
                       .pixels[y][x];
    if (idx == TRANSPARENT) return;
    u32 cur = abgr_to_rgb_hex(PAL(idx));
    u32 shaded = ct_shade_once(cur, 1.0f);
    set_pixel(x, y, snap_to_palette(shaded));
}
static void smart_light_cell(int x, int y) {
    if (x<0||x>=proj.cw||y<0||y>=proj.ch) return;
    u8 idx = pre_stroke.layers[proj.current_layer]
                       .frames[proj.current_frame]
                       .pixels[y][x];
    if (idx == TRANSPARENT) return;
    u32 cur = abgr_to_rgb_hex(PAL(idx));
    u32 lit = ct_highlight_once(cur, 1.0f);
    set_pixel(x, y, snap_to_palette(lit));
}

// Walk a Bresenham line between two cells and call fn on each.
static void smart_walk(int x0,int y0,int x1,int y1, void(*fn)(int,int)){
    int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1;
    int err=dx-dy,x=x0,y=y0;
    for(;;){
        fn(x,y);
        if(x==x1&&y==y1) break;
        int e2=2*err;
        if(e2>-dy){err-=dy;x+=sx;}
        if(e2<dx){err+=dx;y+=sy;}
    }
}

// Auto-outline: each transparent cell adjacent to a colored cell becomes
// the shaded version of its darkest colored neighbor, snapped to palette.
static void apply_auto_outline(void){
    Frame *f = cur_frame();
    static u8 snap[MAX_CH][MAX_CW];
    for(int y=0;y<proj.ch;y++)
        for(int x=0;x<proj.cw;x++)
            snap[y][x] = f->pixels[y][x];
    static const int DX[4]={-1,1,0,0};
    static const int DY[4]={0,0,-1,1};
    for(int y=0;y<proj.ch;y++){
        for(int x=0;x<proj.cw;x++){
            if(snap[y][x] != TRANSPARENT) continue;
            u32 darkest_rgb = 0;
            float darkest_l = 1000.0f;
            bool found = false;
            for(int k=0;k<4;k++){
                int nx=x+DX[k], ny=y+DY[k];
                if(nx<0||ny<0||nx>=proj.cw||ny>=proj.ch) continue;
                u8 ni = snap[ny][nx];
                if(ni == TRANSPARENT) continue;
                u32 rgb = abgr_to_rgb_hex(PAL(ni));
                HSL hsl = ct_rgb_to_hsl(ct_hex_to_rgb(rgb));
                if(!found || hsl.l < darkest_l){
                    darkest_l = hsl.l;
                    darkest_rgb = rgb;
                    found = true;
                }
            }
            if(!found) continue;
            u32 outline = ct_shade_once(darkest_rgb, 1.6f);
            f->pixels[y][x] = snap_to_palette(outline);
        }
    }
}

// ---------- History ----------
static void push_history(const Project *s){
    if(past_count==MAX_HISTORY){ memmove(&past[0],&past[1],sizeof(Project)*(MAX_HISTORY-1)); past_count=MAX_HISTORY-1; }
    past[past_count++]=*s; future_count=0;
}
static void do_undo(void){ if(!past_count){flash("Nothing to undo");return;} if(future_count<MAX_HISTORY) future[future_count++]=proj; proj=past[--past_count]; flash("Undo"); }
static void do_redo(void){ if(!future_count){flash("Nothing to redo");return;} if(past_count<MAX_HISTORY) past[past_count++]=proj; proj=future[--future_count]; flash("Redo"); }

// ---------- Init ----------
static void init_project(int w, int h){
    memset(&proj,TRANSPARENT,sizeof(proj));
    proj.cw=w; proj.ch=h;
    proj.num_layers=1; proj.num_frames=1; proj.current_layer=0; proj.current_frame=0;
    for(int li=0;li<MAX_LAYERS;li++){
        proj.layers[li].visible=(li==0);
        snprintf(proj.layers[li].name,16,"Layer %d",li+1);
    }
    past_count=0; future_count=0;
    cursor_x=proj.cw/2; cursor_y=proj.ch/2;
    recompute_layout();
}

// ---------- Layer / frame ops ----------
static void add_layer(void){
    if(proj.num_layers>=MAX_LAYERS){flash("Max layers");return;}
    push_history(&proj);
    int i=proj.num_layers++;
    proj.layers[i].visible=true;
    snprintf(proj.layers[i].name,16,"Layer %d",i+1);
    for(int fi=0;fi<MAX_FRAMES;fi++) memset(proj.layers[i].frames[fi].pixels,TRANSPARENT,MAX_CW*MAX_CH);
    proj.current_layer=i; flash("Layer added");
}
static void delete_layer(void){
    if(proj.num_layers<=1){flash("Need 1 layer");return;}
    push_history(&proj);
    int i=proj.current_layer;
    for(int li=i;li<proj.num_layers-1;li++) proj.layers[li]=proj.layers[li+1];
    memset(&proj.layers[proj.num_layers-1],TRANSPARENT,sizeof(Layer));
    proj.layers[proj.num_layers-1].visible=false;
    proj.num_layers--;
    if(proj.current_layer>=proj.num_layers) proj.current_layer=proj.num_layers-1;
    flash("Layer deleted");
}
static void next_layer(void){ if(proj.current_layer<proj.num_layers-1){proj.current_layer++;flash(proj.layers[proj.current_layer].name);} }
static void prev_layer(void){ if(proj.current_layer>0){proj.current_layer--;flash(proj.layers[proj.current_layer].name);} }
static void toggle_layer_vis(void){ proj.layers[proj.current_layer].visible=!proj.layers[proj.current_layer].visible; flash(proj.layers[proj.current_layer].visible?"Layer shown":"Layer hidden"); }

static void add_frame(void){
    if(proj.num_frames>=MAX_FRAMES){flash("Max frames");return;}
    push_history(&proj);
    int i=proj.num_frames++;
    for(int li=0;li<proj.num_layers;li++) memset(proj.layers[li].frames[i].pixels,TRANSPARENT,MAX_CW*MAX_CH);
    proj.current_frame=i; flash("Frame added");
}
static void duplicate_frame(void){
    if(proj.num_frames>=MAX_FRAMES){flash("Max frames");return;}
    push_history(&proj);
    int s=proj.current_frame, d=proj.num_frames++;
    for(int li=0;li<proj.num_layers;li++) proj.layers[li].frames[d]=proj.layers[li].frames[s];
    proj.current_frame=d; flash("Frame duplicated");
}
static void next_frame(void){ if(proj.current_frame<proj.num_frames-1) proj.current_frame++; }
static void prev_frame(void){ if(proj.current_frame>0) proj.current_frame--; }

static void next_palette(void){
    current_palette_idx=(current_palette_idx+1)%NUM_PALETTES;
    flash(PALETTES[current_palette_idx].name);
}
static void prev_palette(void){
    current_palette_idx=(current_palette_idx-1+NUM_PALETTES)%NUM_PALETTES;
    flash(PALETTES[current_palette_idx].name);
}

// ---------- Drawing primitives ----------
static void draw_line_px(int x0,int y0,int x1,int y1,u8 v){
    int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy,x=x0,y=y0;
    for(;;){
        set_pixel(x,y,v);
        if(x==x1&&y==y1) break;
        int e2=2*err;
        if(e2>-dy){err-=dy;x+=sx;}
        if(e2<dx){err+=dx;y+=sy;}
    }
}
static void draw_rect_outline(int x0,int y0,int x1,int y1,u8 v){
    int xa=x0<x1?x0:x1,xb=x0<x1?x1:x0,ya=y0<y1?y0:y1,yb=y0<y1?y1:y0;
    for(int x=xa;x<=xb;x++){set_pixel(x,ya,v);set_pixel(x,yb,v);}
    for(int y=ya;y<=yb;y++){set_pixel(xa,y,v);set_pixel(xb,y,v);}
}
static void flood_fill(int x,int y,u8 t,u8 r){
    if(t==r) return;
    if(get_pixel(x,y)!=t) return;
    static int sx[MAX_CW*MAX_CH],sy[MAX_CW*MAX_CH];
    Frame *f=cur_frame();
    int sp=0;
    sx[sp]=x; sy[sp]=y; sp++;
    while(sp>0){
        sp--; int cx=sx[sp],cy=sy[sp];
        if(cx<0||cx>=proj.cw||cy<0||cy>=proj.ch) continue;
        if(f->pixels[cy][cx]!=t) continue;
        f->pixels[cy][cx]=r;
        if(sp+4<=MAX_CW*MAX_CH){ sx[sp]=cx+1;sy[sp]=cy;sp++; sx[sp]=cx-1;sy[sp]=cy;sp++; sx[sp]=cx;sy[sp]=cy+1;sp++; sx[sp]=cx;sy[sp]=cy-1;sp++; }
    }
}

// ---------- Save/load ----------
static void ensure_save_dir(void){ mkdir("sdmc:/3ds",0777); mkdir(SAVE_DIR,0777); }

static bool save_bmp_sheet(const char *p){
    FILE *f=fopen(p,"wb"); if(!f) return false;
    int W=proj.cw*proj.num_frames, H=proj.ch;
    int row=((W*3+3)/4)*4, img=row*H, sz=54+img;
    u8 hdr[54]={'B','M',sz,sz>>8,sz>>16,sz>>24,0,0,0,0,54,0,0,0,40,0,0,0,
        (u8)(W&0xFF),(u8)((W>>8)&0xFF),(u8)((W>>16)&0xFF),(u8)((W>>24)&0xFF),
        (u8)(H&0xFF),(u8)((H>>8)&0xFF),0,0,1,0,24,0,0,0,0,0,img,img>>8,img>>16,img>>24,
        0x13,0x0B,0,0,0x13,0x0B,0,0,0,0,0,0,0,0,0,0};
    fwrite(hdr,1,54,f);
    for(int y=H-1;y>=0;y--){
        int w=0;
        for(int fi=0;fi<proj.num_frames;fi++){
            for(int x=0;x<proj.cw;x++){
                u8 i=composite_at(fi,x,y);
                u32 c=(i==TRANSPARENT)?0xFFFF00FF:PAL(i);
                fputc((c>>16)&0xFF,f); fputc((c>>8)&0xFF,f); fputc(c&0xFF,f); w+=3;
            }
        }
        for(;w<row;w++) fputc(0,f);
    }
    fclose(f); return true;
}

static bool save_native(const char *p){
    FILE *f=fopen(p,"wb"); if(!f) return false;
    const char m[8]={'P','X','F','R','G',3,0,0};
    fwrite(m,1,8,f);
    u16 w=proj.cw,h=proj.ch; fwrite(&w,2,1,f); fwrite(&h,2,1,f);
    u8 pi=(u8)current_palette_idx; fwrite(&pi,1,1,f);
    u8 hdr[4]={proj.num_layers,proj.num_frames,proj.current_layer,proj.current_frame};
    fwrite(hdr,1,4,f);
    for(int li=0;li<proj.num_layers;li++){
        fwrite(proj.layers[li].name,1,16,f);
        u8 v=proj.layers[li].visible?1:0; fwrite(&v,1,1,f);
        for(int fi=0;fi<proj.num_frames;fi++)
            for(int y=0;y<proj.ch;y++)
                fwrite(proj.layers[li].frames[fi].pixels[y],1,proj.cw,f);
    }
    fclose(f); return true;
}

static bool load_native(const char *p){
    FILE *f=fopen(p,"rb"); if(!f) return false;
    char m[8];
    if(fread(m,1,8,f)!=8||memcmp(m,"PXFRG",5)!=0||m[5]!=3){fclose(f);return false;}
    u16 w,h;
    if(fread(&w,2,1,f)!=1||fread(&h,2,1,f)!=1){fclose(f);return false;}
    if(w==0||w>MAX_CW||h==0||h>MAX_CH){fclose(f);return false;}
    u8 pi; if(fread(&pi,1,1,f)!=1){fclose(f);return false;}
    if(pi>=NUM_PALETTES) pi=0;
    u8 hdr[4]; if(fread(hdr,1,4,f)!=4){fclose(f);return false;}
    if(hdr[0]==0||hdr[0]>MAX_LAYERS||hdr[1]==0||hdr[1]>MAX_FRAMES){fclose(f);return false;}
    Project t; memset(&t,TRANSPARENT,sizeof(t));
    t.cw=w; t.ch=h;
    t.num_layers=hdr[0]; t.num_frames=hdr[1];
    t.current_layer=hdr[2]<t.num_layers?hdr[2]:0;
    t.current_frame=hdr[3]<t.num_frames?hdr[3]:0;
    for(int li=0;li<MAX_LAYERS;li++) t.layers[li].visible=false;
    for(int li=0;li<t.num_layers;li++){
        if(fread(t.layers[li].name,1,16,f)!=16){fclose(f);return false;}
        u8 v; if(fread(&v,1,1,f)!=1){fclose(f);return false;}
        t.layers[li].visible=v!=0;
        for(int fi=0;fi<t.num_frames;fi++)
            for(int y=0;y<t.ch;y++)
                if(fread(t.layers[li].frames[fi].pixels[y],1,t.cw,f)!=(size_t)t.cw){fclose(f);return false;}
    }
    fclose(f);
    push_history(&proj);
    proj=t;
    current_palette_idx=pi;
    recompute_layout();
    return true;
}

static void do_save(void){
    ensure_save_dir();
    char b[128],x[128]; time_t t=time(NULL); struct tm *tm=localtime(&t); char s[32];
    strftime(s,32,"%Y%m%d_%H%M%S",tm);
    snprintf(b,128,SAVE_DIR "/sheet_%s.bmp",s);
    snprintf(x,128,SAVE_DIR "/sprite_%s.pxf",s);
    bool a=save_bmp_sheet(b), c=save_native(x);
    flash((a&&c)?"Saved sheet + project":"Save failed");
}
static void do_quick_save(void){ ensure_save_dir(); flash(save_native(SAVE_DIR "/latest.pxf")?"Quick-saved":"Quick-save failed"); }
static void do_quick_load(void){ flash(load_native(SAVE_DIR "/latest.pxf")?"Loaded latest":"No save found"); }

// ---------- Hit regions (edit screen) ----------
typedef struct { int x,y,w,h; } Rect;
static const Rect TR[NUM_TOOLS]={
    {4,2,20,24},{26,2,20,24},{48,2,20,24},
    {4,28,20,24},{26,28,20,24},{48,28,20,24},
    {4,54,20,24},{26,54,20,24},{48,54,20,24}
};
static const Rect LP={4,84,14,14},LL={20,84,28,14},LN={50,84,14,14};
static const Rect LA={4,100,28,18},LV={34,100,14,18},LD={50,100,14,18};
static const Rect FP={4,122,14,14},FL_={20,122,28,14},FN={50,122,14,14};
static const Rect FA={4,138,14,18},FD={20,138,14,18},FY={36,138,14,18},FO={52,138,12,18};
static const Rect BU={4,160,30,20},BR={36,160,30,20};
static const Rect BG={4,184,30,20},BC={36,184,30,20};
static const Rect BS={4,208,30,20},BL={36,208,30,20};
static const Rect PR={302,8,14,224};
static const Rect PALPREV={68,228,16,10}, PALNEXT={252,228,16,10}, PALMID={90,226,156,14};
static const Rect SETBTN={290,228,28,10};

static bool hit(const Rect *r,int x,int y){return x>=r->x&&x<r->x+r->w&&y>=r->y&&y<r->y+r->h;}

// ---------- Drawing helpers ----------
static void draw_text(const char *s,float x,float y,float sc,u32 c){
    C2D_Text t; C2D_TextParse(&t,text_buf,s); C2D_TextOptimize(&t);
    C2D_DrawText(&t,C2D_WithColor,x,y,0,sc,sc,c);
}
static void draw_checker(float x,float y,float w,float h,u32 a,u32 b,float cs){
    int cols=(int)(w/cs)+1,rows=(int)(h/cs)+1;
    for(int yi=0;yi<rows;yi++) for(int xi=0;xi<cols;xi++){
        float cx=x+xi*cs,cy=y+yi*cs;
        float cw=(cx+cs>x+w)?(x+w-cx):cs, ch=(cy+cs>y+h)?(y+h-cy):cs;
        if(cw>0&&ch>0) C2D_DrawRectSolid(cx,cy,0,cw,ch,((xi+yi)&1)?b:a);
    }
}
static void draw_composite(float x,float y,float sc,int fi){
    draw_checker(x,y,proj.cw*sc,proj.ch*sc,C(0x1F,0x1F,0x23),C(0x28,0x28,0x2D),sc*2.0f);
    for(int py=0;py<proj.ch;py++) for(int px=0;px<proj.cw;px++){
        u8 v=composite_at(fi,px,py);
        if(v!=TRANSPARENT) C2D_DrawRectSolid(x+px*sc,y+py*sc,0,sc,sc,PAL(v));
    }
}
static void draw_composite_alpha(float x,float y,float sc,int fi,u8 a){
    for(int py=0;py<proj.ch;py++) for(int px=0;px<proj.cw;px++){
        u8 v=composite_at(fi,px,py);
        if(v!=TRANSPARENT){ u32 c=(PAL(v)&0x00FFFFFF)|((u32)a<<24); C2D_DrawRectSolid(x+px*sc,y+py*sc,0,sc,sc,c); }
    }
}
static void draw_button(const Rect *r,const char *lbl,u32 bg,u32 fg,float ts){
    C2D_DrawRectSolid(r->x,r->y,0,r->w,r->h,bg);
    float tx=r->x+(r->w-(float)strlen(lbl)*7.0f*ts)/2.0f;
    float ty=r->y+(r->h-14.0f*ts)/2.0f;
    if(tx<r->x+2) tx=r->x+2;
    if(ty<r->y+2) ty=r->y+2;
    draw_text(lbl,tx,ty,ts,fg);
}

// ---------- Actions ----------
static void apply_at_pixel(int px,int py,bool start){
    if(start) pre_stroke=proj;
    switch(current_tool){
        case TOOL_PENCIL:
            if(start) set_pixel(px,py,current_color);
            else if(last_px!=px||last_py!=py) draw_line_px(last_px,last_py,px,py,current_color);
            drawing=true; break;
        case TOOL_ERASER:
            if(start) set_pixel(px,py,TRANSPARENT);
            else if(last_px!=px||last_py!=py) draw_line_px(last_px,last_py,px,py,TRANSPARENT);
            drawing=true; break;
        case TOOL_FILL:
            if(start){ u8 t=get_pixel(px,py); if(t!=current_color){push_history(&proj); flood_fill(px,py,t,current_color);} }
            break;
        case TOOL_PICKER:
            if(start){ u8 c=composite_at(proj.current_frame,px,py); if(c!=TRANSPARENT) current_color=c; }
            break;
        case TOOL_LINE: case TOOL_RECT:
            if(start){shape_x0=px;shape_y0=py;} shape_x1=px; shape_y1=py; break;
        case TOOL_SHADE:
            if(start){ smart_shade_cell(px,py); }
            else if(last_px!=px||last_py!=py){ smart_walk(last_px,last_py,px,py,smart_shade_cell); }
            drawing=true; break;
        case TOOL_LIGHT:
            if(start){ smart_light_cell(px,py); }
            else if(last_px!=px||last_py!=py){ smart_walk(last_px,last_py,px,py,smart_light_cell); }
            drawing=true; break;
        case TOOL_OUTLINE:
            if(start){ push_history(&proj); apply_auto_outline(); }
            break;
        default: break;
    }
    last_px=px; last_py=py; cursor_x=px; cursor_y=py;
}

static void end_stroke(void){
    if(drawing){ push_history(&pre_stroke); drawing=false; }
    if((current_tool==TOOL_LINE||current_tool==TOOL_RECT)&&shape_x0>=0){
        push_history(&proj);
        if(current_tool==TOOL_LINE) draw_line_px(shape_x0,shape_y0,shape_x1,shape_y1,current_color);
        else draw_rect_outline(shape_x0,shape_y0,shape_x1,shape_y1,current_color);
        shape_x0=shape_y0=shape_x1=shape_y1=-1;
    }
    last_px=last_py=-1;
}

static void toggle_play(void){
    playing=!playing;
    if(playing){ playback_frame=0; next_frame_at=now_ms()+(1000/fps); flash("Playing"); }
    else flash("Stopped");
}

// ---------- Splash screen ----------
static const Rect SPLASH_16={70,80,80,40}, SPLASH_32={170,80,80,40}, SPLASH_48={270,80,60,40};
static const Rect SPLASH_LOAD={120,140,180,30};

static void handle_splash(u32 kHeld) {
    static bool was = false;
    bool now = (kHeld & KEY_TOUCH) != 0;
    if (now && !was) {
        touchPosition t; hidTouchRead(&t);
        if (hit(&SPLASH_16, t.px, t.py))      { init_project(16,16); screen=SCREEN_EDIT; flash("New 16x16"); }
        else if (hit(&SPLASH_32, t.px, t.py)) { init_project(32,32); screen=SCREEN_EDIT; flash("New 32x32"); }
        else if (hit(&SPLASH_48, t.px, t.py)) { init_project(48,48); screen=SCREEN_EDIT; flash("New 48x48"); }
        else if (hit(&SPLASH_LOAD, t.px, t.py)) {
            init_project(32,32);
            if (load_native(SAVE_DIR "/latest.pxf")) screen=SCREEN_EDIT;
            else { flash("No save found"); }
            screen=SCREEN_EDIT;
        }
    }
    was = now;
}

static void render_splash_top(void) {
    C2D_DrawRectSolid(0,0,0,TOP_W,TOP_H,C(0x12,0x10,0x18));
    draw_text("PIXELFORGE",100,60,1.2f,C(0xFF,0xC0,0xFF));
    draw_text("3DS Pixel Art Editor",110,110,0.55f,C(0xC0,0xC0,0xD0));
    draw_text("Built with citro2d",125,140,0.45f,C(0x80,0x80,0x90));
    draw_text("Tap a size below to begin",95,180,0.55f,C(0xE0,0xE0,0xE8));
}
static void render_splash_bot(void) {
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,C(0x18,0x16,0x1F));
    draw_text("Choose canvas size",90,40,0.6f,C(0xC0,0xD0,0xFF));
    draw_button(&SPLASH_16,"16x16",C(0x33,0x55,0x88),C(0xFF,0xFF,0xFF),0.6f);
    draw_button(&SPLASH_32,"32x32",C(0x55,0x33,0x88),C(0xFF,0xFF,0xFF),0.6f);
    draw_button(&SPLASH_48,"48x48",C(0x88,0x33,0x55),C(0xFF,0xFF,0xFF),0.6f);
    draw_button(&SPLASH_LOAD,"Load last save",C(0x33,0x66,0x33),C(0xE0,0xFF,0xE0),0.55f);
    draw_text("START = quit",110,210,0.45f,C(0x70,0x70,0x78));
}

// ---------- Settings screen ----------
static const Rect SET_16={40,40,80,40}, SET_32={120,40,80,40}, SET_48={200,40,80,40};
static const Rect SET_BACK={20,200,80,30}, SET_NEW={220,200,80,30};

static void handle_settings(u32 kHeld) {
    static bool was=false;
    bool now=(kHeld&KEY_TOUCH)!=0;
    if(now && !was){
        touchPosition t; hidTouchRead(&t);
        if(hit(&SET_16,t.px,t.py)){ init_project(16,16); flash("Reset to 16x16"); screen=SCREEN_EDIT; }
        else if(hit(&SET_32,t.px,t.py)){ init_project(32,32); flash("Reset to 32x32"); screen=SCREEN_EDIT; }
        else if(hit(&SET_48,t.px,t.py)){ init_project(48,48); flash("Reset to 48x48"); screen=SCREEN_EDIT; }
        else if(hit(&SET_BACK,t.px,t.py)){ screen=SCREEN_EDIT; }
        else if(hit(&SET_NEW,t.px,t.py)){ init_project(proj.cw,proj.ch); flash("Cleared"); screen=SCREEN_EDIT; }
    }
    was=now;
}

static void render_settings_top(void){
    C2D_DrawRectSolid(0,0,0,TOP_W,TOP_H,C(0x12,0x10,0x18));
    draw_text("Settings",160,30,1.0f,C(0xFF,0xC0,0xFF));
    char b[64];
    snprintf(b,64,"Current canvas: %d x %d",proj.cw,proj.ch);
    draw_text(b,100,90,0.55f,C(0xC0,0xC0,0xD0));
    snprintf(b,64,"Palette: %s",PALETTES[current_palette_idx].name);
    draw_text(b,100,110,0.55f,C(0xC0,0xC0,0xD0));
    snprintf(b,64,"Layers used: %d / %d",proj.num_layers,MAX_LAYERS);
    draw_text(b,100,130,0.5f,C(0xA0,0xA0,0xA8));
    snprintf(b,64,"Frames used: %d / %d",proj.num_frames,MAX_FRAMES);
    draw_text(b,100,150,0.5f,C(0xA0,0xA0,0xA8));
    draw_text("Warning: changing size clears artwork",70,190,0.5f,C(0xFF,0xA0,0xA0));
}
static void render_settings_bot(void){
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,C(0x18,0x16,0x1F));
    draw_text("Choose new canvas size (clears work):",30,15,0.5f,C(0xC0,0xD0,0xFF));
    draw_button(&SET_16,"16x16",C(0x33,0x55,0x88),C(0xFF,0xFF,0xFF),0.6f);
    draw_button(&SET_32,"32x32",C(0x55,0x33,0x88),C(0xFF,0xFF,0xFF),0.6f);
    draw_button(&SET_48,"48x48",C(0x88,0x33,0x55),C(0xFF,0xFF,0xFF),0.6f);
    draw_text("Or:",30,100,0.5f,C(0xA0,0xA0,0xA8));
    draw_button(&SET_NEW,"Clear current",C(0x66,0x33,0x33),C(0xFF,0xC0,0xC0),0.5f);
    draw_button(&SET_BACK,"< Back",C(0x33,0x33,0x55),C(0xC0,0xC0,0xFF),0.55f);
}

// ---------- Edit screen ----------
static void handle_touch_edit(u32 kHeld,touchPosition t){
    bool now_t=(kHeld&KEY_TOUCH)!=0;
    Rect CR={CANVAS_X,CANVAS_Y,CANVAS_W_PX,CANVAS_H_PX};
    if(now_t&&!was_touching){
        int tx=t.px,ty=t.py;
        if(hit(&CR,tx,ty)){ apply_at_pixel((tx-CANVAS_X)/PIXEL_SIZE,(ty-CANVAS_Y)/PIXEL_SIZE,true); }
        else {
            for(int i=0;i<NUM_TOOLS;i++) if(hit(&TR[i],tx,ty)){ current_tool=(Tool)i; flash(TOOL_NAMES[i]); goto done; }
            if(hit(&LP,tx,ty)) prev_layer();
            else if(hit(&LN,tx,ty)) next_layer();
            else if(hit(&LA,tx,ty)) add_layer();
            else if(hit(&LV,tx,ty)) toggle_layer_vis();
            else if(hit(&LD,tx,ty)) delete_layer();
            else if(hit(&FP,tx,ty)) prev_frame();
            else if(hit(&FN,tx,ty)) next_frame();
            else if(hit(&FA,tx,ty)) add_frame();
            else if(hit(&FD,tx,ty)) duplicate_frame();
            else if(hit(&FY,tx,ty)) toggle_play();
            else if(hit(&FO,tx,ty)){ show_onion=!show_onion; flash(show_onion?"Onion on":"Onion off"); }
            else if(hit(&BU,tx,ty)) do_undo();
            else if(hit(&BR,tx,ty)) do_redo();
            else if(hit(&BG,tx,ty)){ show_grid=!show_grid; flash(show_grid?"Grid on":"Grid off"); }
            else if(hit(&BC,tx,ty)){ push_history(&proj); memset(cur_frame()->pixels,TRANSPARENT,MAX_CW*MAX_CH); flash("Cleared"); }
            else if(hit(&BS,tx,ty)) do_save();
            else if(hit(&BL,tx,ty)) do_quick_load();
            else if(hit(&PALPREV,t.px,t.py)) prev_palette();
            else if(hit(&PALNEXT,t.px,t.py)) next_palette();
            else if(hit(&SETBTN,t.px,t.py)) { screen=SCREEN_SETTINGS; }
            else for(int i=0;i<PALETTE_SIZE;i++){ Rect r={PR.x,PR.y+i*14,PR.w,14}; if(hit(&r,tx,ty)){current_color=i;flash("Color set");break;} }
            done: ;
        }
    } else if(now_t&&was_touching){
        if(hit(&CR,t.px,t.py)) apply_at_pixel((t.px-CANVAS_X)/PIXEL_SIZE,(t.py-CANVAS_Y)/PIXEL_SIZE,false);
    } else if(!now_t&&was_touching){ end_stroke(); }
    was_touching=now_t;
}

static void handle_buttons_edit(u32 kDown,u32 kHeld){
    if(kHeld&KEY_SELECT){
        if(kDown&KEY_DUP) prev_layer();
        if(kDown&KEY_DDOWN) next_layer();
        if(kDown&KEY_DLEFT) prev_frame();
        if(kDown&KEY_DRIGHT) next_frame();
    } else {
        if(kDown&KEY_DUP) cursor_y=(cursor_y-1+proj.ch)%proj.ch;
        if(kDown&KEY_DDOWN) cursor_y=(cursor_y+1)%proj.ch;
        if(kDown&KEY_DLEFT) cursor_x=(cursor_x-1+proj.cw)%proj.cw;
        if(kDown&KEY_DRIGHT) cursor_x=(cursor_x+1)%proj.cw;
    }
    if(kDown&KEY_A){ push_history(&proj); set_pixel(cursor_x,cursor_y,current_color); }
    if(kDown&KEY_B){ push_history(&proj); set_pixel(cursor_x,cursor_y,TRANSPARENT); }
    if(kDown&KEY_X) do_undo();
    if(kDown&KEY_Y) do_redo();
    if(kDown&KEY_L) current_color=(current_color+PALETTE_SIZE-1)%PALETTE_SIZE;
    if(kDown&KEY_R) current_color=(current_color+1)%PALETTE_SIZE;
    if(kDown&KEY_ZL) prev_palette();
    if(kDown&KEY_ZR) next_palette();
    if((kDown&KEY_SELECT)&&!(kHeld&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT))) do_quick_save();
}

static void render_bottom_edit(void){
    for(int i=0;i<NUM_TOOLS;i++){
        bool s=(current_tool==(Tool)i);
        draw_button(&TR[i],TOOL_GLYPHS[i],s?C(0x55,0x22,0x66):C(0x2A,0x2A,0x2F),s?C(0xFF,0xC0,0xFF):C(0xA0,0xA0,0xA8),0.5f);
    }
    draw_button(&LP,"<",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.5f);
    { char b[8]; snprintf(b,8,"L%d",proj.current_layer+1);
      bool v=proj.layers[proj.current_layer].visible;
      draw_button(&LL,b,v?C(0x22,0x33,0x55):C(0x22,0x22,0x2A),v?C(0xC0,0xD0,0xFF):C(0x70,0x70,0x78),0.5f); }
    draw_button(&LN,">",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.5f);
    draw_button(&LA,"+L",C(0x22,0x55,0x33),C(0xC0,0xFF,0xC8),0.5f);
    { bool v=proj.layers[proj.current_layer].visible;
      draw_button(&LV,v?"V":"v",v?C(0x22,0x55,0x66):C(0x55,0x33,0x22),C(0xE0,0xF0,0xFF),0.6f); }
    draw_button(&LD,"X",C(0x55,0x22,0x22),C(0xFF,0xC0,0xC0),0.6f);
    draw_button(&FP,"<",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.5f);
    { char b[16]; snprintf(b,16,"F%d/%d",proj.current_frame+1,proj.num_frames);
      draw_button(&FL_,b,C(0x33,0x33,0x44),C(0xE0,0xE0,0xFF),0.4f); }
    draw_button(&FN,">",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.5f);
    draw_button(&FA,"+F",C(0x22,0x55,0x33),C(0xC0,0xFF,0xC8),0.5f);
    draw_button(&FD,"D",C(0x33,0x44,0x66),C(0xC0,0xD0,0xFF),0.6f);
    draw_button(&FY,playing?"S":"P",playing?C(0x66,0x33,0x22):C(0x33,0x55,0x33),C(0xFF,0xE0,0xC0),0.6f);
    draw_button(&FO,"O",show_onion?C(0x22,0x55,0x66):C(0x2A,0x2A,0x2F),C(0xC0,0xF0,0xFF),0.6f);
    draw_button(&BU,"UN",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.55f);
    draw_button(&BR,"RE",C(0x2A,0x2A,0x2F),C(0xC0,0xC0,0xC8),0.55f);
    draw_button(&BG,"GR",show_grid?C(0x22,0x55,0x66):C(0x2A,0x2A,0x2F),show_grid?C(0xC0,0xF0,0xFF):C(0xA0,0xA0,0xA8),0.55f);
    draw_button(&BC,"CL",C(0x55,0x22,0x22),C(0xFF,0xC0,0xC0),0.55f);
    draw_button(&BS,"SV",C(0x22,0x55,0x33),C(0xC0,0xFF,0xC8),0.55f);
    draw_button(&BL,"LD",C(0x33,0x44,0x66),C(0xC0,0xD0,0xFF),0.55f);

    // Canvas
    draw_checker(CANVAS_X,CANVAS_Y,CANVAS_W_PX,CANVAS_H_PX,C(0x1F,0x1F,0x23),C(0x28,0x28,0x2D),PIXEL_SIZE*2.0f);
    if(show_onion&&proj.current_frame>0) draw_composite_alpha(CANVAS_X,CANVAS_Y,PIXEL_SIZE,proj.current_frame-1,0x60);
    for(int py=0;py<proj.ch;py++) for(int px=0;px<proj.cw;px++){
        u8 v=composite_at(proj.current_frame,px,py);
        if(v!=TRANSPARENT) C2D_DrawRectSolid(CANVAS_X+px*PIXEL_SIZE,CANVAS_Y+py*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(v));
    }
    if((current_tool==TOOL_LINE||current_tool==TOOL_RECT)&&shape_x0>=0){
        if(current_tool==TOOL_LINE){
            int dx=abs(shape_x1-shape_x0),dy=abs(shape_y1-shape_y0);
            int sx=shape_x0<shape_x1?1:-1,sy=shape_y0<shape_y1?1:-1,err=dx-dy,x=shape_x0,y=shape_y0;
            for(;;){
                C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(current_color));
                if(x==shape_x1&&y==shape_y1) break;
                int e2=2*err;
                if(e2>-dy){err-=dy;x+=sx;}
                if(e2<dx){err+=dx;y+=sy;}
            }
        } else {
            int xa=shape_x0<shape_x1?shape_x0:shape_x1,xb=shape_x0<shape_x1?shape_x1:shape_x0;
            int ya=shape_y0<shape_y1?shape_y0:shape_y1,yb=shape_y0<shape_y1?shape_y1:shape_y0;
            for(int x=xa;x<=xb;x++){
                C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+ya*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(current_color));
                C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+yb*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(current_color));
            }
            for(int y=ya;y<=yb;y++){
                C2D_DrawRectSolid(CANVAS_X+xa*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(current_color));
                C2D_DrawRectSolid(CANVAS_X+xb*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PAL(current_color));
            }
        }
    }
    if(show_grid && PIXEL_SIZE>=4){
        u32 g=CA(0xFF,0xFF,0xFF,0x14);
        for(int i=1;i<proj.cw;i++) C2D_DrawRectSolid(CANVAS_X+i*PIXEL_SIZE,CANVAS_Y,0,1,CANVAS_H_PX,g);
        for(int j=1;j<proj.ch;j++) C2D_DrawRectSolid(CANVAS_X,CANVAS_Y+j*PIXEL_SIZE,0,CANVAS_W_PX,1,g);
    }
    { float cx=CANVAS_X+cursor_x*PIXEL_SIZE,cy=CANVAS_Y+cursor_y*PIXEL_SIZE; u32 h=CA(0xFF,0xFF,0x80,0xC0);
      C2D_DrawRectSolid(cx,cy,0,PIXEL_SIZE,1,h); C2D_DrawRectSolid(cx,cy+PIXEL_SIZE-1,0,PIXEL_SIZE,1,h);
      C2D_DrawRectSolid(cx,cy,0,1,PIXEL_SIZE,h); C2D_DrawRectSolid(cx+PIXEL_SIZE-1,cy,0,1,PIXEL_SIZE,h); }
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y-1,0,CANVAS_W_PX+2,1,b);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y+CANVAS_H_PX,0,CANVAS_W_PX+2,1,b);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y,0,1,CANVAS_H_PX,b);
      C2D_DrawRectSolid(CANVAS_X+CANVAS_W_PX,CANVAS_Y,0,1,CANVAS_H_PX,b); }

    // Right palette swatches
    for(int i=0;i<PALETTE_SIZE;i++){
        float y=PR.y+i*14;
        C2D_DrawRectSolid(PR.x,y,0,PR.w,13,PAL(i));
        if(i==current_color){
            u32 h=C(0xFF,0xFF,0xFF);
            C2D_DrawRectSolid(PR.x-2,y-1,0,PR.w+4,1,h);
            C2D_DrawRectSolid(PR.x-2,y+13,0,PR.w+4,1,h);
            C2D_DrawRectSolid(PR.x-2,y-1,0,1,15,h);
            C2D_DrawRectSolid(PR.x+PR.w+1,y-1,0,1,15,h);
        }
    }

    // Bottom strip: palette selector + settings
    C2D_DrawRectSolid(0,224,0,BOT_W,16,C(0x10,0x10,0x18));
    draw_button(&PALPREV,"<",C(0x33,0x33,0x44),C(0xC0,0xC0,0xFF),0.5f);
    { char b[24]; snprintf(b,24,"%s",PALETTES[current_palette_idx].name);
      draw_text(b,PALMID.x+12,PALMID.y+2,0.5f,C(0xE0,0xE0,0xFF)); }
    draw_button(&PALNEXT,">",C(0x33,0x33,0x44),C(0xC0,0xC0,0xFF),0.5f);
    draw_button(&SETBTN,"SET",C(0x33,0x44,0x33),C(0xC0,0xFF,0xC0),0.5f);
}

static void render_top_edit(void){
    C2D_DrawRectSolid(0,0,0,TOP_W,18,C(0x16,0x16,0x1B));
    draw_text("PixelForge 3DS",8,4,0.55f,C(0xE0,0xC0,0xFF));
    { char h[80]; snprintf(h,80,"%dx%d  L%d/%d  F%d/%d  %s",
        proj.cw,proj.ch,proj.current_layer+1,proj.num_layers,
        proj.current_frame+1,proj.num_frames,playing?"PLAY":"STOP");
      draw_text(h,200,4,0.45f,C(0xC0,0xC0,0xD0)); }

    { int lx=8,ly=24,lw=160,lh=22;
      draw_text("LAYERS",lx,ly-12,0.4f,C(0x80,0x80,0x90));
      for(int i=0;i<MAX_LAYERS;i++){
          int ry=ly+i*lh; bool cur=(i==proj.current_layer); bool act=(i<proj.num_layers);
          u32 bg=!act?C(0x10,0x10,0x14):cur?C(0x55,0x22,0x66):C(0x22,0x22,0x28);
          C2D_DrawRectSolid(lx,ry,0,lw,lh-2,bg);
          if(act){
              bool v=proj.layers[i].visible;
              C2D_DrawRectSolid(lx+4,ry+4,0,12,12,v?C(0x44,0xCC,0x66):C(0x44,0x44,0x4A));
              u32 fg=cur?C(0xFF,0xE0,0xFF):v?C(0xD0,0xD0,0xD8):C(0x80,0x80,0x88);
              draw_text(proj.layers[i].name,lx+22,ry+5,0.5f,fg);
          }
      } }

    // ---- Smart ramp (color theory) ----
    { int rx=8, ry=118, sw=28, sh=22, sg=3;
      draw_text("SMART RAMP",rx,ry-12,0.4f,C(0x80,0x80,0x90));
      u32 base_rgb = abgr_to_rgb_hex(PAL(current_color));
      u32 ramp[5]; ct_build_ramp(base_rgb, ramp);
      for(int i=0;i<5;i++){
          int sx_ = rx + i*(sw+sg);
          u32 col = ct_c2d_color(ramp[i]);
          C2D_DrawRectSolid(sx_,ry,0,sw,sh,col);
          if(i==2){ // BASE highlighted with amber border
              u32 b=C(0xFF,0xC0,0x40);
              C2D_DrawRectSolid(sx_-1,ry-1,0,sw+2,1,b);
              C2D_DrawRectSolid(sx_-1,ry+sh,0,sw+2,1,b);
              C2D_DrawRectSolid(sx_-1,ry,0,1,sh,b);
              C2D_DrawRectSolid(sx_+sw,ry,0,1,sh,b);
          }
          // Show snap-target index below each non-base swatch
          if(i!=2){
              u8 si = snap_to_palette(ramp[i]);
              char tb[8]; snprintf(tb,8,">%d",si);
              draw_text(tb,sx_+4,ry+sh+1,0.35f,C(0x90,0x90,0x98));
          } else {
              draw_text("BASE",sx_+2,ry+sh+1,0.35f,C(0xFF,0xC0,0x40));
          }
      } }

    int p4x=180,p4y=24, pf=playing?playback_frame:proj.current_frame;
    int p4s = 128 / proj.cw; if(p4s<1) p4s=1;
    draw_composite(p4x,p4y,(float)p4s,pf);
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(p4x-1,p4y-1,0,proj.cw*p4s+2,1,b);
      C2D_DrawRectSolid(p4x-1,p4y+proj.ch*p4s,0,proj.cw*p4s+2,1,b);
      C2D_DrawRectSolid(p4x-1,p4y,0,1,proj.ch*p4s,b);
      C2D_DrawRectSolid(p4x+proj.cw*p4s,p4y,0,1,proj.ch*p4s,b);
      char b2[24]; snprintf(b2,24,"%dx  F%d",p4s,pf+1);
      draw_text(b2,p4x,p4y+proj.ch*p4s+4,0.45f,C(0x80,0x80,0x88)); }

    // Smaller 1x preview
    int p1x=320,p1y=24;
    draw_composite(p1x,p1y,1.0f,pf);
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(p1x-1,p1y-1,0,proj.cw+2,1,b);
      C2D_DrawRectSolid(p1x-1,p1y+proj.ch,0,proj.cw+2,1,b);
      C2D_DrawRectSolid(p1x-1,p1y,0,1,proj.ch,b);
      C2D_DrawRectSolid(p1x+proj.cw,p1y,0,1,proj.ch,b);
      draw_text("1x",p1x,p1y+proj.ch+2,0.4f,C(0x80,0x80,0x88)); }

    // Frame thumbnails
    { int fx=8,fy=150,fs=16,gap=4;
      draw_text("FRAMES",fx,fy-12,0.4f,C(0x80,0x80,0x90));
      for(int fi=0;fi<proj.num_frames;fi++){
          int tx=fx+fi*(fs+gap),ty=fy;
          int step = proj.cw/fs; if(step<1) step=1;
          int draws = proj.cw/step;
          float pix = (float)fs/(float)draws;
          for(int py=0;py<draws;py++) for(int px=0;px<draws;px++){
              u8 v=composite_at(fi,px*step,py*step);
              if(v!=TRANSPARENT) C2D_DrawRectSolid(tx+px*pix,ty+py*pix,0,pix+1,pix+1,PAL(v));
              else { u32 ck=((px+py)&1)?C(0x28,0x28,0x2D):C(0x1F,0x1F,0x23); C2D_DrawRectSolid(tx+px*pix,ty+py*pix,0,pix+1,pix+1,ck); }
          }
          if(fi==proj.current_frame){ u32 h=C(0xFF,0xC0,0xFF);
              C2D_DrawRectSolid(tx-1,ty-1,0,fs+2,1,h); C2D_DrawRectSolid(tx-1,ty+fs,0,fs+2,1,h);
              C2D_DrawRectSolid(tx-1,ty,0,1,fs,h); C2D_DrawRectSolid(tx+fs,ty,0,1,fs,h);
          } else if(playing&&fi==playback_frame){ u32 h=C(0xC0,0xFF,0xC0);
              C2D_DrawRectSolid(tx-1,ty-1,0,fs+2,1,h); C2D_DrawRectSolid(tx-1,ty+fs,0,fs+2,1,h);
              C2D_DrawRectSolid(tx-1,ty,0,1,fs,h); C2D_DrawRectSolid(tx+fs,ty,0,1,fs,h);
          }
      } }

    // Status bar
    { int sy=175;
      C2D_DrawRectSolid(0,sy,0,TOP_W,TOP_H-sy,C(0x12,0x12,0x16));
      char b[80];
      snprintf(b,80,"%s | color %d | cursor %d,%d",TOOL_NAMES[current_tool],current_color,cursor_x,cursor_y);
      draw_text(b,8,sy+4,0.45f,C(0xE0,0xE0,0xE8));
      C2D_DrawRectSolid(290,sy+4,0,16,12,PAL(current_color));
      snprintf(b,80,"Palette: %s  Hist %d/%d  Onion %s  Grid %s",
        PALETTES[current_palette_idx].name,past_count,future_count,
        show_onion?"on":"off",show_grid?"on":"off");
      draw_text(b,8,sy+18,0.4f,C(0xA0,0xA0,0xA8));
      draw_text("Stylus paints  DPad cursor  A paint  B erase  X undo  Y redo",8,sy+34,0.4f,C(0x70,0x70,0x78));
      draw_text("L/R color  ZL/ZR palette  SELECT+DPad layer/frame  START quit",8,sy+46,0.4f,C(0x70,0x70,0x78));
      draw_text("SH/LT tap pixel to shade/light  OL taps once = outline whole canvas",8,sy+58,0.4f,C(0x70,0x70,0x78)); }

    if(now_ms()<flash_until&&flash_msg[0]){
        float w=(float)strlen(flash_msg)*8.0f+16; float x=(TOP_W-w)/2;
        C2D_DrawRectSolid(x,22,0,w,18,C(0x44,0x22,0x66));
        draw_text(flash_msg,x+8,25,0.55f,C(0xFF,0xE0,0xFF));
    }
}

// ---------- Main ----------
int main(int argc,char **argv){
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    C3D_RenderTarget *bot=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    text_buf=C2D_TextBufNew(4096);

    init_project(32,32);
    screen = SCREEN_SPLASH;

    while(aptMainLoop()){
        hidScanInput();
        u32 kDown=hidKeysDown(),kHeld=hidKeysHeld();
        touchPosition t; hidTouchRead(&t);
        if(kDown&KEY_START) break;

        if(screen==SCREEN_SPLASH) handle_splash(kHeld);
        else if(screen==SCREEN_SETTINGS) handle_settings(kHeld);
        else {
            handle_buttons_edit(kDown,kHeld);
            handle_touch_edit(kHeld,t);
            if(playing&&now_ms()>=next_frame_at){
                playback_frame=(playback_frame+1)%proj.num_frames;
                next_frame_at=now_ms()+(1000/fps);
            }
        }

        C2D_TextBufClear(text_buf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top,C(0x09,0x09,0x0B)); C2D_SceneBegin(top);
        if(screen==SCREEN_SPLASH) render_splash_top();
        else if(screen==SCREEN_SETTINGS) render_settings_top();
        else render_top_edit();

        C2D_TargetClear(bot,C(0x18,0x18,0x1C)); C2D_SceneBegin(bot);
        if(screen==SCREEN_SPLASH) render_splash_bot();
        else if(screen==SCREEN_SETTINGS) render_settings_bot();
        else render_bottom_edit();

        C3D_FrameEnd(0);
    }

    C2D_TextBufDelete(text_buf);
    C2D_Fini(); C3D_Fini(); gfxExit();
    return 0;
}
