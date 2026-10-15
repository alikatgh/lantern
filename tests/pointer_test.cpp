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
    // A small legacy window crops the minimum 400x240 presentation.
    point(lt::mapPointer(150,90,true,300,180,300,180,false),200,120,true);
    point(lt::mapPointer(-1,90,true,300,180,300,180,false),49,120,false);
    point(lt::mapPointer(300,90,true,300,180,300,180,false),350,120,false);
    point(lt::mapPointer(0,0,true,0,240,400,240,false),0,0,false);
    point(lt::mapPointer(0,0,true,400,240,0,0,true),0,0,false);
    point(lt::mapPointer(std::numeric_limits<float>::quiet_NaN(),0,true,
                         400,240,400,240,false),0,0,false);
}
void mouse(Uint32 type, Uint32 windowID, int x, int y) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.windowID = windowID; e.motion.x = x; e.motion.y = y;
    } else {
        e.button.windowID = windowID; e.button.button = SDL_BUTTON_LEFT;
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
    poll(); expect(!lt_touch_pressed(), "fast click edge lasts one frame");
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
lt.cursor_visible(true)
)");
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE,"Wick restores visible cursor");
    for (int i=0; i<3; ++i) lt_cursor_visible(1);
    expect(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE,"repeated modal visibility is idempotent");
    // Native resize remaps the unchanged window-point position lazily.
    SDL_SetWindowSize(win,1000,666); poll();
    SDL_GetWindowSize(win,&ww,&wh);
    mouse(SDL_MOUSEMOTION,id,ww/2,wh/2); poll();
    near(lt_pointer_x(),200,"resize maps content center x");
    near(lt_pointer_y(),120,"resize maps content center y");
    mouse(SDL_MOUSEMOTION,id,ww/2,0); poll();
    expect(!lt_pointer_inside(),"letterbox rejects hover");
    mouse(SDL_MOUSEMOTION,id,-1,wh/2); poll();
    expect(!lt_pointer_inside(),"outside-window coordinates reject hover");
    lt_shutdown();
    expect(!lt_pointer_inside(),"shutdown has no pointer");
}
} // namespace
int main() {
    try {
        mappingChecks();
        wick::Diagnostic d;
        expect(checkWickSource("lt.cursor_visible(true)\nlet x: num = lt.pointer_x()\nlet y: num = lt.pointer_y()\nlet inside: bool = lt.pointer_inside()", "pointer.wick",d),"compile-only new API");
        expect(!checkWickSource("lt.cursor_visible(1)","pointer.wick",d),"typed cursor API rejects numeric arguments");
        expect(!checkWickSource("let x: bool = lt.pointer_x()","pointer.wick",d),"typed pointer API rejects wrong result type");
        eventChecks("1",false); eventChecks("2",false);
        eventChecks("4",false); eventChecks("",true);
        std::cout << "desktop pointer: " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& e) {
        lt_shutdown(); std::cerr << e.what() << '\n'; return 1;
    }
}
