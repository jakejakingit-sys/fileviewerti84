#include <graphx.h>
#include <keypadc.h>
#include <sys/timers.h>
#include <fileioc.h>
#include <ti/vars.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SW 320
#define SH 240
#define MAX_ITEMS 80
#define TEXT_CAP 4096
#define FVGF_W 64
#define FVGF_H 48

enum { C_BG=248, C_PANEL=249, C_LINE=250, C_TEXT=251, C_DIM=252, C_ACCENT=253, C_WARN=254, C_WHITE=255 };
enum { TAB_TEXT, TAB_IMAGE, TAB_GIF };
enum { VIEW_LIST, VIEW_TEXT, VIEW_IMAGE, VIEW_GIF, VIEW_INFO };
typedef struct { char name[9]; uint8_t type, kind; void *data; } Item;

static Item items[MAX_ITEMS];
static unsigned item_count, selected, scroll;
static int tab=TAB_TEXT, view=VIEW_LIST, text_scroll, gif_frame, gif_clock;
static char message[30];
static uint8_t text_data[TEXT_CAP];
static unsigned text_len;
static uint8_t *image_data;
static uint16_t image_size;
static uint8_t image_stride=134;
static bool image_valid;
static uint8_t gif_w,gif_h,gif_n,gif_delay;
static uint8_t *gif_data;
static uint16_t gif_size;
static char opened_name[9];
static unsigned debug_direct_hits,debug_name_hits,debug_symbols;
static char debug_last_name[9];
static uint8_t debug_last_type;
static bool last_up,last_down,last_left,last_right,last_enter,last_clear;
static bool running=true;

static void rect(int x,int y,int w,int h,uint8_t c){gfx_SetColor(c);gfx_FillRectangle(x,y,w,h);}
static void frame(int x,int y,int w,int h,uint8_t c){gfx_SetColor(c);gfx_Rectangle(x,y,w,h);}
static void text(const char*s,int x,int y,uint8_t c){gfx_SetTextFGColor(c);gfx_PrintStringXY(s,x,y);}
static void set_message(const char*s){strncpy(message,s,sizeof(message)-1);message[sizeof(message)-1]=0;}

static int item_kind_for(uint8_t type,const char *name){
  if(tab==TAB_TEXT){
    if(type==OS_TYPE_STR || type==OS_TYPE_PRGM || type==OS_TYPE_PROT_PRGM || type==OS_TYPE_TMP_PRGM) return 1;
    if(type==OS_TYPE_APPVAR) return 2;
  } else if(tab==TAB_IMAGE){
    if(type==OS_TYPE_PIC || type==OS_TYPE_IMAGE) return 3;
  } else if(tab==TAB_GIF && type==OS_TYPE_APPVAR) return 4;
  (void)name; return 0;
}

static void set_image_item(const char *prefix,unsigned index,void *data,uint8_t type){
  Item *it=&items[item_count++];
  if(prefix[0]=='I'){
    memcpy(it->name,"Image",5);it->name[5]=(char)('0'+((index+1)%10));it->name[6]=0;
  }else{
    memcpy(it->name,"Pic",3);it->name[3]=(char)('0'+((index+1)%10));it->name[4]=0;
  }
  it->type=type;it->kind=3;it->data=data;
}

static void scan_items(void){
  void *entry=os_GetSymTablePtr(), *data; uint24_t raw_type,name_len; char found[16];
  item_count=0; selected=scroll=0;
  debug_direct_hits=debug_name_hits=debug_symbols=0;debug_last_name[0]=0;debug_last_type=0;
  /* Image/Pic names are tokenized in the OS symbol table, not ASCII names.
     Image: 0x3C,index; Pic: 0x60,index. */
  if(tab==TAB_IMAGE){
    entry=os_GetSymTablePtr();
    while(item_count<MAX_ITEMS && (entry=os_NextSymEntry(entry,&raw_type,&name_len,found,&data))!=NULL){
      debug_symbols++;
      if(name_len<2)continue;
      uint8_t token=(uint8_t)found[0],index=(uint8_t)found[1],type=(uint8_t)raw_type;
      if(type==OS_TYPE_IMAGE&&token==0x3C){
        if(index>9)continue;
        debug_direct_hits++;debug_name_hits++;set_image_item("Image",index,data,type);
      }else if(type==OS_TYPE_PIC&&token==0x60){
        if(index>9)continue;
        debug_direct_hits++;debug_name_hits++;set_image_item("Pic",index,data,type);
      }
    }
    return;
  }
  while(item_count<MAX_ITEMS && (entry=os_NextSymEntry(entry,&raw_type,&name_len,found,&data))!=NULL){
    if(!name_len || name_len>8) continue;
    found[name_len]=0;
    /* The symbol table also contains OS-reserved entries whose names start
       below 'A'; ignore those just like CEdev's variable-browser example. */
    if(found[0]<'A')continue;
    uint8_t type=(uint8_t)raw_type;
    int kind=item_kind_for(type,found);
    if(!kind || strcmp(found,"FILEVIEW")==0) continue;
    if(tab==TAB_TEXT && kind==2){
      uint8_t slot=ti_OpenVar(found,"r",OS_TYPE_APPVAR); if(!slot)continue;
      uint8_t *p=(uint8_t*)ti_GetDataPtr(slot); uint16_t n=ti_GetSize(slot);
      bool ok=n>=4 && p && memcmp(p,"FVTX",4)==0; ti_Close(slot); if(!ok)continue;
    }
    if(tab==TAB_GIF){
      uint8_t slot=ti_OpenVar(found,"r",OS_TYPE_APPVAR); if(!slot)continue;
      uint8_t *p=(uint8_t*)ti_GetDataPtr(slot); uint16_t n=ti_GetSize(slot);
      bool ok=n>=10 && p && memcmp(p,"FVGF",4)==0; ti_Close(slot); if(!ok)continue;
    }
    Item *it=&items[item_count++]; strncpy(it->name,found,8); it->name[8]=0; it->type=type; it->kind=(uint8_t)kind; it->data=data;
  }
}

static void load_text(Item *it){
  uint8_t slot=ti_OpenVar(it->name,"r",it->type);
  if(!slot){set_message("CANNOT OPEN VARIABLE");view=VIEW_INFO;return;}
  uint8_t *p=(uint8_t*)ti_GetDataPtr(slot); uint16_t size=ti_GetSize(slot); text_len=0;
  if(!p){ti_Close(slot);set_message("EMPTY VARIABLE");view=VIEW_INFO;return;}
  if(it->kind==2){
    if(size>4){text_len=size-4;if(text_len>TEXT_CAP)text_len=TEXT_CAP;memcpy(text_data,p+4,text_len);}
  }else if(it->type==OS_TYPE_STR){
    string_t *s=(string_t*)p;text_len=s->len;if(text_len>TEXT_CAP)text_len=TEXT_CAP;memcpy(text_data,s->data,text_len);
  }else{
    /* TI-BASIC programs are tokenized. Decode tokens into a readable buffer. */
    void *read=p; unsigned int remaining=size;
    while(remaining && text_len<TEXT_CAP-1){
      uint8_t token_len=0; unsigned int string_len=0; void *before=read;
      char *part=ti_GetTokenString(&read,&token_len,&string_len);
      if(!part || read==before || token_len>remaining)break;
      unsigned int take=string_len; if(take>TEXT_CAP-text_len-1)take=TEXT_CAP-text_len-1;
      memcpy(text_data+text_len,part,take);text_len+=take;remaining-=token_len;
      if(text_len<TEXT_CAP-1 && remaining){text_data[text_len++]='\n';}
    }
  }
  ti_Close(slot); text_scroll=0;strncpy(opened_name,it->name,8);opened_name[8]=0;view=VIEW_TEXT;
}

static void open_image(Item *it){
  /* Pic Vars use a different packed format; never interpret them as RGB565. */
  if(it->type==OS_TYPE_PIC){
    image_data=NULL;image_valid=false;set_message("PIC PREVIEW NOT SUPPORTED");view=VIEW_INFO;return;
  }
  /* CE Image Vars are [size word][0x81][RGB565 pixels]. The size word is
     not reliable for deriving row width: 133 visible pixels plus 2 padding
     bytes means a fixed 134-pixel (268-byte) row stride. */
  uint8_t *data=(uint8_t*)it->data;
  if(!data){set_message("CANNOT OPEN IMAGE");view=VIEW_INFO;return;}
  image_stride=134;
  if(data[2]==0x81)image_data=data+3;
  else if(data[0]==0x81)image_data=data+1;
  else image_data=NULL;
  image_size=(uint16_t)(83u*image_stride*2u);image_valid=image_data!=NULL;
  strncpy(opened_name,it->name,8);opened_name[8]=0;view=VIEW_IMAGE;
}

static void open_gif(Item *it){
  uint8_t slot=ti_OpenVar(it->name,"r",OS_TYPE_APPVAR);
  if(!slot){set_message("CANNOT OPEN ANIMATION");view=VIEW_INFO;return;}
  uint8_t *p=(uint8_t*)ti_GetDataPtr(slot);uint16_t n=ti_GetSize(slot);
  if(!p||n<10||memcmp(p,"FVGF",4)){ti_Close(slot);set_message("BAD FRAME PACK");view=VIEW_INFO;return;}
  gif_w=p[4];gif_h=p[5];gif_n=p[6];gif_delay=p[7];gif_data=p+10;gif_size=n-10;
  if(!gif_w||!gif_h||!gif_n||gif_w>FVGF_W||gif_h>FVGF_H||gif_size<(uint16_t)(gif_w*gif_h*gif_n)){
    ti_Close(slot);set_message("BAD FRAME DATA");view=VIEW_INFO;return;
  }
  strncpy(opened_name,it->name,8);opened_name[8]=0;gif_frame=gif_clock=0;ti_Close(slot);view=VIEW_GIF;
}

static void open_selected(void){
  if(selected>=item_count)return;
  Item *it=&items[selected];
  if(tab==TAB_TEXT)load_text(it);else if(tab==TAB_IMAGE)open_image(it);else open_gif(it);
}

static void background(void){
  gfx_FillScreen(C_BG);
  rect(0,0,SW,27,C_PANEL);rect(0,26,SW,2,C_ACCENT);
  text("FILEVIEWER",10,8,C_WHITE);text("TI-84 CE FILE BROWSER",198,9,C_DIM);
}

static void draw_list(void){
  static const char *tabs[3]={"TEXT","IMAGES","GIFS"};
  background();
  for(int i=0;i<3;i++){int x=18+i*100;rect(x,37,86,21,i==tab?C_ACCENT:C_PANEL);text(tabs[i],x+17,44,i==tab?C_BG:C_DIM);}
  text("UP/DOWN SELECT   ENTER OPEN",19,68,C_DIM);
  frame(12,82,296,133,C_LINE);
  if(!item_count){
    text("NO FILES IN THIS CATEGORY",37,105,C_WARN);
    if(tab==TAB_IMAGE){
      text("IMAGE/PIC DIRECT LOOKUPS:",23,129,C_DIM);gfx_SetTextFGColor(C_WHITE);gfx_SetTextXY(210,129);gfx_PrintUInt(debug_direct_hits,1);
      text("IMAGE/PIC NAMES IN VAT:",23,148,C_DIM);gfx_SetTextFGColor(C_WHITE);gfx_SetTextXY(210,148);gfx_PrintUInt(debug_name_hits,1);
      text("SYMBOLS SCANNED:",23,167,C_DIM);gfx_SetTextFGColor(C_WHITE);gfx_SetTextXY(210,167);gfx_PrintUInt(debug_symbols,1);
      if(debug_last_name[0]){text("LAST I/P NAME:",23,186,C_DIM);text(debug_last_name,142,186,C_WHITE);text("TYPE",205,186,C_DIM);gfx_SetTextFGColor(C_WHITE);gfx_SetTextXY(244,186);gfx_PrintUInt(debug_last_type,1);}
    }else text(tab==TAB_GIF?"ADD AN FVGF PACK MADE BY TOOL":"TRANSFER FILES TO CALCULATOR",27,139,C_DIM);
  }
  else{
    if(selected<scroll)scroll=selected;if(selected>=scroll+8)scroll=selected-7;
    for(unsigned row=0;row<8&&scroll+row<item_count;row++){
      unsigned idx=scroll+row;int y=88+(int)row*15;
      if(idx==selected)rect(17,y-1,286,14,C_PANEL);
      text(items[idx].name,24,y,idx==selected?C_WHITE:C_DIM);
      if(tab==TAB_TEXT)text(items[idx].kind==2?"TEXT APPVAR":"PROGRAM/STRING",118,y,C_DIM);
      else if(tab==TAB_IMAGE)text(items[idx].type==OS_TYPE_PIC?"PICTURE":"IMAGE",180,y,C_DIM);
      else text("ANIMATION",183,y,C_DIM);
    }
  }
  rect(0,222,SW,18,C_PANEL);text("LEFT/RIGHT CATEGORY    CLEAR QUIT",14,227,C_DIM);
}

static void draw_text_view(void){
  background();text(opened_name,12,39,C_ACCENT);text("CLEAR: BACK   UP/DOWN: SCROLL",115,39,C_DIM);frame(9,58,302,154,C_LINE);
  unsigned line=0, pos=0, skip=(unsigned)text_scroll*25;
  while(pos<text_len&&line<11){if(skip){if(text_data[pos++]=='\n')skip--;continue;}
    char row[39];unsigned n=0;while(pos<text_len&&n<37&&text_data[pos]!='\n'){uint8_t c=text_data[pos++];row[n++]=(c>=32&&c<127)?c:'?';}if(pos<text_len&&text_data[pos]=='\n')pos++;row[n]=0;text(row,16,65+(int)line*13,C_TEXT);line++;
  }
  text("TEXT VIEW",12,222,C_ACCENT);
}

static void draw_image_view(void){
  background();text(opened_name,12,39,C_ACCENT);text("CLEAR: BACK",235,39,C_DIM);
  frame(15,61,290,145,C_LINE);
  if(image_valid&&image_data&&image_size>=83u*image_stride*2u){
    /* TI CE Image Vars are 83x133 pixels, 16-bit color, stored as RGB565. */
    int ox=(320-133)/2,oy=(240-83)/2;
    for(int y=0;y<83;y++)for(int x=0;x<133;x++){
      const uint8_t *p=image_data+((unsigned)y*image_stride+x)*2;
      uint16_t v=(uint16_t)(p[0]|((uint16_t)p[1]<<8));uint8_t r=(v>>11)&31,g=(v>>5)&63,b=v&31;
      uint8_t index=(uint8_t)(((r>>2)<<5)|((g>>3)<<2)|(b>>3));
      if(index>=248)index=247;
      gfx_SetColor(index);gfx_SetPixel(ox+x,oy+y);
    }
  }else{
    text("UNRECOGNIZED IMAGE DATA",59,126,C_WARN);
  }
  text("IMAGES",12,222,C_ACCENT);
}

static void draw_gif_view(void){
  background();text(opened_name,12,39,C_ACCENT);text("LEFT/RIGHT FRAME   CLEAR BACK",120,39,C_DIM);
  frame(20,62,280,142,C_LINE);
  uint8_t *frame_data=gif_data+(unsigned)gif_frame*gif_w*gif_h;int ox=(320-gif_w)/2,oy=77+(100-gif_h)/2;
  for(int y=0;y<gif_h;y++)for(int x=0;x<gif_w;x++){gfx_SetColor(frame_data[y*gif_w+x]);gfx_SetPixel(ox+x,oy+y);}
  gfx_SetColor(C_ACCENT);gfx_Line(ox,oy+gif_h+3,ox+gif_w-1,oy+gif_h+3);
  text("FRAME",20,218,C_DIM);gfx_SetTextFGColor(C_WHITE);gfx_SetTextXY(68,218);gfx_PrintUInt(gif_frame+1,1);
  text("/",83,218,C_DIM);gfx_SetTextXY(91,218);gfx_PrintUInt(gif_n,1);
}

static void draw_info(void){background();frame(20,65,280,130,C_LINE);text(message[0]?message:"UNAVAILABLE",45,111,C_WARN);text("PRESS CLEAR TO RETURN",71,145,C_DIM);}
static void draw(void){if(view==VIEW_LIST)draw_list();else if(view==VIEW_TEXT)draw_text_view();else if(view==VIEW_IMAGE)draw_image_view();else if(view==VIEW_GIF)draw_gif_view();else draw_info();gfx_SwapDraw();}

static void update(void){
  kb_Scan();
  bool left=(kb_Data[7]&kb_Left)!=0,right=(kb_Data[7]&kb_Right)!=0;
  bool u=(kb_Data[7]&kb_Up)!=0,d=(kb_Data[7]&kb_Down)!=0;
  bool enter=(kb_Data[6]&kb_Enter)!=0,clear=(kb_Data[6]&kb_Clear)!=0;
  bool edge_left=left&&!last_left,edge_right=right&&!last_right,edge_up=u&&!last_up,edge_down=d&&!last_down,edge_enter=enter&&!last_enter,edge_clear=clear&&!last_clear;
  last_left=left;last_right=right;last_up=u;last_down=d;last_enter=enter;last_clear=clear;
  if(view!=VIEW_LIST){
    if(edge_clear){view=VIEW_LIST;scan_items();return;}
    if(view==VIEW_TEXT){if(edge_up&&text_scroll>0)text_scroll--;if(edge_down)text_scroll++;}
    if(view==VIEW_GIF){if(edge_left&&gif_frame>0)gif_frame--;if(edge_right&&gif_frame+1<gif_n)gif_frame++;if(++gif_clock>=gif_delay){gif_clock=0;gif_frame=(gif_frame+1)%gif_n;}}
    return;
  }
  if(edge_clear){gfx_End();running=false;return;}
  if(edge_left||edge_right){tab=(tab+(edge_right?1:2))%3;scan_items();}
  if(edge_up&&selected>0)selected--;
  if(edge_down&&selected+1<item_count)selected++;
  if(edge_enter)open_selected();
}

int main(void){
  gfx_Begin();gfx_SetDrawBuffer();gfx_SetTextScale(1,1);
  uint16_t palette[256];
  for(unsigned i=0;i<256;i++){
    unsigned r=(i>>5)&7,g=(i>>2)&7,b=i&3;
    palette[i]=gfx_RGBTo1555((uint8_t)(r*255/7),(uint8_t)(g*255/7),(uint8_t)(b*255/3));
  }
  palette[C_BG]=gfx_RGBTo1555(4,8,14);palette[C_PANEL]=gfx_RGBTo1555(15,26,39);palette[C_LINE]=gfx_RGBTo1555(42,67,83);
  palette[C_TEXT]=gfx_RGBTo1555(184,210,218);palette[C_DIM]=gfx_RGBTo1555(95,126,143);palette[C_ACCENT]=gfx_RGBTo1555(84,236,202);palette[C_WARN]=gfx_RGBTo1555(255,130,105);palette[C_WHITE]=gfx_RGBTo1555(244,250,250);
  gfx_SetPalette(palette,sizeof(palette),0);scan_items();
  while(running){draw();update();delay(1);}
  return 0;
}
