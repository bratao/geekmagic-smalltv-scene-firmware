#include "../src/scene/SceneCore.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

// Model only the renderer's invalidation/packing/display writes. Every resulting
// display pixel is checked against an independently complete rasterization.
void overlappingSequence(bool secondPulse) {
    scene::Scene s;s.count=secondPulse?3:2;
    s.nodes[0].kind=scene::Kind::Clock;s.nodes[0].font=scene::FontId::Clock;
    s.nodes[0].x=10;s.nodes[0].y=5;s.nodes[0].low=0;s.nodes[0].high=255;
    s.nodes[1].kind=scene::Kind::Rect;s.nodes[1].x=60;s.nodes[1].y=12;
    s.nodes[1].w=20;s.nodes[1].h=13;s.nodes[1].color=0x07e0;
    s.nodes[2].kind=scene::Kind::Rect;s.nodes[2].x=180;s.nodes[2].y=18;
    s.nodes[2].w=17;s.nodes[2].h=7;s.nodes[2].color=0xf800;s.nodes[2].pulse=true;
    s.nodes[2].period=3000;
    uint16_t display[scene::Width*scene::Height]={};
    uint16_t bandPixels[scene::Width*scene::BandHeight];
    uint16_t reference[scene::Width*scene::BandHeight];
    uint32_t hashes[scene::Height]={};uint8_t previousLevels[scene::MaxNodes]={};
    uint16_t previousMinute=0xffff;
    for(int step=0;step<80;++step) {
        const bool fresh=step==0, dirtyAll=fresh || step==40;
        if(step==40) {s.background=0x0001;s.nodes[1].x=54;s.nodes[1].h=21;}
        const scene::Frame frame{uint16_t(step<20?599:step<60?600:601),uint32_t(step*100)};
        const bool minuteChanged=frame.minuteOfDay!=previousMinute;
        uint32_t dirty=dirtyAll?((1u<<30)-1):0;
        uint8_t left[30],right[30];memset(left,dirtyAll?0:240,sizeof(left));memset(right,dirtyAll?240:0,sizeof(right));
        for(unsigned i=0;i<s.count;++i) {
            const auto& n=s.nodes[i];const bool clock=n.kind==scene::Kind::Clock;
            if(!clock && !n.pulse) continue;
            const uint8_t level=scene::pulseLevel(n,frame.tickMs);
            if((clock && minuteChanged) || level!=previousLevels[i]) {
                scene::Bounds bounds{};
                const bool colon=clock && !minuteChanged && scene::clockPulseBounds(n,frame.minuteOfDay,bounds);
                for(int b=0;b<30;++b) {
                    const int y=b*scene::BandHeight;
                    if(colon?(bounds.right>bounds.left && bounds.bottom>y && bounds.top<y+scene::BandHeight):scene::intersectsBand(n,y)) {
                        dirty|=1u<<b;left[b]=std::min<int>(left[b],colon?bounds.left:0);
                        right[b]=std::max<int>(right[b],colon?bounds.right:240);
                    }
                }
            }
            previousLevels[i]=level;
        }
        previousMinute=frame.minuteOfDay;
        for(int b=0;b<30;++b) {
            const int y=b*scene::BandHeight;
            if(dirty&(1u<<b)) {
                scene::renderBand(s,frame,y,bandPixels);
                bool changed[scene::BandHeight]={};
                for(int row=0;row<scene::BandHeight;++row) {
                    const uint32_t hash=scene::rowHash(bandPixels+row*scene::Width);
                    changed[row]=fresh || hashes[y+row]!=hash;hashes[y+row]=hash;
                }
                for(int row=0;row<scene::BandHeight;) {
                    if(!changed[row]) {++row;continue;}
                    const int first=row;while(row<scene::BandHeight && changed[row]) ++row;
                    const int height=row-first,x=left[b],width=right[b]-x;
                    if(width<scene::Width) for(int r=0;r<height;++r)
                        memmove(bandPixels+first*scene::Width+r*width,bandPixels+(first+r)*scene::Width+x,width*sizeof(uint16_t));
                    for(int r=0;r<height;++r) memcpy(display+(y+first+r)*scene::Width+x,
                        bandPixels+first*scene::Width+r*width,width*sizeof(uint16_t));
                }
            }
            scene::renderBand(s,frame,y,reference);
            assert(memcmp(display+y*scene::Width,reference,sizeof(reference))==0);
        }
    }
}

// Compare complete reference pixels, not just hashes, across fonts and clipping.
int main() {
    unsigned cases=0; unsigned long long changed=0;
    uint16_t before[scene::Width*scene::BandHeight],after[scene::Width*scene::BandHeight];
    for(int f=0;f<5;++f) for(int x: {-480,-12,10,210,480})
    for(int y: {-480,-10,5,220,480}) for(int minute: {0,69,599,754,1439}) {
        scene::Scene s; s.count=1;
        auto& n=s.nodes[0]; n.kind=scene::Kind::Clock;n.font=scene::FontId(f);
        n.x=x;n.y=y;n.w=220;n.low=0;n.high=255;
        scene::Bounds b;
        assert(scene::clockPulseBounds(n,minute,b));
        for(int band=0;band<scene::Height;band+=scene::BandHeight) {
            scene::renderBand(s,{uint16_t(minute),0},band,before);
            scene::renderBand(s,{uint16_t(minute),2000},band,after);
            for(int row=0;row<scene::BandHeight;++row) for(int col=0;col<scene::Width;++col)
                if(before[row*scene::Width+col]!=after[row*scene::Width+col]) {
                    assert(col>=b.left && col<b.right && band+row>=b.top && band+row<b.bottom);
                    ++changed;
                }
        }
        ++cases;
    }
    // A truncated clock must use the conservative full-node invalidation path.
    scene::Node narrow; narrow.kind=scene::Kind::Clock;narrow.font=scene::FontId::Clock;narrow.w=10;
    scene::Bounds b;assert(!scene::clockPulseBounds(narrow,754,b));

    // Every pattern of changed rows, including disjoint runs, survives in-place packing.
    for(unsigned mask=1;mask<256;++mask) for(int left: {0,1,65,239}) for(int width: {1,240-left}) {
        uint16_t original[scene::Width*scene::BandHeight], packed[scene::Width*scene::BandHeight];
        for(int i=0;i<scene::Width*scene::BandHeight;++i) original[i]=uint16_t(i);
        memcpy(packed,original,sizeof(packed));
        for(int row=0;row<scene::BandHeight;) {
            if(!(mask&(1u<<row))) {++row;continue;}
            const int first=row;while(row<scene::BandHeight && (mask&(1u<<row))) ++row;
            const int height=row-first;
            for(int r=0;r<height;++r) memmove(packed+first*scene::Width+r*width,
                packed+(first+r)*scene::Width+left,width*sizeof(uint16_t));
            for(int r=0;r<height;++r) for(int col=0;col<width;++col)
                assert(packed[first*scene::Width+r*width+col]==original[(first+r)*scene::Width+left+col]);
        }
    }
    scene::Node clock;clock.kind=scene::Kind::Clock;clock.font=scene::FontId::Clock;clock.x=10;clock.y=5;
    assert(scene::clockPulseBounds(clock,754,b));
    printf("PASS %u clock cases; %llu changed pixels contained; all 2040 packing cases.\n",cases,changed);
    printf("Report clock colon bounds: [%d,%d)-[%d,%d), SPI row width %d vs 240.\n",b.left,b.top,b.right,b.bottom,b.right-b.left);
    overlappingSequence(false);overlappingSequence(true);
    printf("PASS 160 complete-frame comparisons: opaque overlap, second pulse, minute changes and scene replacement.\n");
}
