/* hello_c — proves the lantern C ABI from pure C (no Lua, no C++).
 * Build: the lantern_hello_c CMake target. Drives the loop manually
 * (lt_frame_poll/begin/end) — the other half of the API the Lua host
 * doesn't exercise. LANTERN_SHOT works here too.
 */
#include <lantern.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    if (!lt_boot("lantern hello_c", 3)) return 1;
    int cube = lt_mesh_cube();
    const char* shot = getenv("LANTERN_SHOT");
    double t = 0.0;
    int frame = 0;

    while (lt_frame_poll()) {
        t += lt_frame_dt();
        lt_frame_begin();
        lt_clear(0.10f, 0.08f, 0.16f);
        lt_light(0.4f, -1.0f, -0.3f, 0.35f);
        lt_camera(3.5f * (float)sin(t * 0.6), 2.2f, 3.5f * (float)cos(t * 0.6),
                  0, 0, 0, 55.0f);
        lt_draw_mesh(cube, -1, 0, 0, 0, (float)t * 0.7f, (float)t, 0,
                     1.4f, 1.4f, 1.4f, 0.98f, 0.80f, 0.25f);
        lt_rect(0, (float)lt_screen_h() - 14, (float)lt_screen_w(), 14,
                0.05f, 0.05f, 0.08f, 0.6f);
        lt_print("HELLO FROM C - LANTERN ABI", 4, (float)lt_screen_h() - 11,
                 1, 1, 1, 0.9f);
        lt_frame_end();
        if (shot && frame == 60) {
            char path[512];
            snprintf(path, sizeof path, "%s.bmp", shot);
            lt_screenshot(path);
            break;
        }
        frame++;
    }
    lt_shutdown();
    return 0;
}
