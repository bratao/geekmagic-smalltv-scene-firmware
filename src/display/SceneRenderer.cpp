#include "display/SceneRenderer.h"
#include "display/DisplayManager.h"
#include "../scene/SceneCore.h"
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

namespace {
struct State {
    scene::Scene scene;
    uint16_t band[scene::Width * scene::BandHeight];
    uint32_t hashes[scene::Height] = {};
    uint32_t epoch = 0, previousMs = 0, lastFrameMs = 0;
    uint64_t elapsedMs = 0;
    int32_t timezone = 0;
    uint32_t frames = 0, rows = 0, skipped = 0, lastRenderUs = 0;
    uint32_t revision = 0, renderedRevision = 0, maxRenderUs = 0, lastPixels = 0;
    uint16_t previousMinute = 0xffff;
    uint8_t previousLevels[scene::MaxNodes] = {};
    bool dirtyAll = true;
    bool fresh = true;
};
State* state = nullptr;

bool nameString(JsonVariantConst value) {
    if (!value.is<const char*>()) return false;
    const JsonString text=value.as<JsonString>();
    return text.c_str() && text.size()==strlen(text.c_str());
}

bool integer(JsonObjectConst obj, const char* key, int32_t low, int32_t high, int32_t fallback, int32_t& out) {
    if (!obj.containsKey(key)) { out = fallback; return true; }
    JsonVariantConst value = obj[key];
    if (!value.is<int32_t>()) return false;
    out = value.as<int32_t>();
    return out >= low && out <= high;
}
bool color(JsonObjectConst obj, const char* key, uint16_t fallback, uint16_t& out) {
    out = fallback;
    if (!obj.containsKey(key)) return true;
    if (!obj[key].is<const char*>()) return false;
    const JsonString raw = obj[key].as<JsonString>();
    const char* text = raw.c_str();
    if (!text || raw.size() != strlen(text)) return false;
    if (*text == '#') ++text;
    if (strlen(text) != 6) return false;
    uint32_t rgb = 0;
    for (int i=0; i<6; ++i) {
        const char c = text[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c-'0';
        else if (c >= 'a' && c <= 'f') digit = c-'a'+10;
        else if (c >= 'A' && c <= 'F') digit = c-'A'+10;
        else return false;
        rgb = (rgb << 4) | digit;
    }
    out = uint16_t(((rgb>>8)&0xf800) | ((rgb>>5)&0x07e0) | ((rgb>>3)&31));
    return true;
}
bool boolean(JsonObjectConst obj, const char* key, bool fallback, bool& out) {
    if (!obj.containsKey(key)) { out = fallback; return true; }
    if (!obj[key].is<bool>()) return false;
    out = obj[key].as<bool>(); return true;
}
bool parseNode(JsonObjectConst obj, scene::Scene& output) {
    if (!nameString(obj["type"])) return false;
    const char* type = obj["type"];
    scene::Node node;
    if (!strcmp(type,"text")) node.kind=scene::Kind::Text;
    else if (!strcmp(type,"clock")) { node.kind=scene::Kind::Clock; node.font=scene::FontId::Clock; }
    else if (!strcmp(type,"rect")) node.kind=scene::Kind::Rect;
    else if (!strcmp(type,"line")) node.kind=scene::Kind::Line;
    else if (!strcmp(type,"pixel")) node.kind=scene::Kind::Pixel;
    else if (!strcmp(type,"circle")) node.kind=scene::Kind::Circle;
    else if (!strcmp(type,"ellipse")) node.kind=scene::Kind::Ellipse;
    else if (!strcmp(type,"triangle")) node.kind=scene::Kind::Triangle;
    else if (!strcmp(type,"roundrect")) node.kind=scene::Kind::RoundRect;
    else return false;
    const char* coordinates[] = {"x","y","x1","y1","x2","y2","w","h","r","ry"};
    int16_t* values[] = {&node.x,&node.y,&node.x1,&node.y1,&node.x2,&node.y2,&node.w,&node.h,&node.r,&node.ry};
    for (unsigned i=0;i<10;++i) {
        int32_t value;
        if (!integer(obj,coordinates[i],i<6?-480:0,480,*values[i],value)) return false;
        *values[i]=int16_t(value);
    }
    if (node.kind==scene::Kind::Line || node.kind==scene::Kind::Triangle) {
        int32_t x,y;
        if (!integer(obj,"x0",-480,480,node.x,x) || !integer(obj,"y0",-480,480,node.y,y)) return false;
        node.x=int16_t(x);node.y=int16_t(y);
    }
    if (node.kind==scene::Kind::Ellipse) {
        int32_t radius;
        if (!integer(obj,"rx",0,480,node.r,radius)) return false;
        node.r=int16_t(radius);
    }
    if (obj.containsKey("font")) {
        if (!nameString(obj["font"])) return false;
        const char* name=obj["font"];
        if (!strcmp(name,"small")) node.font=scene::FontId::Small;
        else if (!strcmp(name,"meta")) node.font=scene::FontId::Meta;
        else if (!strcmp(name,"heading")) node.font=scene::FontId::Heading;
        else if (!strcmp(name,"title")) node.font=scene::FontId::Title;
        else if (!strcmp(name,"clock")) node.font=scene::FontId::Clock;
        else return false;
    }
    int32_t period,low,high;
    if (!integer(obj,"period_ms",1000,60000,4000,period) ||
        !integer(obj,"min",0,255,128,low) || !integer(obj,"max",0,255,240,high) || low>high ||
        !color(obj,"color",0xffff,node.color) || !boolean(obj,"fill",true,node.fill) ||
        !boolean(obj,"pulse",false,node.pulse)) return false;
    node.period=uint16_t(period);node.low=uint8_t(low);node.high=uint8_t(high);
    if (node.kind==scene::Kind::Text) {
        if (!obj["text"].is<const char*>()) return false;
        const JsonString text=obj["text"].as<JsonString>();
        if (!text.c_str() || text.size()!=strlen(text.c_str()) || text.size()>512 ||
            text.size()>scene::TextCapacity-output.textUsed) return false;
        node.textOffset=output.textUsed;node.textLength=uint16_t(text.size());
        memcpy(output.text+output.textUsed,text.c_str(),text.size());
        output.textUsed+=node.textLength;
    }
    output.nodes[output.count++]=node;
    return true;
}
} // namespace

bool SceneRenderer::submit(JsonObjectConst document, String& error) {
    if (!nameString(document["mode"]) || strcmp(document["mode"],"scene") ||
        !document["commands"].is<JsonArrayConst>() || document["commands"].size()>scene::MaxNodes ||
        !document["epoch"].is<uint32_t>()) { error="invalid scene envelope";return false; }
    int32_t timezone;
    if (!integer(document,"tz_offset",-50400,50400,0,timezone)) {error="invalid timezone";return false;}
    std::unique_ptr<scene::Scene> candidate(new (std::nothrow) scene::Scene());
    if (!candidate) {error="scene allocation failed";return false;}
    if (!color(document,"bg",0,candidate->background)) {error="invalid background";return false;}
    for (JsonVariantConst item:document["commands"].as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>() || !parseNode(item.as<JsonObjectConst>(),*candidate)) {
            error="invalid scene node";return false;
        }
    }
    // Preserve both the running GIF and previous scene until all validation and allocation succeeds.
    State* target=state;
    if (!target) target=new (std::nothrow) State();
    if (!target) {error="renderer allocation failed";return false;}
    DisplayManager::stopGif(true);
    target->scene=*candidate;
    target->epoch=document["epoch"].as<uint32_t>();target->timezone=timezone;
    target->previousMs=millis();target->lastFrameMs=target->previousMs-100;
    target->elapsedMs=0;
    target->dirtyAll=true;++target->revision;
    // Existing row hashes remain valid: scene changes redraw only different rows.
    state=target;
    return true;
}
void SceneRenderer::stop() { delete state;state=nullptr; }
bool SceneRenderer::active() {return state!=nullptr;}
void SceneRenderer::update() {
    if (!state) return;
    State& s=*state;
    const uint32_t now=millis();
    s.elapsedMs+=uint32_t(now-s.previousMs);s.previousMs=now;
    // NTP owns the device wall clock; request epoch is only an offline fallback.
    const time_t synced=time(nullptr);
    const int64_t seconds=(synced>1600000000?int64_t(synced):int64_t(s.epoch)+int64_t(s.elapsedMs/1000))+s.timezone;
    const int64_t daySeconds=((seconds%86400)+86400)%86400;
    const scene::Frame frame{uint16_t(daySeconds/60),now};
    const bool minuteChanged=frame.minuteOfDay!=s.previousMinute;
    if (!s.dirtyAll && !minuteChanged && uint32_t(now-s.lastFrameMs)<100) return;
    s.lastFrameMs=now;
    const uint32_t started=micros();
    uint32_t dirtyBands=s.dirtyAll?((uint32_t(1)<<(scene::Height/scene::BandHeight))-1):0;
    for (unsigned i=0;i<s.scene.count;++i) {
        const scene::Node& node=s.scene.nodes[i];
        const bool clock=node.kind==scene::Kind::Clock;
        if (!clock && !node.pulse) continue;
        const uint8_t level=scene::pulseLevel(node,frame.tickMs);
        if ((clock && minuteChanged) || ((clock || node.pulse) && level!=s.previousLevels[i])) {
            for (int band=0;band<scene::Height/scene::BandHeight;++band)
                if (scene::intersectsBand(node,band*scene::BandHeight)) dirtyBands|=uint32_t(1)<<band;
        }
        s.previousLevels[i]=level;
    }
    s.previousMinute=frame.minuteOfDay;
    s.lastPixels=0;
    for (int y=0;y<scene::Height;y+=scene::BandHeight) {
        if (!(dirtyBands & (uint32_t(1)<<(y/scene::BandHeight)))) continue;
        scene::renderBand(s.scene,frame,y,s.band);
        bool changed[scene::BandHeight] = {};
        for (int row=0;row<scene::BandHeight;++row) {
            uint16_t* pixels=s.band+row*scene::Width;
            const uint32_t hash=scene::rowHash(pixels);
            if (s.fresh || s.hashes[y+row]!=hash) {
                changed[row]=true;
                s.hashes[y+row]=hash;++s.rows;
            } else ++s.skipped;
        }
        // One SPI transfer per consecutive run; no intermediate clearing.
        for (int row=0;row<scene::BandHeight;) {
            if (!changed[row]) { ++row;continue; }
            const int first=row;
            while(row<scene::BandHeight && changed[row]) ++row;
            const int height=row-first;
            DisplayManager::getGfx()->draw16bitRGBBitmap(
                0,y+first,s.band+first*scene::Width,scene::Width,height);
            s.lastPixels+=scene::Width*height;
        }
        yield();
    }
    s.fresh=false;s.dirtyAll=false;s.renderedRevision=s.revision;
    if (dirtyBands) ++s.frames;
    s.lastRenderUs=micros()-started;
    if (s.lastRenderUs>s.maxRenderUs) s.maxRenderUs=s.lastRenderUs;
}
void SceneRenderer::status(JsonObject result) {
    result["active"]=active();result["free_heap"]=ESP.getFreeHeap();
    result["max_free_block"]=ESP.getMaxFreeBlockSize();result["reset_reason"]=ESP.getResetReason();
    result["uptime_ms"]=millis();result["heap_fragmentation"]=ESP.getHeapFragmentation();
    result["state_bytes"]=state?sizeof(State):0;result["max_nodes"]=scene::MaxNodes;
    result["text_capacity"]=scene::TextCapacity;
    if (state) {
        result["nodes"]=state->scene.count;result["text_bytes"]=state->scene.textUsed;
        result["frames"]=state->frames;result["rows_drawn"]=state->rows;
        result["rows_skipped"]=state->skipped;result["last_render_us"]=state->lastRenderUs;
        result["revision"]=state->revision;result["rendered_revision"]=state->renderedRevision;
        result["max_render_us"]=state->maxRenderUs;result["last_pixels"]=state->lastPixels;
    }
}
