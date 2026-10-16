// Real SDL backend, C ABI and Wick registry. Dummy drivers avoid native GUI
// requirements. Pure mapping fixtures cover geometry unavailable on CI.
#include "../src/wick_host.cpp"
#include "pointer_mapping.hpp"
#include <SDL.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int assertions = 0;
void expect(bool yes, const char* message) {
    ++assertions;
    if (!yes) throw std::runtime_error(message);
}
void near(float got, float expected, const char* message) {
    expect(std::isfinite(got) && std::abs(got - expected) < 0.01f, message);
}
void point(lt::PlatformPointer p, float x, float y, bool inside) {
    near(p.x, x, "mapped pointer x");
    near(p.y, y, "mapped pointer y");
    expect(p.inside == inside, "mapped pointer content membership");
}
void mappingChecks() {
    // Ordinary, Retina and non-uniform backing ratios use window points.
    point(lt::mapPointer(200,120,true,400,240,400,240,false),200,120,true);
    point(lt::mapPointer(200,120,true,400,240,800,480,false),200,120,true);
    point(lt::mapPointer(200,120,true,400,240,800,720,false),200,120,true);
    // Integer-scale letterbox [100,60,800,480] inside a 1000x600 output.
    point(lt::mapPointer(100,60,true,1000,600,1000,600,false),0,0,true);
    point(lt::mapPointer(900,540,true,1000,600,1000,600,false),399,239,false);
    point(lt::mapPointer(99,300,true,1000,600,1000,600,false),0,120,false);
    point(lt::mapPointer(500,59,true,1000,600,1000,600,false),200,0,false);
    point(lt::mapPointer(500,300,false,1000,600,1000,600,false),200,120,false);
    // Native fit has a different rectangle: [0,33,1000,600].
    point(lt::mapPointer(250,166.5f,true,500,333,1000,666,true),200,120,true);
    point(lt::mapPointer(250,15,true,500,333,1000,666,true),200,0,false);
    // An unchanged 5:3 backing surface has no letterbox even after the window
    // point dimensions change. Input must follow the actual presented output.
    point(lt::mapPointer(500,0,true,1000,666,1200,720,false),200,0,true);
    // A small legacy window crops the minimum 400x240 presentation.
    point(lt::mapPointer(150,90,true,300,180,300,180,false),200,120,true);
    point(lt::mapPointer(-1,90,true,300,180,300,180,false),49,120,false);
    point(lt::mapPointer(300,90,true,300,180,300,180,false),350,120,false);
    point(lt::mapPointer(0,0,true,0,240,400,240,false),0,0,false);
    point(lt::mapPointer(0,0,true,400,240,0,0,true),0,0,false);
    point(lt::mapPointer(std::numeric_limits<float>::quiet_NaN(),0,true,
                         400,240,400,240,false),0,0,false);
}
void mouse(Uint32 type, Uint32 windowID, int x, int y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.windowID = windowID; e.motion.x = x; e.motion.y = y;
    } else {
        e.button.windowID = windowID; e.button.button = button;
        e.button.x = x; e.button.y = y;
    }
    expect(SDL_PushEvent(&e) == 1, "push mouse event");
}
void windowEvent(Uint32 windowID, Uint8 event) {
    SDL_Event e{}; e.type = SDL_WINDOWEVENT;
    e.window.windowID = windowID; e.window.event = event;
    expect(SDL_PushEvent(&e) == 1, "push window event");
}
void poll() { expect(lt_frame_poll() != 0, "bounded event poll"); }
void runWick(const std::string& source) {
    auto vm = std::unique_ptr<VM, decltype(&wick::destroy)>(wick::create(), wick::destroy);
    std::string error;
    expect(registerLT(vm.get(), error), "register real Wick signatures");
    expect(wick::load(vm.get(), source, "pointer.wick", error), error.c_str());
}
void mouseButtonChecks(SDL_Window* win) {
    const Uint32 id = SDL_GetWindowID(win);
    int ww=0, wh=0;
    SDL_GetWindowSize(win,&ww,&wh);
    const int cx=ww/2, cy=wh/2;
    const auto oldTouch = lt::platTouch();
    expect(!lt_mouse_pressed(nullptr) && !lt_mouse_pressed("") && !lt_mouse_pressed("RIGHT"),
           "unknown/null mouse names are false");
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEMOTION,id,0,0);
    windowEvent(id,SDL_WINDOWEVENT_LEAVE); poll();
    expect(lt_mouse_pressed("right") && lt_mouse_pressed("right"),
           "fast right click survives release/hover/leave and reads do not consume it");
    expect(!lt_mouse_pressed("left") && !lt_mouse_pressed("middle") && !lt_touch_pressed(),
           "right click never triggers another button or legacy touch");
    expect(lt::platTouch().seq == oldTouch.seq && lt_touch_down() == oldTouch.down,
           "right click preserves old touch sequence/down");
    near(lt_touch_press_x(),oldTouch.pressX,"right click preserves left press x");
    near(lt_touch_press_y(),oldTouch.pressY,"right click preserves left press y");
    expect(lt_touch_press_inside() == oldTouch.pressInside,"right click preserves left press membership");
    runWick(R"(
check(lt.mouse_pressed("right"), "Wick receives fresh right mouse edge")
check(not lt.mouse_pressed("left") and not lt.mouse_pressed("middle"), "Wick buttons are independent")
check(not lt.mouse_pressed("unknown"), "Wick invalid name is false")
)");
    poll(); expect(!lt_mouse_pressed("right"),"right edge lasts one frame");
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_MIDDLE);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_MIDDLE); poll();
    expect(lt_mouse_pressed("middle") && !lt_mouse_pressed("right") && !lt_mouse_pressed("left"),
           "middle is an independent fast edge");
    for (Uint32 foreign : {Uint32(0),id+1000}) {
        mouse(SDL_MOUSEBUTTONDOWN,foreign,cx,cy,SDL_BUTTON_RIGHT);
        mouse(SDL_MOUSEBUTTONUP,foreign,cx,cy,SDL_BUTTON_RIGHT); poll();
        expect(!lt_mouse_pressed("right") && !lt_mouse_pressed("middle"),
               "foreign/unscoped window cannot generate mouse edges");
    }
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_RIGHT); poll();
    expect(lt_mouse_pressed("right"),"held right press begins once");
    poll(); expect(!lt_mouse_pressed("right"),"held right button does not repeat");
    mouse(SDL_MOUSEMOTION,id,0,0); poll();
    expect(!lt_mouse_pressed("right"),"held drag cannot create another edge");
    mouse(SDL_MOUSEBUTTONUP,id,0,0,SDL_BUTTON_RIGHT); poll();
    expect(!lt_mouse_pressed("right"),"right release is not a press");
    // Current pointer reentry cannot turn a rejected DOWN into an activation.
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,0,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEMOTION,id,cx,cy); poll();
    expect(!lt_mouse_pressed("right") && lt_pointer_inside(),"letterbox right DOWN stays rejected after reentry");
    mouse(SDL_MOUSEBUTTONDOWN,id,-1,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_RIGHT); poll();
    expect(!lt_mouse_pressed("right"),"outside-window right DOWN is rejected");
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,0);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy); poll();
    expect(lt_touch_pressed() && !lt_touch_press_inside() && !lt_mouse_pressed("left"),
           "generic left rejects letterbox while legacy outside touch behavior stays unchanged");
    const auto before = lt::platTouch().seq;
    mouse(SDL_MOUSEBUTTONDOWN,id,ww/4,wh/2);
    mouse(SDL_MOUSEBUTTONUP,id,ww/4,wh/2);
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_RIGHT);
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_MIDDLE);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy,SDL_BUTTON_MIDDLE); poll();
    expect(lt_mouse_pressed("left") && lt_mouse_pressed("right") && lt_mouse_pressed("middle"),
           "simultaneous buttons retain independent edges");
    expect(lt_touch_pressed() && lt::platTouch().seq == before+1,"only left contributes to legacy touch sequence");
    const float leftPressX = lt_touch_press_x();
    expect(leftPressX < 200,"later right/middle press cannot overwrite left snapshot");
    SDL_SetWindowSize(win,800,700);
    expect(lt_mouse_pressed("right"),"later resize cannot invalidate already accepted mouse edge");
    near(lt_touch_press_x(),leftPressX,"later resize preserves left snapshot");
    poll(); expect(!lt_mouse_pressed("right"),"resize cannot create another mouse edge");
}
void eventChecks(const char* density, bool native) {
    unsetenv("LANTERN_RENDER_SCALE"); unsetenv("LANTERN_RENDER_NATIVE");
    unsetenv("LANTERN_PAINTED_2D"); unsetenv("LANTERN_RENDER_WORKERS");
    if (native) setenv("LANTERN_RENDER_NATIVE", "1", 1);
    else setenv("LANTERN_RENDER_SCALE", density, 1);
    expect(lt_boot("pointer test",3), "real SDL engine boots");
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE, "native cursor visible by default");
    SDL_Window* win = nullptr;
    for (Uint32 id=1; id<=64 && !win; ++id) win = SDL_GetWindowFromID(id);
    expect(win != nullptr, "find engine window");
    const Uint32 id = SDL_GetWindowID(win);
    int ww=0, wh=0;
    SDL_GetWindowSize(win,&ww,&wh);
    const int cx=ww/2, cy=wh/2;
    poll();
    expect(!lt_touch_press_inside(), "new boot has no stale press membership");
    near(lt_touch_press_x(),0,"new boot press x is reset");
    near(lt_touch_press_y(),0,"new boot press y is reset");
    const float initialTouchX=lt_touch_x(), initialTouchY=lt_touch_y();
    mouse(SDL_MOUSEMOTION,id,cx,cy); poll();
    expect(lt_pointer_inside(), "hover enters content without pressing");
    near(lt_pointer_x(),200,"hover logical x at every density");
    near(lt_pointer_y(),120,"hover logical y at every density");
    expect(!lt_touch_down() && !lt_touch_pressed(), "hover never generates touch");
    near(lt_touch_x(),initialTouchX,"hover preserves last touch x");
    near(lt_touch_y(),initialTouchY,"hover preserves last touch y");
    windowEvent(id,SDL_WINDOWEVENT_LEAVE); poll();
    expect(!lt_pointer_inside(), "leave suppresses hover at last in-content position");
    mouse(SDL_MOUSEMOTION,id,cx,cy); poll();
    expect(lt_pointer_inside(), "reentry motion restores hover");
    mouse(SDL_MOUSEMOTION,id+1000,0,0); poll();
    near(lt_pointer_x(),200,"other window motion is ignored");
    // A sub-frame click followed by hover elsewhere preserves its original
    // touch/activation location, while pointer coordinates continue moving.
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy);
    mouse(SDL_MOUSEMOTION,id,0,0); poll();
    expect(lt_touch_pressed() && !lt_touch_down(), "fast click edge survives release");
    near(lt_touch_x(),200,"fast click keeps press x after subsequent hover");
    near(lt_touch_y(),120,"fast click keeps press y after subsequent hover");
    near(lt_pointer_x(),0,"pointer follows subsequent hover x");
    near(lt_pointer_y(),0,"pointer follows subsequent hover y");
    near(lt_touch_press_x(),200,"fast click snapshot x survives subsequent hover");
    near(lt_touch_press_y(),120,"fast click snapshot y survives subsequent hover");
    expect(lt_touch_press_inside(),"fast click snapshot retains content membership");
    poll(); expect(!lt_touch_pressed(), "fast click edge lasts one frame");
    const auto beforeForeign = lt::platTouch().seq;
    for (Uint32 foreign : {Uint32(0), id + 1000}) {
        mouse(SDL_MOUSEBUTTONDOWN,foreign,0,0);
        mouse(SDL_MOUSEMOTION,foreign,0,0);
        mouse(SDL_MOUSEBUTTONUP,foreign,0,0); poll();
        expect(!lt_touch_pressed() && !lt_touch_down(),"foreign/unscoped window cannot generate a press");
        expect(lt::platTouch().seq == beforeForeign,"foreign window cannot mutate press sequence");
        near(lt_touch_press_x(),200,"foreign window cannot mutate snapshot x");
    }
    // Down and held drag in the SAME queue drain: old touch tracks the drag;
    // the additive activation snapshot stays at the down location.
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy);
    mouse(SDL_MOUSEMOTION,id,0,0); poll();
    expect(lt_touch_pressed() && lt_touch_down(),"same-poll press plus drag retains edge");
    near(lt_touch_x(),0,"old touch x retains drag semantics");
    near(lt_touch_y(),0,"old touch y retains drag semantics");
    near(lt_touch_press_x(),200,"same-poll drag cannot move press x");
    near(lt_touch_press_y(),120,"same-poll drag cannot move press y");
    expect(lt_touch_press_inside(),"same-poll drag cannot change press membership");
    mouse(SDL_MOUSEBUTTONUP,id,0,0); poll();
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy);
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy);
    windowEvent(id,SDL_WINDOWEVENT_LEAVE); poll();
    expect(lt_touch_pressed() && !lt_touch_down() && !lt_pointer_inside(),"released inside tap can leave before poll");
    expect(lt_touch_press_inside(),"window leave cannot invalidate an accepted inside press");
    near(lt_touch_press_x(),200,"leave keeps press x");
    near(lt_touch_press_y(),120,"leave keeps press y");
    const auto beforeMultiple = lt::platTouch().seq;
    mouse(SDL_MOUSEBUTTONDOWN,id,0,0);
    mouse(SDL_MOUSEBUTTONUP,id,0,0);
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy);
    mouse(SDL_MOUSEMOTION,id,0,0); poll();
    expect(lt::platTouch().seq == beforeMultiple + 2,"multiple begins advance the existing sequence per press");
    near(lt_touch_press_x(),200,"multiple begins select last press x");
    near(lt_touch_press_y(),120,"multiple begins select last press y");
    expect(lt_touch_press_inside(),"multiple begins select last press membership");
    mouse(SDL_MOUSEBUTTONUP,id,0,0); poll();
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy); poll();
    expect(lt_touch_down() && lt_touch_pressed(), "held click begins once");
    poll(); expect(lt_touch_down() && !lt_touch_pressed(), "held click does not repeat");
    mouse(SDL_MOUSEBUTTONUP,id,cx,cy); poll();
    lt_cursor_visible(0);
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE,"cursor can be hidden");
    mouse(SDL_MOUSEMOTION,id,cx,cy); poll();
    expect(lt_pointer_inside(),"hidden cursor still tracks hover");
    runWick(R"(
check(lt.pointer_inside(), "Wick receives inside")
check(lt.pointer_x() == 200 and lt.pointer_y() == 120, "Wick receives logical hover")
check(lt.touch_press_inside(), "Wick receives press content membership")
check(lt.touch_press_x() == 200 and lt.touch_press_y() == 120, "Wick receives the accepted press snapshot")
lt.cursor_visible(true)
)");
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE,"Wick restores visible cursor");
    for (int i=0; i<3; ++i) lt_cursor_visible(1);
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE,"repeated modal visibility is idempotent");
    mouse(SDL_MOUSEBUTTONDOWN,id,ww/4,wh/4);
    mouse(SDL_MOUSEBUTTONUP,id,ww/4,wh/4); poll();
    const float beforeResizePressX = lt_touch_press_x(), beforeResizePressY = lt_touch_press_y();
    near(beforeResizePressX,100,"off-center press x before resize");
    near(beforeResizePressY,60,"off-center press y before resize");
    // Native resize remaps the unchanged window-point position lazily.
    SDL_SetWindowSize(win,1000,666); poll();
    // SDL3's software renderer (used through sdl2-compat on CI) refreshes its
    // cached window surface when a real render command queue is activated.
    // Pumping input alone need not change the reported backing output yet.
    int beforeW=0,beforeH=0;
    expect(SDL_GetRendererOutputSize(SDL_GetRenderer(win),&beforeW,&beforeH) == 0,
           "measure output before resized surface activation");
    lt_frame_begin(); lt_frame_end(); poll();
    SDL_GetWindowSize(win,&ww,&wh);
    int dw=0,dh=0;
    expect(SDL_GetRendererOutputSize(SDL_GetRenderer(win),&dw,&dh) == 0,
           "measure resized renderer output");
    std::cerr << "pointer resize: window " << ww << 'x' << wh
              << " output before " << beforeW << 'x' << beforeH
              << " after " << dw << 'x' << dh << " native=" << native << '\n';
    expect(dw > 0 && dh > 0 && dw*3 != dh*5,
           "resized drawable really has non-5:3 aspect");
    const auto content=lt::presentationRect(dw,dh,native);
    expect(content.y > 0,"measured presentation really has a top letterbox");
    mouse(SDL_MOUSEMOTION,id,ww/2,wh/2); poll();
    near(lt_pointer_x(),200,"resize maps content center x");
    near(lt_pointer_y(),120,"resize maps content center y");
    // Resizing changes current touch/hover mapping, never the old projection
    // bound to the most recent press edge.
    near(lt_touch_press_x(),beforeResizePressX,"resize cannot reproject press x");
    near(lt_touch_press_y(),beforeResizePressY,"resize cannot reproject press y");
    expect(lt_touch_press_inside(),"resize cannot reproject press membership");
    mouse(SDL_MOUSEBUTTONDOWN,id,ww/2,0);
    mouse(SDL_MOUSEMOTION,id,ww/2,wh/2);
    mouse(SDL_MOUSEBUTTONUP,id,ww/2,wh/2); poll();
    expect(lt_touch_pressed() && lt_pointer_inside(),"letterbox begin can drag to content before poll");
    expect(!lt_touch_press_inside(),"letterbox press stays rejected after a drag into content");
    near(lt_touch_press_y(),0,"letterbox press snapshot stays clamped at top");
    mouse(SDL_MOUSEBUTTONDOWN,id,ww/2,wh/2);
    mouse(SDL_MOUSEBUTTONUP,id,ww/2,wh/2);
    windowEvent(id,SDL_WINDOWEVENT_LEAVE); poll();
    expect(lt_touch_pressed() && !lt_pointer_inside() && lt_touch_press_inside(),"inside fast press survives leave after resize");
    mouse(SDL_MOUSEBUTTONDOWN,id,-1,wh/2);
    mouse(SDL_MOUSEBUTTONUP,id,-1,wh/2);
    mouse(SDL_MOUSEMOTION,id,ww/2,wh/2); poll();
    expect(lt_touch_pressed() && !lt_touch_press_inside() && lt_pointer_inside(),"outside-window press stays rejected after reentry");
    mouse(SDL_MOUSEMOTION,id,ww/2,0); poll();
    expect(!lt_pointer_inside(),"letterbox rejects hover");
    mouse(SDL_MOUSEMOTION,id,-1,wh/2); poll();
    expect(!lt_pointer_inside(),"outside-window coordinates reject hover");
    mouseButtonChecks(win);
    lt_shutdown();
    expect(!lt_pointer_inside(),"shutdown has no pointer");
    expect(!lt_touch_press_inside(),"shutdown clears press membership");
    near(lt_touch_press_x(),0,"shutdown clears press x");
    near(lt_touch_press_y(),0,"shutdown clears press y");
    expect(!lt_mouse_pressed("left") && !lt_mouse_pressed("right") && !lt_mouse_pressed("middle"),
           "shutdown clears all mouse edges");
    // Direct backend poll without a renderer must ignore every mouse press.
    expect(SDL_Init(SDL_INIT_EVENTS) == 0,"initialize event queue only, without a renderer");
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy);
    mouse(SDL_MOUSEBUTTONDOWN,id,cx,cy,SDL_BUTTON_RIGHT);
    expect(lt::platPoll(),"unbooted backend poll cannot create a press");
    expect(lt::platTouch().seq == 0 && !lt::platTouch().pressInside,"missing renderer cannot latch stale input");
    const auto noMouse = lt::platMouseButtons();
    expect(noMouse.left == 0 && noMouse.middle == 0 && noMouse.right == 0,
           "missing renderer cannot latch any mouse button");
    SDL_Quit();
}
} // namespace
int main() {
    try {
        expect(!lt_touch_press_inside(),"before boot has no press membership");
        near(lt_touch_press_x(),0,"before boot press x");
        near(lt_touch_press_y(),0,"before boot press y");
        expect(!lt_mouse_pressed("right"),"before boot has no mouse edge");
        mappingChecks();
        wick::Diagnostic d;
        expect(checkWickSource("lt.cursor_visible(true)\nlet x: num = lt.pointer_x()\nlet y: num = lt.pointer_y()\nlet inside: bool = lt.pointer_inside()", "pointer.wick",d),"compile-only new API");
        expect(!checkWickSource("lt.cursor_visible(1)","pointer.wick",d),"typed cursor API rejects numeric arguments");
        expect(!checkWickSource("let x: bool = lt.pointer_x()","pointer.wick",d),"typed pointer API rejects wrong result type");
        expect(checkWickSource("let x: num = lt.touch_press_x()\nlet y: num = lt.touch_press_y()\nlet inside: bool = lt.touch_press_inside()", "press.wick",d),"compile typed press snapshot API");
        expect(!checkWickSource("let x: bool = lt.touch_press_x()","press.wick",d),"press x rejects wrong result type");
        expect(!checkWickSource("let inside: num = lt.touch_press_inside()","press.wick",d),"press membership rejects wrong result type");
        expect(checkWickSource("let pressed: bool = lt.mouse_pressed(\"right\")","mouse.wick",d),"compile typed mouse API");
        expect(!checkWickSource("lt.mouse_pressed(1)","mouse.wick",d),"mouse API rejects numeric button argument");
        expect(!checkWickSource("let pressed: num = lt.mouse_pressed(\"right\")","mouse.wick",d),"mouse API rejects wrong result type");
        eventChecks("1",false); eventChecks("2",false);
        eventChecks("4",false); eventChecks("",true);
        std::cout << "desktop pointer: " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& e) {
        lt_shutdown(); std::cerr << e.what() << '\n'; return 1;
    }
}
