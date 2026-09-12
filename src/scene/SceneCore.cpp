#include "SceneCore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef ARDUINO
#include <pgmspace.h>
#else
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define memcpy_P std::memcpy
#endif

namespace scene {
namespace {
struct Painter {
    uint16_t* pixels;
    int top;
    void pixel(int x, int y, uint16_t color) {
        if (x >= 0 && x < Width && y >= top && y < top + BandHeight && y < Height)
            pixels[(y - top) * Width + x] = color;
    }
    void span(int x, int y, int w, uint16_t color) {
        if (y < top || y >= top + BandHeight || y >= Height || w <= 0) return;
        const int end = std::min(Width, x + w);
        x = std::max(0, x);
        if (end > x) std::fill(pixels + (y-top)*Width+x, pixels + (y-top)*Width+end, color);
    }
    void rect(int x, int y, int w, int h, uint16_t color, bool fill) {
        if (w <= 0 || h <= 0) return;
        for (int row=std::max(y,top); row<std::min(y+h,top+BandHeight); ++row) {
            if (fill || row == y || row == y+h-1) span(x,row,w,color);
            else { pixel(x,row,color); pixel(x+w-1,row,color); }
        }
    }
    void line(int x, int y, int ex, int ey, uint16_t color) {
        const int dx=std::abs(ex-x), sx=x<ex?1:-1, dy=-std::abs(ey-y), sy=y<ey?1:-1;
        int error=dx+dy;
        for (;;) {
            pixel(x,y,color); if (x==ex && y==ey) break;
            const int e=2*error;
            if(e>=dy) { error+=dy; x+=sx; }
            if(e<=dx) { error+=dx; y+=sy; }
        }
    }
    void ellipse(int x,int y,int rx,int ry,uint16_t color,bool fill) {
        if(rx<0 || ry<0) return;
        if(rx==0) { line(x,y-ry,x,y+ry,color); return; }
        if(ry==0) { span(x-rx,y,rx*2+1,color); return; }
        for(int row=std::max(top,y-ry);row<=std::min(top+BandHeight-1,y+ry);++row) {
            const double f=std::max(0.0,1.0-double(row-y)*(row-y)/(double(ry)*ry));
            const int half=int(std::round(rx*std::sqrt(f)));
            if(fill) span(x-half,row,half*2+1,color);
            else {
                // Join adjacent scanline boundaries, including the flat arc ends.
                const int neighbor=std::min(ry,std::abs(row-y)+1);
                const double nf=std::max(0.0,1.0-double(neighbor)*neighbor/(double(ry)*ry));
                const int inner=std::min(half,int(std::round(rx*std::sqrt(nf))));
                span(x-half,row,half-inner+1,color);
                span(x+inner,row,half-inner+1,color);
            }
        }
    }
};
uint16_t scale(uint16_t c,uint8_t level) {
    return uint16_t((((c>>11)*level/255)<<11) | ((((c>>5)&63)*level/255)<<5) | ((c&31)*level/255));
}
uint16_t blend(uint16_t bg,uint16_t fg,unsigned a) {
    const unsigned b=15-a;
    return uint16_t(((((fg>>11)*a+(bg>>11)*b+7)/15)<<11) |
        (((((fg>>5)&63)*a+((bg>>5)&63)*b+7)/15)<<5) | (((fg&31)*a+(bg&31)*b+7)/15));
}
uint16_t nextCode(const char*& text, const char* end) {
    if(text>=end) return 0;
    const uint8_t a=uint8_t(*text++);
    if(a<128) return a;
    if(a>=0xc2 && a<=0xdf && text<end && (uint8_t(*text)&0xc0)==0x80) {
        return uint16_t(((a&31)<<6)|(uint8_t(*text++)&63));
    }
    if((a&0xf0)==0xe0 && end-text>=2 && (uint8_t(text[0])&0xc0)==0x80 && (uint8_t(text[1])&0xc0)==0x80) {
        const uint16_t result=uint16_t(((a&15)<<12)|((uint8_t(text[0])&63)<<6)|(uint8_t(text[1])&63));
        text+=2; return result>=0x800 && !(result>=0xd800 && result<=0xdfff)?result:'?';
    }
    if(a>=0xf0 && a<=0xf4 && end-text>=3 &&
        (uint8_t(text[0])&0xc0)==0x80 && (uint8_t(text[1])&0xc0)==0x80 &&
        (uint8_t(text[2])&0xc0)==0x80) { text+=3; return '?'; }
    return '?';
}
Glyph glyph(const Font& f,uint16_t code) {
    int lo=0,hi=f.count-1;
    Glyph g{};
    while(lo<=hi) {
        const int mid=(lo+hi)/2;
        memcpy_P(&g,f.glyphs+mid,sizeof(g));
        if(g.code==code) return g;
        if(g.code<code) lo=mid+1; else hi=mid-1;
    }
    if(code!='?') return glyph(f,'?');
    return {};
}
int textWidth(const Font& f,const char* start,const char* end,int limit) {
    int width=0; while(start<end && width<=limit) width+=glyph(f,nextCode(start,end)).advance; return width;
}
void drawGlyph(Painter& p,const Font& f,Glyph g,int x,int y,uint16_t color,int clipLeft,int clipRight) {
    const int gx=x+g.x, gy=y+f.baseline+g.y;
    const int first=std::max(0,p.top-gy), last=std::min<int>(g.h,p.top+BandHeight-gy);
    for(int row=first;row<last;++row) for(int col=std::max(0,std::max(0,clipLeft)-gx);col<std::min<int>(g.w,std::min(Width,clipRight)-gx);++col) {
        const unsigned index=unsigned(row*g.w+col);
        const uint8_t packed=pgm_read_byte(f.data+g.offset+index/2);
        const unsigned alpha=(index&1)?packed&15:packed>>4;
        if(alpha) { uint16_t& dest=p.pixels[(gy+row-p.top)*Width+gx+col]; dest=alpha==15?color:blend(dest,color,alpha); }
    }
}
void text(Painter& p,const Node& n,const char* start,size_t length,uint16_t color,bool clock,uint8_t pulse) {
    const Font& f=font(n.font);
    const char* end=start+length;
    const bool truncated=textWidth(f,start,end,n.w)>n.w;
    const Glyph dots=glyph(f,0x2026);
    int x=n.x;
    while(start<end) {
        const uint16_t code=nextCode(start,end);
        const Glyph g=glyph(f,code);
        if(x+g.advance>n.x+n.w || (truncated && x+g.advance+dots.advance>n.x+n.w)) {
            if(dots.advance<=n.w) drawGlyph(p,f,dots,std::min(x,n.x+n.w-dots.advance),n.y,color,n.x,n.x+n.w);
            break;
        }
        drawGlyph(p,f,g,x,n.y,clock && code==':'?scale(color,pulse):color,n.x,n.x+n.w);
        x+=g.advance;
    }
}
int64_t edge(int ax,int ay,int bx,int by,int x,int y) {
    return int64_t(x-ax)*(by-ay)-int64_t(y-ay)*(bx-ax);
}
} // namespace

uint8_t pulseLevel(const Node& node,uint32_t tickMs) {
    // Cosine envelope in flash; no floating point trigonometry per frame.
    static const uint8_t levels[40] PROGMEM={255,253,249,241,231,218,203,185,166,147,128,108,89,70,52,37,24,14,6,2,0,2,6,14,24,37,52,70,89,108,128,147,166,185,203,218,231,241,249,253};
    const uint32_t period=std::max<uint32_t>(1000,node.period);
    const unsigned index=(tickMs%period)*40/period;
    const unsigned low=std::min(node.low,node.high), high=std::max(node.low,node.high);
    return uint8_t(low+(high-low)*pgm_read_byte(levels+index)/255);
}

bool intersectsBand(const Node& n,int y) {
    int top=n.y,bottom=n.y;
    switch(n.kind) {
    case Kind::Text: case Kind::Clock: top-=8; bottom=n.y+font(n.font).height; break;
    case Kind::Rect: case Kind::RoundRect: bottom=n.y+n.h; break;
    case Kind::Line: top=std::min<int>(n.y,n.y1);bottom=std::max<int>(n.y,n.y1);break;
    case Kind::Circle: top=n.y-n.r;bottom=n.y+n.r;break;
    case Kind::Ellipse: top=n.y-n.ry;bottom=n.y+n.ry;break;
    case Kind::Triangle: top=std::min({n.y,n.y1,n.y2});bottom=std::max({n.y,n.y1,n.y2});break;
    default: break;
    }
    return bottom>=y && top<y+BandHeight;
}

bool clockPulseBounds(const Node& n,uint16_t minuteOfDay,Bounds& bounds) {
    if(n.kind!=Kind::Clock) return false;
    const Font& f=font(n.font);
    char value[6];const unsigned minute=minuteOfDay%1440;
    std::snprintf(value,sizeof(value),"%02u:%02u",minute/60,minute%60);
    // Truncation may replace the colon with an ellipsis; retain the general path.
    if(textWidth(f,value,value+5,n.w)>n.w) return false;
    const Glyph colon=glyph(f,':');
    const int x=n.x+glyph(f,value[0]).advance+glyph(f,value[1]).advance+colon.x;
    const int y=n.y+f.baseline+colon.y;
    bounds={std::max({0,int(n.x),x}),std::max(0,y),
        std::min({Width,int(n.x)+n.w,x+colon.w}),std::min(Height,y+colon.h)};
    return true;
}

void renderBand(const Scene& s,const Frame& frame,int bandY,uint16_t* pixels) {
    if(!pixels || bandY<0 || bandY>Height-BandHeight) return;
    std::fill(pixels,pixels+Width*BandHeight,s.background);
    Painter p{pixels,bandY};
    for(unsigned i=0;i<std::min<unsigned>(s.count,MaxNodes);++i) {
        const Node& n=s.nodes[i];
        if(!intersectsBand(n,bandY)) continue;
        const uint8_t level=(n.pulse || n.kind==Kind::Clock)?pulseLevel(n,frame.tickMs):255;
        const uint16_t color=n.pulse?scale(n.color,level):n.color;
        switch(n.kind) {
        case Kind::Text:
            if(n.textOffset<=TextCapacity && n.textLength<=TextCapacity-n.textOffset)
                text(p,n,s.text+n.textOffset,n.textLength,color,false,level);
            break;
        case Kind::Clock: {
            char value[6];const unsigned minute=frame.minuteOfDay%1440;
            std::snprintf(value,sizeof(value),"%02u:%02u",minute/60,minute%60);
            text(p,n,value,5,n.color,true,level);break;
        }
        case Kind::Rect: p.rect(n.x,n.y,n.w,n.h,color,n.fill);break;
        case Kind::Pixel: p.pixel(n.x,n.y,color);break;
        case Kind::Line: p.line(n.x,n.y,n.x1,n.y1,color);break;
        case Kind::Circle: p.ellipse(n.x,n.y,n.r,n.r,color,n.fill);break;
        case Kind::Ellipse: p.ellipse(n.x,n.y,n.r,n.ry,color,n.fill);break;
        case Kind::Triangle:
            if(!n.fill) { p.line(n.x,n.y,n.x1,n.y1,color);p.line(n.x1,n.y1,n.x2,n.y2,color);p.line(n.x2,n.y2,n.x,n.y,color); }
            else for(int y=std::max<int>(bandY,std::min({n.y,n.y1,n.y2}));y<=std::min<int>(bandY+BandHeight-1,std::max({n.y,n.y1,n.y2}));++y)
                for(int x=std::max<int>(0,std::min({n.x,n.x1,n.x2}));x<=std::min<int>(Width-1,std::max({n.x,n.x1,n.x2}));++x) {
                    const auto a=edge(n.x,n.y,n.x1,n.y1,x,y),b=edge(n.x1,n.y1,n.x2,n.y2,x,y),c=edge(n.x2,n.y2,n.x,n.y,x,y);
                    if((a>=0&&b>=0&&c>=0)||(a<=0&&b<=0&&c<=0)) p.pixel(x,y,color);
                }
            break;
        case Kind::RoundRect: {
            const int radius=std::max(0,std::min<int>(n.r,std::min(n.w,n.h)/2));
            for(int y=std::max<int>(bandY,n.y);y<std::min<int>(bandY+BandHeight,n.y+n.h);++y) {
                int inset=0;
                if(radius>0) {
                    const int dy=y<n.y+radius?n.y+radius-y:(y>=n.y+n.h-radius?y-(n.y+n.h-radius-1):0);
                    inset=radius-int(std::sqrt(std::max(0,radius*radius-dy*dy)));
                }
                if(n.fill || y==n.y || y==n.y+n.h-1) p.span(n.x+inset,y,n.w-2*inset,color);
                else {p.pixel(n.x+inset,y,color);p.pixel(n.x+n.w-inset-1,y,color);}
            }break;
        }
        }
    }
}
uint32_t rowHash(const uint16_t* pixels) {
    uint32_t hash=2166136261u;
    for(int x=0;x<Width;++x) {hash^=pixels[x];hash*=16777619u;}
    return hash;
}
} // namespace scene
