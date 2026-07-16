/* lantern.h — the lantern engine C ABI (v0.3).
 *
 * This is the complete engine surface. C/C++ games link lantern_core and call
 * these; the bundled Lua host is itself a client of this header (dogfooding).
 *
 * Conventions:
 *  - The internal render target is ALWAYS 400x240 (lt_screen_w/h). 2D
 *    coordinates are pixels in that space, y-down. 3D is right-handed, y-up.
 *  - Handles (mesh, texture, sound) are non-negative ints; -1 = none/failure.
 *  - Colors are floats 0..1.
 *  - Lighting is PER-VERTEX (gouraud) — the PICA200 idiom. Transparency in 3D
 *    is alpha-TEST (texel alpha < 0.5 discards), so no depth sorting needed.
 *
 * Minimal C game:
 *    #include <lantern.h>
 *    static void update(double dt) { ... }
 *    static void draw(void)        { lt_clear(0,0,0); ... }
 *    int main(void) { lt_boot("my game", 3); lt_run(update, draw); lt_shutdown(); }
 */
#ifndef LANTERN_H
#define LANTERN_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle ------------------------------------------------------- */
/* 1 ok; 0 failed (everything partially created is torn down again —
 * check this before calling lt_run!).                                     */
int    lt_boot(const char* title, int window_scale);
void   lt_shutdown(void);
/* Request a clean exit: the current/next lt_frame_poll returns 0.         */
void   lt_quit(void);
/* By default the keyboard Escape key quits (dev convenience). Games that
 * handle "escape" themselves (pause menus) call lt_escape_quits(0).       */
void   lt_escape_quits(int enable);
/* Free every game-created mesh, texture, and sound (built-ins survive).
 * Existing handles become invalid (draws with them are safe no-ops).
 * The Lua host calls this on hot-reload so reloads don't leak.            */
void   lt_resources_reset(void);
/* Convenience fixed loop: calls update(dt)/draw() until quit.             */
typedef void (*lt_update_fn)(double dt);
typedef void (*lt_draw_fn)(void);
void   lt_run(lt_update_fn update, lt_draw_fn draw);
/* Or drive the loop yourself:                                             */
int    lt_frame_poll(void);       /* pump events; 0 = quit requested       */
void   lt_frame_begin(void);      /* bind the 400x240 target               */
void   lt_frame_end(void);        /* flush 2D, integer-blit, swap          */
double lt_frame_dt(void);         /* seconds since previous poll (<=0.1)   */

/* Environment contract (read at lt_boot, honored for EVERY host):
 *   LANTERN_SHOT=<prefix>     save <prefix>.bmp of the 400x240 frame at
 *                             frame N, then request quit (CI screenshots)
 *   LANTERN_SHOT_FRAME=N      which frame to capture (default 60)
 *   LANTERN_FIXED_DT=<sec|1>  deterministic mode: lt_frame_dt and lt_time
 *                             advance by a fixed step — REQUIRED for
 *                             cross-machine-reproducible screenshots
 *   LANTERN_NOVSYNC=1         uncap the frame rate (benchmarks)           */

/* ---- screen ----------------------------------------------------------- */
int    lt_screen_w(void);         /* 400 */
int    lt_screen_h(void);         /* 240 */
void   lt_clear(float r, float g, float b);
/* Save the current 400x240 frame (call after lt_frame_end). */
void   lt_screenshot(const char* bmp_path);

/* ---- 3D camera / environment ------------------------------------------ */
void   lt_camera(float ex, float ey, float ez,
                 float tx, float ty, float tz, float fov_deg);
/* One directional light + flat ambient floor.                             */
void   lt_light(float dx, float dy, float dz, float ambient);
/* Up to 4 point lights (PICA200 had 4 hardware lights). radius <= 0 turns
 * slot i off. Attenuation: (1 - d/radius)^2, clamped.                     */
void   lt_point_light(int i, float x, float y, float z, float radius,
                      float r, float g, float b);
/* Linear fog by view depth; disable with end <= start. Applies to meshes
 * AND billboards — it is the 3DS-era depth cue.                           */
void   lt_fog(float start, float end, float r, float g, float b);

/* ---- 3D meshes --------------------------------------------------------- */
/* Vertex format: 12 floats — pos(3) normal(3) uv(2) color(4 RGBA).
 * Vertex color multiplies texture and tint; it is the low-poly gradient
 * tool. GL_TRIANGLES. */
int    lt_mesh_create(const float* verts, int vert_count);
int    lt_mesh_cube(void);        /* unit cube, per-face normals, 0..1 UVs */
int    lt_mesh_plane(int segs);   /* unit XZ plane, y=0, segs x segs quads */
int    lt_mesh_sphere(int seg);   /* unit-diameter UV sphere               */
int    lt_mesh_cylinder(int seg); /* unit diameter/height, capped, y axis  */
int    lt_mesh_cone(int seg);     /* unit base diameter/height, y axis     */
/* Wavefront OBJ (v/vt/vn/f, fan-triangulated). Missing normals get flat
 * face normals. Materials are ignored — texture via lt_draw_mesh.        */
int    lt_mesh_load_obj(const char* path);
/* tex -1 = untextured. Gouraud-lit, tinted by r,g,b.                      */
void   lt_draw_mesh(int mesh, int tex,
                    float x, float y, float z,
                    float rx, float ry, float rz,
                    float sx, float sy, float sz,
                    float r, float g, float b);
/* Keyframe animation (Quake-style tweening): draw with vertices lerped
 * between two meshes of identical vertex count, t = 0..1.                 */
void   lt_draw_mesh_lerp(int mesh_a, int mesh_b, float t, int tex,
                         float x, float y, float z,
                         float rx, float ry, float rz,
                         float sx, float sy, float sz,
                         float r, float g, float b);
/* ALBW-style grounding shadow: a soft dark disc flat on XZ at height y
 * (pass the floor height + a small epsilon). Depth-tested, not written.   */
void   lt_shadow(float x, float y, float z, float radius, float alpha);
/* Camera-facing textured quad in the 3D pass (sprite characters in a
 * diorama). Fullbright (no lighting), fogged, alpha-tested. w,h in world
 * units; u/v select an atlas sub-rect (0,0,1,1 = whole texture).          */
void   lt_billboard(int tex, float x, float y, float z, float w, float h,
                    float u0, float v0, float u1, float v1);

/* ---- 2D (batched, composited over 3D at frame end) -------------------- */
/* BMP; pure magenta (255,0,255) texels become transparent (color key).    */
int    lt_texture_load(const char* bmp_path, int* out_w, int* out_h);
void   lt_rect(float x, float y, float w, float h,
               float r, float g, float b, float a);
void   lt_sprite(int tex, float x, float y, float sx, float sy);
/* Full-control sprite: rotation (radians, around the sprite center),
 * tint+alpha. Negative sx/sy flips.                                       */
void   lt_sprite_ex(int tex, float x, float y, float sx, float sy,
                    float rot, float r, float g, float b, float a);
/* Atlas/tilemap workhorse: draw a sub-rect (UVs 0..1) into x,y,w,h.       */
void   lt_sprite_uv(int tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1);
/* As lt_sprite_uv, but multiplies each texel by (r,g,b) — a per-sprite    */
/* modulate/tint (e.g. season tinting a tile). Texel alpha still keys.     */
void   lt_sprite_uv_tinted(int tex, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1,
                           float r, float g, float b);
/* Built-in 8x8 debug font (uppercase; lowercase is upcased). 8px advance. */
void   lt_print(const char* text, float x, float y,
                float r, float g, float b, float a);

/* ---- audio (48 kHz stereo mixer, 16 channels) -------------------------- */
int    lt_sound_load(const char* wav_path);
/* loop 0/1; returns a channel handle, or -1 if none free.                 */
int    lt_sound_play(int sound, float volume, int loop);
void   lt_sound_stop(int channel);
/* Live per-channel volume (0..1) — music crossfades between looping beds. */
void   lt_channel_volume(int channel, float volume);
void   lt_master_volume(float volume);

/* ---- input ------------------------------------------------------------ */
/* Merged keyboard + first gamepad. Names: left right up down z x c space
 * return escape a s d w.  Pad: dpad/left-stick=dirs, A=z, B=x, Y=c,
 * Start=return.  _down = held now; _pressed = went down this frame.       */
int    lt_input_down(const char* name);
int    lt_input_pressed(const char* name);
int    lt_gamepad_connected(void);
/* Rumble the connected pad (no-op without one). Intensities 0..1.          */
void   lt_rumble(float low, float high, int duration_ms);

/* ---- touch (single point, 3DS-style) ----------------------------------- */
/* One touch point, like the 3DS touchscreen — deliberately not multitouch.
 * Coordinates are 400x240 screen pixels (the letterbox is undone for you),
 * clamped to the screen. While nothing is touching, _x/_y keep the last
 * touched position. On desktop the left mouse button is the finger.        */
int    lt_touch_down(void);       /* held now */
int    lt_touch_pressed(void);    /* went down this frame */
float  lt_touch_x(void);
float  lt_touch_y(void);

/* ---- save data --------------------------------------------------------- */
/* Persistent per-game storage (binary-safe). Files live under the user's
 * application-support dir: <support>/lantern/saves/<name>. Names:
 * [A-Za-z0-9_.-] only (anything else is rejected).                         */
int    lt_save_write(const char* name, const void* data, int len); /* 1 ok  */
/* Returns bytes read (0..buf_len; 0 = an existing empty save), or -1 when
 * the save can't be read: missing, unreadable, or an invalid name.         */
int    lt_save_read(const char* name, void* buf, int buf_len);

/* ---- misc -------------------------------------------------------------- */
double lt_time(void);             /* seconds since boot */

#ifdef __cplusplus
}
#endif
#endif /* LANTERN_H */
