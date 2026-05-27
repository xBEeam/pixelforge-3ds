#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define CW            32
#define CH            32
#define PIXEL_SIZE     7
#define CANVAS_X      72
#define CANVAS_Y       8
#define CANVAS_W      (CW * PIXEL_SIZE)
#define CANVAS_H      (CH * PIXEL_SIZE)

#define PALETTE_SIZE  16
#define TRANSPARENT   0xFF
#define MAX_LAYERS     4
#define MAX_FRAMES     8
#define MAX_HISTORY   24

#define BOT_W 320
#define BOT_H 240
#define TOP_W 400
#define TOP_H 240

#define SAVE_DIR "sdmc:/3ds/pixelforge"

#define C(r,g,b) C2D_Color32(r,g,b,0xFF)
#define CA(r,g,b,a) C2D_Color32(r,g,b,a)

static const u32 PALETTE[PALETTE_SIZE] = {
    C(0x00,0x00,0x00), C(0x1D,0x2B,0x53), C(0x7E,0x25,0x53), C(0x00,0x87,0x51),
    C(0xAB,0x52,0x36), C(0x5F,0x57,0x4F), C(0xC2,0xC3,0xC7), C(0xFF,0xF1,0xE8),
    C(0xFF,0x00,0x4D), C(0xFF,0xA3,0x00), C(0xFF,0xEC,0x27), C(0x00,0xE4,0x36),
    C(0x29,0xAD,0xFF), C(0x83,0x76,0x9C), C(0xFF,0x77,0xA8), C(0xFF,0xCC,0xAA),
};

typedef enum {
    TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_PICKER, TOOL_LINE, TOOL_RECT,
    NUM_TOOLS
} Tool;

static const char *TOOL_NAMES[NUM_TOOLS] = { "Pencil","Eraser","Fill","Pick","Line","Rect" };
static const char *TOOL_GLYPHS[NUM_TOOLS] = { "PN","ER","FL","PK","LN","RC" };

typedef struct { u8 pixels[CH][CW]; } Frame;
typedef struct { Frame frames[MAX_FRAMES]; bool visible; char name[16]; } Layer;
typedef struct { Layer layers[MAX_LAYERS]; u8 num_layers, num_frames, current_layer, current_frame; } Project;

static Project proj;
static Project past[MAX_HISTORY];
static int     past_count = 0;
static Project future[MAX_HISTORY];
static int     future_count = 0;

static Tool  current_tool  = TOOL_PENCIL;
static u8    current_color = 0;
static int   cursor_x = CW/2, cursor_y = CH/2;
static bool  show_grid = true, show_onion = false, playing = false;
static int   playback_frame = 0, fps = 8;
static u64   next_frame_at = 0;

static bool    was_touching = false, drawing = false;
static int     last_px = -1, last_py = -1;
static int     shape_x0 = -1, shape_y0 = -1, shape_x1 = -1, shape_y1 = -1;
static Project pre_stroke;

static char  flash_msg[64] = "";
static u64   flash_until   = 0;
static C2D_TextBuf  text_buf;

static u64 now_ms(void) { return osGetTime(); }
static void flash(const char *m) { strncpy(flash_msg,m,63); flash_msg[63]=0; flash_until=now_ms()+1800; }

static Frame *cur_frame(void) { return &proj.layers[proj.current_layer].frames[proj.current_frame]; }
static u8 get_pixel(int x,int y){ if(x<0||x>=CW||y<0||y>=CH) return TRANSPARENT; return cur_frame()->pixels[y][x]; }
static void set_pixel(int x,int y,u8 v){ if(x<0||x>=CW||y<0||y>=CH) return; cur_frame()->pixels[y][x]=v; }

static u8 composite_at(int fi,int x,int y){
    u8 r=TRANSPARENT;
    for(int li=0;li<proj.num_layers;li++){
        if(!proj.layers[li].visible) continue;
        u8 v=proj.layers[li].frames[fi].pixels[y][x];
        if(v!=TRANSPARENT) r=v;
    }
    return r;
}

static void push_history(const Project *s){
    if(past_count==MAX_HISTORY){ memmove(&past[0],&past[1],sizeof(Project)*(MAX_HISTORY-1)); past_count=MAX_HISTORY-1; }
    past[past_count++]=*s; future_count=0;
}
static void do_undo(void){ if(!past_count){flash("Nothing to undo");return;} if(future_count<MAX_HISTORY) future[future_count++]=proj; proj=past[--past_count]; flash("Undo"); }
static void do_redo(void){ if(!future_count){flash("Nothing to redo");return;} if(past_count<MAX_HISTORY) past[past_count++]=proj; proj=future[--future_count]; flash("Redo"); }

static void init_project(void){
    memset(&proj,TRANSPARENT,sizeof(proj));
    proj.num_layers=1; proj.num_frames=1; proj.current_layer=0; proj.current_frame=0;
    for(int li=0;li<MAX_LAYERS;li++){
        proj.layers[li].visible=(li==0);
        snprintf(proj.layers[li].name,16,"Layer %d",li+1);
    }
}

static void add_layer(void){
    if(proj.num_layers>=MAX_LAYERS){flash("Max layers");return;}
    push_history(&proj);
    int i=proj.num_layers++;
    proj.layers[i].visible=true;
    snprintf(proj.layers[i].name,16,"Layer %d",i+1);
    for(int fi=0;fi<MAX_FRAMES;fi++) memset(proj.layers[i].frames[fi].pixels,TRANSPARENT,CW*CH);
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
    for(int li=0;li<proj.num_layers;li++) memset(proj.layers[li].frames[i].pixels,TRANSPARENT,CW*CH);
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

static void draw_line_px(int x0,int y0,int x1,int y1,u8 v){
    int dx=abs(x1-x0),dy=abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy,x=x0,y=y0;
    for(;;){ set_pixel(x,y,v); if(x==x1&&y==y1) break; int e2=2*err; if(e2>-dy){err-=dy;x+=sx;} if(e2<dx){err+=dx;y+=sy;} }
}
static void draw_rect_outline(int x0,int y0,int x1,int y1,u8 v){
    int xa=x0<x1?x0:x1,xb=x0<x1?x1:x0,ya=y0<y1?y0:y1,yb=y0<y1?y1:y0;
    for(int x=xa;x<=xb;x++){set_pixel(x,ya,v);set_pixel(x,yb,v);}
    for(int y=ya;y<=yb;y++){set_pixel(xa,y,v);set_pixel(xb,y,v);}
}
static void flood_fill(int x,int y,u8 t,u8 r){
    if(t==r) return; if(get_pixel(x,y)!=t) return;
    static int sx[CW*CH],sy[CW*CH]; Frame *f=cur_frame(); int sp=0;
    sx[sp]=x; sy[sp]=y; sp++;
    while(sp>0){
        sp--; int cx=sx[sp],cy=sy[sp];
        if(cx<0||cx>=CW||cy<0||cy>=CH) continue;
        if(f->pixels[cy][cx]!=t) continue;
        f->pixels[cy][cx]=r;
        if(sp+4<=CW*CH){ sx[sp]=cx+1;sy[sp]=cy;sp++; sx[sp]=cx-1;sy[sp]=cy;sp++; sx[sp]=cx;sy[sp]=cy+1;sp++; sx[sp]=cx;sy[sp]=cy-1;sp++; }
    }
}

static void ensure_save_dir(void){ mkdir("sdmc:/3ds",0777); mkdir(SAVE_DIR,0777); }

static bool save_bmp_sheet(const char *p){
    FILE *f=fopen(p,"wb"); if(!f) return false;
    int W=CW*proj.num_frames, H=CH;
    int row=((W*3+3)/4)*4, img=row*H, sz=54+img;
    u8 hdr[54]={'B','M',sz,sz>>8,sz>>16,sz>>24,0,0,0,0,54,0,0,0,40,0,0,0,
        (u8)(W&0xFF),(u8)((W>>8)&0xFF),(u8)((W>>16)&0xFF),(u8)((W>>24)&0xFF),
        (u8)(H&0xFF),(u8)((H>>8)&0xFF),0,0,1,0,24,0,0,0,0,0,img,img>>8,img>>16,img>>24,
        0x13,0x0B,0,0,0x13,0x0B,0,0,0,0,0,0,0,0,0,0};
    fwrite(hdr,1,54,f);
    for(int y=H-1;y>=0;y--){
        int w=0;
        for(int fi=0;fi<proj.num_frames;fi++){
            for(int x=0;x<CW;x++){
                u8 i=composite_at(fi,x,y);
                u32 c=(i==TRANSPARENT)?C(0xFF,0,0xFF):PALETTE[i];
                fputc((c>>16)&0xFF,f); fputc((c>>8)&0xFF,f); fputc(c&0xFF,f); w+=3;
            }
        }
        for(;w<row;w++) fputc(0,f);
    }
    fclose(f); return true;
}

static bool save_native(const char *p){
    FILE *f=fopen(p,"wb"); if(!f) return false;
    const char m[8]={'P','X','F','R','G',2,0,0};
    fwrite(m,1,8,f);
    u16 w=CW,h=CH; fwrite(&w,2,1,f); fwrite(&h,2,1,f);
    fwrite(PALETTE,sizeof(u32),PALETTE_SIZE,f);
    u8 hdr[4]={proj.num_layers,proj.num_frames,proj.current_layer,proj.current_frame};
    fwrite(hdr,1,4,f);
    for(int li=0;li<proj.num_layers;li++){
        fwrite(proj.layers[li].name,1,16,f);
        u8 v=proj.layers[li].visible?1:0; fwrite(&v,1,1,f);
        for(int fi=0;fi<proj.num_frames;fi++) fwrite(proj.layers[li].frames[fi].pixels,1,CW*CH,f);
    }
    fclose(f); return true;
}

static bool load_native(const char *p){
    FILE *f=fopen(p,"rb"); if(!f) return false;
    char m[8];
    if(fread(m,1,8,f)!=8||memcmp(m,"PXFRG",5)!=0||m[5]!=2){fclose(f);return false;}
    u16 w,h;
    if(fread(&w,2,1,f)!=1||fread(&h,2,1,f)!=1||w!=CW||h!=CH){fclose(f);return false;}
    u32 pal[PALETTE_SIZE];
    if(fread(pal,sizeof(u32),PALETTE_SIZE,f)!=PALETTE_SIZE){fclose(f);return false;}
    u8 hdr[4]; if(fread(hdr,1,4,f)!=4){fclose(f);return false;}
    if(hdr[0]==0||hdr[0]>MAX_LAYERS||hdr[1]==0||hdr[1]>MAX_FRAMES){fclose(f);return false;}
    Project t; memset(&t,TRANSPARENT,sizeof(t));
    t.num_layers=hdr[0]; t.num_frames=hdr[1];
    t.current_layer=hdr[2]<t.num_layers?hdr[2]:0;
    t.current_frame=hdr[3]<t.num_frames?hdr[3]:0;
    for(int li=0;li<MAX_LAYERS;li++) t.layers[li].visible=false;
    for(int li=0;li<t.num_layers;li++){
        if(fread(t.layers[li].name,1,16,f)!=16){fclose(f);return false;}
        u8 v; if(fread(&v,1,1,f)!=1){fclose(f);return false;}
        t.layers[li].visible=v!=0;
        for(int fi=0;fi<t.num_frames;fi++)
            if(fread(t.layers[li].frames[fi].pixels,1,CW*CH,f)!=CW*CH){fclose(f);return false;}
    }
    fclose(f); push_history(&proj); proj=t; return true;
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

typedef struct { int x,y,w,h; } Rect;
static const Rect TR[NUM_TOOLS]={{4,2,30,24},{36,2,30,24},{4,28,30,24},{36,28,30,24},{4,54,30,24},{36,54,30,24}};
static const Rect LP={4,84,14,14},LL={20,84,28,14},LN={50,84,14,14};
static const Rect LA={4,100,28,18},LV={34,100,14,18},LD={50,100,14,18};
static const Rect FP={4,122,14,14},FL={20,122,28,14},FN={50,122,14,14};
static const Rect FA={4,138,14,18},FD={20,138,14,18},FY={36,138,14,18},FO={52,138,12,18};
static const Rect BU={4,160,30,20},BR={36,160,30,20};
static const Rect BG={4,184,30,20},BC={36,184,30,20};
static const Rect BS={4,208,30,20},BL={36,208,30,20};
static const Rect CR={CANVAS_X,CANVAS_Y,CANVAS_W,CANVAS_H};
static const Rect PR={302,8,14,224};

static bool hit(const Rect *r,int x,int y){return x>=r->x&&x<r->x+r->w&&y>=r->y&&y<r->y+r->h;}

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

static void handle_touch(u32 kHeld,touchPosition t){
    bool now_t=(kHeld&KEY_TOUCH)!=0;
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
            else if(hit(&BC,tx,ty)){ push_history(&proj); memset(cur_frame()->pixels,TRANSPARENT,CW*CH); flash("Cleared"); }
            else if(hit(&BS,tx,ty)) do_save();
            else if(hit(&BL,tx,ty)) do_quick_load();
            else for(int i=0;i<PALETTE_SIZE;i++){ Rect r={PR.x,PR.y+i*14,PR.w,14}; if(hit(&r,tx,ty)){current_color=i;flash("Color set");break;} }
            done: ;
        }
    } else if(now_t&&was_touching){
        if(hit(&CR,t.px,t.py)) apply_at_pixel((t.px-CANVAS_X)/PIXEL_SIZE,(t.py-CANVAS_Y)/PIXEL_SIZE,false);
    } else if(!now_t&&was_touching){ end_stroke(); }
    was_touching=now_t;
}

static void handle_buttons(u32 kDown,u32 kHeld){
    if(kHeld&KEY_SELECT){
        if(kDown&KEY_DUP) prev_layer();
        if(kDown&KEY_DDOWN) next_layer();
        if(kDown&KEY_DLEFT) prev_frame();
        if(kDown&KEY_DRIGHT) next_frame();
    } else {
        if(kDown&KEY_DUP) cursor_y=(cursor_y-1+CH)%CH;
        if(kDown&KEY_DDOWN) cursor_y=(cursor_y+1)%CH;
        if(kDown&KEY_DLEFT) cursor_x=(cursor_x-1+CW)%CW;
        if(kDown&KEY_DRIGHT) cursor_x=(cursor_x+1)%CW;
    }
    if(kDown&KEY_A){ push_history(&proj); set_pixel(cursor_x,cursor_y,current_color); }
    if(kDown&KEY_B){ push_history(&proj); set_pixel(cursor_x,cursor_y,TRANSPARENT); }
    if(kDown&KEY_X) do_undo();
    if(kDown&KEY_Y) do_redo();
    if(kDown&KEY_L) current_color=(current_color+PALETTE_SIZE-1)%PALETTE_SIZE;
    if(kDown&KEY_R) current_color=(current_color+1)%PALETTE_SIZE;
    if((kDown&KEY_SELECT)&&!(kHeld&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT))) do_quick_save();
}

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
    draw_checker(x,y,CW*sc,CH*sc,C(0x1F,0x1F,0x23),C(0x28,0x28,0x2D),sc*2.0f);
    for(int py=0;py<CH;py++) for(int px=0;px<CW;px++){
        u8 v=composite_at(fi,px,py);
        if(v!=TRANSPARENT) C2D_DrawRectSolid(x+px*sc,y+py*sc,0,sc,sc,PALETTE[v]);
    }
}
static void draw_composite_alpha(float x,float y,float sc,int fi,u8 a){
    for(int py=0;py<CH;py++) for(int px=0;px<CW;px++){
        u8 v=composite_at(fi,px,py);
        if(v!=TRANSPARENT){ u32 c=(PALETTE[v]&0x00FFFFFF)|((u32)a<<24); C2D_DrawRectSolid(x+px*sc,y+py*sc,0,sc,sc,c); }
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

static void render_bottom(void){
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
    { char b[8]; snprintf(b,8,"F%d/%d",proj.current_frame+1,proj.num_frames);
      draw_button(&FL,b,C(0x33,0x33,0x44),C(0xE0,0xE0,0xFF),0.4f); }
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

    draw_checker(CANVAS_X,CANVAS_Y,CANVAS_W,CANVAS_H,C(0x1F,0x1F,0x23),C(0x28,0x28,0x2D),PIXEL_SIZE*2.0f);
    if(show_onion&&proj.current_frame>0) draw_composite_alpha(CANVAS_X,CANVAS_Y,PIXEL_SIZE,proj.current_frame-1,0x60);
    for(int py=0;py<CH;py++) for(int px=0;px<CW;px++){
        u8 v=composite_at(proj.current_frame,px,py);
        if(v!=TRANSPARENT) C2D_DrawRectSolid(CANVAS_X+px*PIXEL_SIZE,CANVAS_Y+py*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[v]);
    }
    if((current_tool==TOOL_LINE||current_tool==TOOL_RECT)&&shape_x0>=0){
        if(current_tool==TOOL_LINE){
            int dx=abs(shape_x1-shape_x0),dy=abs(shape_y1-shape_y0);
            int sx=shape_x0<shape_x1?1:-1,sy=shape_y0<shape_y1?1:-1,err=dx-dy,x=shape_x0,y=shape_y0;
            for(;;){ C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[current_color]);
                if(x==shape_x1&&y==shape_y1) break; int e2=2*err; if(e2>-dy){err-=dy;x+=sx;} if(e2<dx){err+=dx;y+=sy;} }
        } else {
            int xa=shape_x0<shape_x1?shape_x0:shape_x1,xb=shape_x0<shape_x1?shape_x1:shape_x0;
            int ya=shape_y0<shape_y1?shape_y0:shape_y1,yb=shape_y0<shape_y1?shape_y1:shape_y0;
            for(int x=xa;x<=xb;x++){
                C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+ya*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[current_color]);
                C2D_DrawRectSolid(CANVAS_X+x*PIXEL_SIZE,CANVAS_Y+yb*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[current_color]);
            }
            for(int y=ya;y<=yb;y++){
                C2D_DrawRectSolid(CANVAS_X+xa*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[current_color]);
                C2D_DrawRectSolid(CANVAS_X+xb*PIXEL_SIZE,CANVAS_Y+y*PIXEL_SIZE,0,PIXEL_SIZE,PIXEL_SIZE,PALETTE[current_color]);
            }
        }
    }
    if(show_grid){
        u32 g=CA(0xFF,0xFF,0xFF,0x14);
        for(int i=1;i<CW;i++) C2D_DrawRectSolid(CANVAS_X+i*PIXEL_SIZE,CANVAS_Y,0,1,CANVAS_H,g);
        for(int j=1;j<CH;j++) C2D_DrawRectSolid(CANVAS_X,CANVAS_Y+j*PIXEL_SIZE,0,CANVAS_W,1,g);
    }
    { float cx=CANVAS_X+cursor_x*PIXEL_SIZE,cy=CANVAS_Y+cursor_y*PIXEL_SIZE; u32 h=CA(0xFF,0xFF,0x80,0xC0);
      C2D_DrawRectSolid(cx,cy,0,PIXEL_SIZE,1,h); C2D_DrawRectSolid(cx,cy+PIXEL_SIZE-1,0,PIXEL_SIZE,1,h);
      C2D_DrawRectSolid(cx,cy,0,1,PIXEL_SIZE,h); C2D_DrawRectSolid(cx+PIXEL_SIZE-1,cy,0,1,PIXEL_SIZE,h); }
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y-1,0,CANVAS_W+2,1,b);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y+CANVAS_H,0,CANVAS_W+2,1,b);
      C2D_DrawRectSolid(CANVAS_X-1,CANVAS_Y,0,1,CANVAS_H,b);
      C2D_DrawRectSolid(CANVAS_X+CANVAS_W,CANVAS_Y,0,1,CANVAS_H,b); }
    for(int i=0;i<PALETTE_SIZE;i++){
        float y=PR.y+i*14;
        C2D_DrawRectSolid(PR.x,y,0,PR.w,13,PALETTE[i]);
        if(i==current_color){
            u32 h=C(0xFF,0xFF,0xFF);
            C2D_DrawRectSolid(PR.x-2,y-1,0,PR.w+4,1,h);
            C2D_DrawRectSolid(PR.x-2,y+13,0,PR.w+4,1,h);
            C2D_DrawRectSolid(PR.x-2,y-1,0,1,15,h);
            C2D_DrawRectSolid(PR.x+PR.w+1,y-1,0,1,15,h);
        }
    }
}

static void render_top(void){
    C2D_DrawRectSolid(0,0,0,TOP_W,18,C(0x16,0x16,0x1B));
    draw_text("PixelForge 3DS",8,4,0.55f,C(0xE0,0xC0,0xFF));
    { char h[64]; snprintf(h,64,"L%d/%d  F%d/%d  %s",proj.current_layer+1,proj.num_layers,proj.current_frame+1,proj.num_frames,playing?"PLAY":"STOP");
      draw_text(h,250,4,0.5f,C(0xC0,0xC0,0xD0)); }
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
    int p4x=180,p4y=24,p4s=4,pf=playing?playback_frame:proj.current_frame;
    draw_composite(p4x,p4y,(float)p4s,pf);
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(p4x-1,p4y-1,0,CW*p4s+2,1,b);
      C2D_DrawRectSolid(p4x-1,p4y+CH*p4s,0,CW*p4s+2,1,b);
      C2D_DrawRectSolid(p4x-1,p4y,0,1,CH*p4s,b);
      C2D_DrawRectSolid(p4x+CW*p4s,p4y,0,1,CH*p4s,b);
      char b2[16]; snprintf(b2,16,"4x  F%d",pf+1); draw_text(b2,p4x,p4y+CH*p4s+4,0.45f,C(0x80,0x80,0x88)); }
    int p1x=320,p1y=24;
    draw_composite(p1x,p1y,1.0f,pf);
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(p1x-1,p1y-1,0,CW+2,1,b);
      C2D_DrawRectSolid(p1x-1,p1y+CH,0,CW+2,1,b);
      C2D_DrawRectSolid(p1x-1,p1y,0,1,CH,b);
      C2D_DrawRectSolid(p1x+CW,p1y,0,1,CH,b);
      draw_text("1x",p1x,p1y+CH+2,0.4f,C(0x80,0x80,0x88)); }
    int p2x=320,p2y=70,p2s=2;
    draw_composite(p2x,p2y,(float)p2s,pf);
    { u32 b=C(0x4A,0x4A,0x52);
      C2D_DrawRectSolid(p2x-1,p2y-1,0,CW*p2s+2,1,b);
      C2D_DrawRectSolid(p2x-1,p2y+CH*p2s,0,CW*p2s+2,1,b);
      C2D_DrawRectSolid(p2x-1,p2y,0,1,CH*p2s,b);
      C2D_DrawRectSolid(p2x+CW*p2s,p2y,0,1,CH*p2s,b);
      draw_text("2x",p2x,p2y+CH*p2s+2,0.4f,C(0x80,0x80,0x88)); }
    { int fx=8,fy=150,fs=16,gap=4;
      draw_text("FRAMES",fx,fy-12,0.4f,C(0x80,0x80,0x90));
      for(int fi=0;fi<proj.num_frames;fi++){
          int tx=fx+fi*(fs+gap),ty=fy;
          for(int py=0;py<CH;py+=2) for(int px=0;px<CW;px+=2){
              u8 v=composite_at(fi,px,py);
              if(v!=TRANSPARENT) C2D_DrawRectSolid(tx+px/2,ty+py/2,0,1,1,PALETTE[v]);
              else { u32 ck=((px/2+py/2)&1)?C(0x28,0x28,0x2D):C(0x1F,0x1F,0x23); C2D_DrawRectSolid(tx+px/2,ty+py/2,0,1,1,ck); }
          }
          if(fi==proj.current_frame){ u32 h=C(0xFF,0xC0,0xFF);
              C2D_DrawRectSolid(tx-1,ty-1,0,fs+2,1,h); C2D_DrawRectSolid(tx-1,ty+fs,0,fs+2,1,h);
              C2D_DrawRectSolid(tx-1,ty,0,1,fs,h); C2D_DrawRectSolid(tx+fs,ty,0,1,fs,h);
          } else if(playing&&fi==playback_frame){ u32 h=C(0xC0,0xFF,0xC0);
              C2D_DrawRectSolid(tx-1,ty-1,0,fs+2,1,h); C2D_DrawRectSolid(tx-1,ty+fs,0,fs+2,1,h);
              C2D_DrawRectSolid(tx-1,ty,0,1,fs,h); C2D_DrawRectSolid(tx+fs,ty,0,1,fs,h);
          }
      } }
    { int sy=175;
      C2D_DrawRectSolid(0,sy,0,TOP_W,TOP_H-sy,C(0x12,0x12,0x16));
      char b[64];
      snprintf(b,64,"Tool: %s  Color: %d  Cursor: %d,%d",TOOL_NAMES[current_tool],current_color,cursor_x,cursor_y);
      draw_text(b,8,sy+4,0.45f,C(0xE0,0xE0,0xE8));
      C2D_DrawRectSolid(220,sy+4,0,16,12,PALETTE[current_color]);
      snprintf(b,64,"FPS: %d  Hist: %d/%d  Onion: %s  Grid: %s",fps,past_count,future_count,show_onion?"on":"off",show_grid?"on":"off");
      draw_text(b,8,sy+18,0.4f,C(0xA0,0xA0,0xA8));
      draw_text("Stylus paints | DPad cursor | A paint B erase X undo Y redo",8,sy+32,0.4f,C(0x70,0x70,0x78));
      draw_text("L/R cycle color | SELECT+DPad = layer/frame nav",8,sy+44,0.4f,C(0x70,0x70,0x78));
      draw_text("SELECT = quicksave | SV = timestamped sheet | START = quit",8,sy+56,0.4f,C(0x70,0x70,0x78)); }
    if(now_ms()<flash_until&&flash_msg[0]){
        float w=(float)strlen(flash_msg)*8.0f+16; float x=(TOP_W-w)/2;
        C2D_DrawRectSolid(x,22,0,w,18,C(0x44,0x22,0x66));
        draw_text(flash_msg,x+8,25,0.55f,C(0xFF,0xE0,0xFF));
    }
}

int main(int argc,char **argv){
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    C3D_RenderTarget *bot=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    text_buf=C2D_TextBufNew(4096);
    init_project();
    load_native(SAVE_DIR "/latest.pxf");
    while(aptMainLoop()){
        hidScanInput();
        u32 kDown=hidKeysDown(),kHeld=hidKeysHeld();
        touchPosition t; hidTouchRead(&t);
        if(kDown&KEY_START) break;
        handle_buttons(kDown,kHeld);
        handle_touch(kHeld,t);
        if(playing&&now_ms()>=next_frame_at){
            playback_frame=(playback_frame+1)%proj.num_frames;
            next_frame_at=now_ms()+(1000/fps);
        }
        C2D_TextBufClear(text_buf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top,C(0x09,0x09,0x0B)); C2D_SceneBegin(top); render_top();
        C2D_TargetClear(bot,C(0x18,0x18,0x1C)); C2D_SceneBegin(bot); render_bottom();
        C3D_FrameEnd(0);
    }
    C2D_TextBufDelete(text_buf);
    C2D_Fini(); C3D_Fini(); gfxExit();
    return 0;
}
