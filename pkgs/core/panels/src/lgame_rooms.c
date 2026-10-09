/* ── rooms ──
 *
 * A first-person walk around a boxy room. This is the first thing here that
 * uses the 3D path as a *game* rather than as a probe: the selftest draws one
 * fixed frame that never changes, so nothing has yet had to deal with a camera
 * that moves, geometry resubmitted every frame, or a player who can walk into
 * a wall.
 *
 * Everything is axis-aligned and made of flat quads, so there are no textures
 * and no perspective-correct UVs, and every frame is a pure function of the
 * player's position. That is a deliberate constraint rather than a stylistic
 * one: scripts/lgame_check.py asserts exact pixel values, so the frame has to
 * be reproducible from the state the kernel prints.
 */

#include "lgame.h"
#include "keyboard.h"
#include "timer.h"
#include "kprintf.h"

#define VIEW_W 320
#define VIEW_H 240

#define ROOM_HALF  6                     /* room spans -6..6 on x and z */
#define ROOM_CEIL  5
#define EYE_HEIGHT (3 * LGAME_FP_ONE / 2)

#define PILLAR_HALF (LGAME_FP_ONE / 2)
#define PILLAR_TOP  4
/* Off the centre line on purpose: a room symmetric about the view axis makes an
 * inverted turn control very hard to spot from a single frame. */
#define PILLAR_A_X (-3 * LGAME_FP_ONE / 2)
#define PILLAR_B_X ( 3 * LGAME_FP_ONE / 2)

/* Movement is a discrete step on *press*, plus a slow rate while a key is held.
 *
 * The step is not a nicety, it is the only thing that makes this game playable
 * on the boot entry that can reach it. `no-desktop` (limine entry 5) is the
 * only path that drops to a shell with the framebuffer up, and its only input
 * device is the serial console, where a byte is latched for exactly one frame
 * and never gets a release event. A rate-based control therefore moves the
 * player 1/60th of a step per keypress and turning 90 degrees takes a hundred
 * presses. A held-key rate is still layered underneath, because on a real PS/2
 * keyboard holding a key keeps lgame_key_down() true and drifting is the
 * behaviour a player expects. */
#define TURN_STEP (LGAME_FP_PI / 12)     /* 15 degrees per press */
#define WALK_STEP LGAME_FP_1             /* 1 world unit per press */
#define TURN_RATE (LGAME_FP_ONE / 4)     /* rad/s while held */
#define WALK_RATE (LGAME_FP_ONE / 2)     /* units/s while held */

#define SKY_COLOUR  0xFF101014u
#define BORDER_COLOUR 0xFF05050Au      /* outside the 3D viewport */
#define WALL_COLOUR 0xFF6E7076u
#define FLOOR_COLOUR 0xFF3A4048u
#define CEIL_COLOUR  0xFF22262Cu
#define STONE_COLOUR 0xFF9AA0Au
#define MARK_COLOUR  0xFFE0B040u

#define wrap_pi(a) lgame_fp_wrap_pi(a)

/* Keystrokes arrive as ASCII over serial as well as scancodes from the PS/2
 * port, so movement accepts both cases rather than assuming a shift state. */
static int pressed(int lower, int upper) {
    return lgame_key_pressed(lower) || lgame_key_pressed(upper);
}

/* A horizontal quad at height `y` spanning [x0,x1] x [z0,z1], facing up when
 * `up` is set. Floor and ceiling. */
static void slab(lgame_fp_t y, lgame_fp_t x0, lgame_fp_t x1,
                 lgame_fp_t z0, lgame_fp_t z1, lgame_color_t col, int up) {
    lgame_vec3_t n = up ? lgame_vec3(LGAME_FP_0, LGAME_FP_1, LGAME_FP_0)
                        : lgame_vec3(LGAME_FP_0, -LGAME_FP_1, LGAME_FP_0);
    lgame_3d_tri_shaded(lgame_vec3(x0, y, z1), lgame_vec3(x1, y, z1),
                        lgame_vec3(x0, y, z0), n, col);
    lgame_3d_tri_shaded(lgame_vec3(x1, y, z1), lgame_vec3(x1, y, z0),
                        lgame_vec3(x0, y, z0), n, col);
}

/* A vertical quad in the plane x = `x`, facing +X when `plus` is set. */
static void wall_x(lgame_fp_t x, lgame_fp_t y0, lgame_fp_t y1,
                   lgame_fp_t z0, lgame_fp_t z1, lgame_color_t col, int plus) {
    lgame_vec3_t n = plus ? lgame_vec3(LGAME_FP_1, LGAME_FP_0, LGAME_FP_0)
                          : lgame_vec3(-LGAME_FP_1, LGAME_FP_0, LGAME_FP_0);
    lgame_3d_tri_shaded(lgame_vec3(x, y0, z1), lgame_vec3(x, y0, z0),
                        lgame_vec3(x, y1, z1), n, col);
    lgame_3d_tri_shaded(lgame_vec3(x, y0, z0), lgame_vec3(x, y1, z0),
                        lgame_vec3(x, y1, z1), n, col);
}

/* A vertical quad in the plane z = `z`, facing +Z when `plus` is set. */
static void wall_z(lgame_fp_t z, lgame_fp_t y0, lgame_fp_t y1,
                   lgame_fp_t x0, lgame_fp_t x1, lgame_color_t col, int plus) {
    lgame_vec3_t n = plus ? lgame_vec3(LGAME_FP_0, LGAME_FP_0, LGAME_FP_1)
                          : lgame_vec3(LGAME_FP_0, LGAME_FP_0, -LGAME_FP_1);
    lgame_3d_tri_shaded(lgame_vec3(x0, y0, z), lgame_vec3(x1, y0, z),
                        lgame_vec3(x0, y1, z), n, col);
    lgame_3d_tri_shaded(lgame_vec3(x1, y0, z), lgame_vec3(x1, y1, z),
                        lgame_vec3(x0, y1, z), n, col);
}

/* A pillar: four sides and a cap, no bottom. */
static void pillar(lgame_fp_t cx, lgame_fp_t cz, lgame_color_t col) {
    lgame_fp_t x0 = cx - PILLAR_HALF, x1 = cx + PILLAR_HALF;
    lgame_fp_t z0 = cz - PILLAR_HALF, z1 = cz + PILLAR_HALF;
    lgame_fp_t top = PILLAR_TOP * LGAME_FP_ONE;
    wall_x(x0, LGAME_FP_0, top, z0, z1, col, 0);
    wall_x(x1, LGAME_FP_0, top, z0, z1, col, 1);
    wall_z(z0, LGAME_FP_0, top, x0, x1, col, 0);
    wall_z(z1, LGAME_FP_0, top, x0, x1, col, 1);
    slab(top, x0, x1, z0, z1, col, 0);
}

/* Player state. `yaw` is measured from -Z toward +X, so yaw 0 looks down -Z
 * and yaw = PI/2 looks down +X; the forward vector is therefore (sin, -cos). */
static lgame_fp_t px, pz, yaw;

/* Print the player state in a form the check can parse, so the check judges the
 * frame against what the game says it was rather than against a second copy of
 * these numbers.
 *
 * Only printed when the state actually changed. Emitting it every frame would
 * put ~60 short lines a second down a 115200-baud line -- a few milliseconds
 * each, which is a real slice of the frame budget, and the check's scan over
 * the log would grow without bound. Printing on change means an idle game says
 * nothing after the first frame, which is exactly when the check captures. */
static lgame_fp_t last_px, last_pz, last_yaw;
static int have_printed;

static void report(void) {
    if (have_printed && px == last_px && pz == last_pz && yaw == last_yaw)
        return;
    last_px = px; last_pz = pz; last_yaw = yaw;
    have_printed = 1;
    /* Raw 16.16 counts, not scaled: the check needs the exact number, and
     * printing a rounded version would make it re-derive the rounding.
     *
     * The counters are here because an empty room and a room that is entirely
     * behind the player look identical on screen -- both are all sky. What
     * separates them is `submitted` being large and `drawn` being zero, which
     * is the same reason lgame_3d counts at all. */
    kprintf("lgame: rooms at %d,%d yaw=%d 3dc %d/%d/%d/%d\n", px, pz, yaw,
            lgame_3d_submitted(), lgame_3d_drawn(),
            lgame_3d_clipped(), lgame_3d_culled());
}

static void draw_world(void) {
    lgame_fp_t h = ROOM_HALF * LGAME_FP_ONE;
    lgame_fp_t c = ROOM_CEIL * LGAME_FP_ONE;

    lgame_3d_clear(lgame_color_hex(SKY_COLOUR));

    slab(LGAME_FP_0, -h, h, -h, h, lgame_color_hex(FLOOR_COLOUR), 1);
    slab(c, -h, h, -h, h, lgame_color_hex(CEIL_COLOUR), 0);
    wall_x(-h, LGAME_FP_0, c, -h, h, lgame_color_hex(WALL_COLOUR), 1);
    wall_x(h, LGAME_FP_0, c, -h, h, lgame_color_hex(WALL_COLOUR), 0);
    wall_z(-h, LGAME_FP_0, c, -h, h, lgame_color_hex(WALL_COLOUR), 1);
    wall_z(h, LGAME_FP_0, c, -h, h, lgame_color_hex(WALL_COLOUR), 0);

    pillar(PILLAR_A_X, LGAME_FP_0, lgame_color_hex(STONE_COLOUR));
    pillar(PILLAR_B_X, LGAME_FP_0, lgame_color_hex(STONE_COLOUR));

    /* A bright band across the far (-Z) wall, the one the player starts out facing.
     * It is the only feature whose screen position is a direct function of yaw
     * and nothing else, which is what makes it what the check watches when it
     * turns the player. It is a vertical quad, not a slab: a slab at z = z0 =
     * -h would be zero-area and simply never draw.
     *
     * The check needs to tell "the player turned" from "the player walked into
     * the wall and the wall got closer", and only a feature fixed to the room
     * does that: movement slides along x and changes the band's width
     * asymmetrically, while a turn moves it across the screen monotonically. */
    wall_z(-h, LGAME_FP_ONE, LGAME_FP_2, -h / 4, h / 4,
           lgame_color_hex(MARK_COLOUR), 1);
}

int lgame_rooms_run(void) {
    /* 0,0 means "use the whole surface", so the 320x240 viewport can be centred
     * inside it. Passing VIEW_W/VIEW_H here would set the canvas size instead,
     * and the canvas origin is the framebuffer origin, not the centre. */
    if (lgame_init(0, 0, "LGame Rooms") != LGAME_OK)
        return -1;

    px = 0;
    pz = 4 * LGAME_FP_ONE;
    yaw = 0;

    if (lgame_3d_begin(VIEW_W, VIEW_H, (lgame.width - VIEW_W) / 2,
                       (lgame.height - VIEW_H) / 2) != LGAME_OK) {
        kprintf("lgame: rooms could not open a 3D viewport\n");
        lgame_quit();
        return -2;
    }
    /* Light from above and behind the start position, so the floor is brighter
     * than the ceiling and the two walls either side differ. */
    lgame_3d_light(lgame_vec3(LGAME_FP_0, -LGAME_FP_1, -LGAME_FP_1),
                   LGAME_FP_ONE / 5);

    while (lgame_running()) {
        lgame_frame_begin();
        lgame_input_poll();

        if (lgame_key_pressed(LGAME_KEY_ESC)) break;

        /* Two terms per axis: a fixed step on the frame the key went down, and
         * a rate on every frame the key is down. On serial the two collapse
         * into one frame, which is the point; on PS/2 holding accumulates. */
        int turn = 0, strafe = 0, fwd = 0;

        if (lgame_key_pressed(KEY_LEFT)  || pressed('a', 'A'))
            turn += TURN_STEP;
        if (lgame_key_pressed(KEY_RIGHT) || pressed('d', 'D'))
            turn -= TURN_STEP;
        if (lgame_key_pressed(KEY_UP)    || pressed('w', 'W'))
            fwd  += WALK_STEP;
        if (lgame_key_pressed(KEY_DOWN)  || pressed('s', 'S'))
            fwd  -= WALK_STEP;
        if (pressed('q', 'Q')) strafe -= WALK_STEP;
        if (pressed('e', 'E')) strafe += WALK_STEP;

        int held = 0;
        if (lgame_key_down(KEY_LEFT)  || lgame_key_down('a') || lgame_key_down('A'))
            held += 1;
        if (lgame_key_down(KEY_RIGHT) || lgame_key_down('d') || lgame_key_down('D'))
            held -= 1;
        int fwd_held = 0;
        if (lgame_key_down(KEY_UP)    || lgame_key_down('w') || lgame_key_down('W'))
            fwd_held += 1;
        if (lgame_key_down(KEY_DOWN)  || lgame_key_down('s') || lgame_key_down('S'))
            fwd_held -= 1;

        /* dt_ms is milliseconds as a plain integer; fp_div turns it into
         * seconds in 16.16. lgame caps dt_ms (see LGAME_MAX_FRAME_MS) so a
         * long stall cannot teleport the player through a wall. */
        lgame_fp_t dt_s = lgame_fp_div((lgame_fp_t)lgame.dt_ms, 1000);

        yaw = wrap_pi(yaw + turn + lgame_fp_mul((lgame_fp_t)held,
                                               lgame_fp_mul(TURN_RATE, dt_s)));

        /* Forward is (sin yaw, -cos yaw): at yaw 0 that is (0, -1), which is
         * down -Z, matching "yaw 0 looks at the far wall". */
        lgame_fp_t sy = lgame_fp_sin(yaw), cy = lgame_fp_cos(yaw);
        lgame_fp_t rate = lgame_fp_mul((lgame_fp_t)fwd_held,
                                       lgame_fp_mul(WALK_RATE, dt_s));
        /* `step` is a distance in world units (16.16); multiplying it by a unit
         * direction is one fp_mul, and the sum of the two axes is another. */
        lgame_fp_t step = fwd + rate;
        px += lgame_fp_add(lgame_fp_mul(sy, step),
                           lgame_fp_mul(cy, (lgame_fp_t)strafe));
        pz += lgame_fp_add(lgame_fp_mul(-cy, step),
                           lgame_fp_mul(sy, (lgame_fp_t)strafe));

        /* Walls are solid. The limit is 2 units in, not a hair: the near plane
         * sits at 1.0 world unit, so a player allowed right up to the face
         * would clip the wall in front of them and see straight through the
         * room. */
        lgame_fp_t lim = (ROOM_HALF - 2) * LGAME_FP_ONE;
        if (px < -lim) px = -lim;
        if (px >  lim) px =  lim;
        if (pz < -lim) pz = -lim;
        if (pz >  lim) pz =  lim;

        /* Aim a unit along the facing direction, so the camera basis is well
         * conditioned: eye == target is a degenerate case the camera refuses. */
        lgame_3d_camera(lgame_vec3(px, EYE_HEIGHT, pz),
                        lgame_vec3(px + sy, EYE_HEIGHT, pz - cy), 240);
        lgame_clear(lgame_color_hex(BORDER_COLOUR));
        draw_world();
        report();
        lgame_present();
    }

    lgame_3d_end();
    lgame_quit();
    return 0;
}