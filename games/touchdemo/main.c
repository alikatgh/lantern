/* touchdemo — the touch API demo, and the first game that runs on iOS.
 *
 * Tap (or click) anywhere: the ball glides to that spot on the ground,
 * a ring pulse marks the tap, and the HUD shows live touch state. Pure
 * C ABI, no Lua — the same file is compiled into the iOS app, whose host
 * drives touchdemo_init/update/draw from its display-link loop (that's
 * why main() is guarded).
 */
#include <lantern.h>
#include <math.h>
#include <stdio.h>

static int g_ground, g_ball, g_lamp;
static float g_bx = 0, g_bz = 0;         /* ball position (world XZ)   */
static float g_tx = 0, g_tz = 0;         /* target position            */
static double g_pulse = 1e9;             /* seconds since last tap     */

/* The playfield seen by the fixed camera: screen x/y map linearly onto
 * this XZ rectangle (tuned so the whole screen is reachable ground). */
#define FIELD_W 9.0f
#define FIELD_D 6.5f

void touchdemo_init(void) {
    g_ground = lt_mesh_plane(12);
    g_ball = lt_mesh_sphere(14);
    g_lamp = lt_mesh_cylinder(10);
    lt_escape_quits(1);
}

void touchdemo_update(double dt) {
    if (lt_touch_pressed()) g_pulse = 0;
    g_pulse += dt;
    if (lt_touch_down()) {
        g_tx = (lt_touch_x() / 400.0f - 0.5f) * FIELD_W;
        g_tz = (lt_touch_y() / 240.0f - 0.5f) * FIELD_D;
    }
    /* critically-damped-ish glide */
    float k = 1.0f - (float)pow(0.002, dt);
    g_bx += (g_tx - g_bx) * k;
    g_bz += (g_tz - g_bz) * k;
}

void touchdemo_draw(void) {
    lt_clear(0.08f, 0.09f, 0.14f);
    lt_camera(0, 7.5f, 5.8f, 0, 0, 0, 55);
    lt_light(0.35f, -1, -0.4f, 0.32f);
    lt_fog(9, 16, 0.08f, 0.09f, 0.14f);

    /* warm lamp that rides with the ball, cool rim from the tap point */
    lt_point_light(0, g_bx, 1.6f, g_bz, 5, 1.0f, 0.85f, 0.6f);
    float pr = (float)(g_pulse < 0.5 ? (0.5 - g_pulse) * 8.0 : 0);
    lt_point_light(1, g_tx, 0.8f, g_tz, 2.5f + pr, 0.4f, 0.7f, 1.0f);

    lt_draw_mesh(g_ground, -1, 0, 0, 0, 0, 0, 0, 12, 1, 9,
                 0.32f, 0.36f, 0.30f);
    /* target marker: a squat cylinder that flattens as the pulse fades */
    float ph = g_pulse < 0.5f ? 0.25f * (0.5f - (float)g_pulse) * 2 : 0.02f;
    lt_draw_mesh(g_lamp, -1, g_tx, ph / 2, g_tz, 0, 0, 0, 0.7f, ph, 0.7f,
                 0.45f, 0.75f, 1.0f);
    lt_shadow(g_bx, 0.02f, g_bz, 0.55f, 0.4f);
    lt_draw_mesh(g_ball, -1, g_bx, 0.5f, g_bz, 0, 0, 0, 1, 1, 1,
                 0.95f, 0.8f, 0.35f);

    /* HUD: live touch state, plus a crosshair while touching */
    char line[64];
    snprintf(line, sizeof line, "TOUCH %s  X %3.0f  Y %3.0f",
             lt_touch_down() ? "DOWN" : "UP  ", lt_touch_x(), lt_touch_y());
    lt_print(line, 4, 4, 1, 1, 1, 1);
    lt_print("TAP TO MOVE THE BALL", 4, 228, 0.7f, 0.75f, 0.85f, 1);
    if (lt_touch_down()) {
        lt_rect(lt_touch_x() - 4, lt_touch_y(), 9, 1, 1, 1, 1, 0.8f);
        lt_rect(lt_touch_x(), lt_touch_y() - 4, 1, 9, 1, 1, 1, 0.8f);
    }
}

#ifndef LANTERN_NO_MAIN
int main(void) {
    if (!lt_boot("lantern touch demo", 3)) return 1;
    touchdemo_init();
    lt_run(touchdemo_update, touchdemo_draw);
    lt_shutdown();
    return 0;
}
#endif
