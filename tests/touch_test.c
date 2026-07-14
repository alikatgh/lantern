/* touch_test.c — ABI-level test of the touch API on the SDL backend.
 *
 * Built to catch: coordinate-mapping bugs (letterbox/HiDPI), edge-detection
 * bugs (_pressed), and state bugs (release keeps last position). It injects
 * mouse events through SDL_PushEvent — the backend tracks touch from events,
 * not SDL_GetMouseState, precisely so this test can drive it. It does NOT
 * catch: real-hardware touch behavior (that's the iOS backend's job).
 */
#include <lantern.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;

static void expect(int cond, const char* what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_fail = 1;
    }
}

static void expectNear(float got, float want, float tol, const char* what) {
    float d = got - want;
    if (d < 0) d = -d;
    if (d > tol) {
        fprintf(stderr, "FAIL: %s — got %.2f, want %.2f (±%.1f)\n", what, got,
                want, tol);
        g_fail = 1;
    }
}

static void pushMouse(Uint32 type, int x, int y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = x;
        e.button.y = y;
    }
    SDL_PushEvent(&e);
}

int main(void) {
    /* scale 3 → 1200x720 window; the frame fills it exactly (no letterbox
     * offset), so window center must map to screen center. */
    if (!lt_boot("touch test", 3)) {
        fprintf(stderr, "boot failed\n");
        return 1;
    }

    /* nothing touched yet */
    lt_frame_poll();
    expect(lt_touch_down() == 0, "initially not down");
    expect(lt_touch_pressed() == 0, "initially not pressed");

    /* press at window center → (200,120), pressed exactly one frame */
    pushMouse(SDL_MOUSEBUTTONDOWN, 600, 360);
    lt_frame_poll();
    expect(lt_touch_down() == 1, "down after press");
    expect(lt_touch_pressed() == 1, "pressed on the press frame");
    expectNear(lt_touch_x(), 200, 1.5f, "center x");
    expectNear(lt_touch_y(), 120, 1.5f, "center y");

    lt_frame_poll();
    expect(lt_touch_down() == 1, "still down next frame");
    expect(lt_touch_pressed() == 0, "pressed is edge-only");

    /* drag to the top-left corner */
    pushMouse(SDL_MOUSEMOTION, 0, 0);
    lt_frame_poll();
    expectNear(lt_touch_x(), 0, 1.5f, "corner x");
    expectNear(lt_touch_y(), 0, 1.5f, "corner y");

    /* drag to the far corner: clamps to the last screen pixel */
    pushMouse(SDL_MOUSEMOTION, 1199, 719);
    lt_frame_poll();
    expectNear(lt_touch_x(), 399, 1.5f, "clamped x");
    expectNear(lt_touch_y(), 239, 1.5f, "clamped y");

    /* release: not down, position sticks */
    pushMouse(SDL_MOUSEBUTTONUP, 1199, 719);
    lt_frame_poll();
    expect(lt_touch_down() == 0, "up after release");
    expect(lt_touch_pressed() == 0, "not pressed after release");
    expectNear(lt_touch_x(), 399, 1.5f, "position sticks after release");

    /* second tap: pressed fires again */
    pushMouse(SDL_MOUSEBUTTONDOWN, 600, 360);
    lt_frame_poll();
    expect(lt_touch_pressed() == 1, "pressed fires on second tap");
    pushMouse(SDL_MOUSEBUTTONUP, 600, 360);
    lt_frame_poll();

    /* sub-frame tap: down AND up land between two polls. down never reads
     * 1, but pressed must still fire once — found live on the iOS
     * simulator, where a synthetic tap is shorter than a frame. */
    pushMouse(SDL_MOUSEBUTTONDOWN, 300, 180);
    pushMouse(SDL_MOUSEBUTTONUP, 300, 180);
    lt_frame_poll();
    expect(lt_touch_down() == 0, "sub-frame tap: down already released");
    expect(lt_touch_pressed() == 1, "sub-frame tap: pressed still fires");
    expectNear(lt_touch_x(), 100, 1.5f, "sub-frame tap x");
    lt_frame_poll();
    expect(lt_touch_pressed() == 0, "sub-frame tap: pressed is one frame");

    lt_shutdown();
    if (g_fail) return 1;
    printf("touch_test: all assertions passed\n");
    return 0;
}
