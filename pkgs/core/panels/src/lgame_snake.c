#include "lgame.h"
#include "keyboard.h"
#include "timer.h"

#define SNAKE_COLS     30
#define SNAKE_ROWS     20
#define SNAKE_TILE     24
#define SNAKE_MAX_SEG  (SNAKE_COLS * SNAKE_ROWS)

typedef struct {
    int x, y;
} seg_t;

static seg_t snake[SNAKE_MAX_SEG];
static int snake_len;
static int dir_x, dir_y;      /* current direction */
static int next_x, next_y;    /* buffered turn */
static int food_x, food_y;
static int score;
static int game_over;
static int paused;
static uint64_t move_accum;
static uint64_t move_interval;
static int board_x, board_y;

static void spawn_food(void) {
    int free_cells = SNAKE_COLS * SNAKE_ROWS - snake_len;
    if (free_cells <= 0) {
        food_x = -1;
        food_y = -1;
        return;
    }
    int pick = lgame_rand(0, free_cells - 1);
    for (int cy = 0; cy < SNAKE_ROWS; cy++) {
        for (int cx = 0; cx < SNAKE_COLS; cx++) {
            int occupied = 0;
            for (int i = 0; i < snake_len; i++) {
                if (snake[i].x == cx && snake[i].y == cy) { occupied = 1; break; }
            }
            if (!occupied) {
                if (pick == 0) { food_x = cx; food_y = cy; return; }
                pick--;
            }
        }
    }
    food_x = -1;
    food_y = -1;
}

static int cell_free(int x, int y) {
    if (x < 0 || y < 0 || x >= SNAKE_COLS || y >= SNAKE_ROWS) return 0;
    for (int i = 1; i < snake_len; i++)
        if (snake[i].x == x && snake[i].y == y) return 0;
    return 1;
}

static void init_snake(void) {
    snake_len = 4;
    for (int i = 0; i < snake_len; i++) {
        snake[i].x = SNAKE_COLS / 2 - 2 + i;
        snake[i].y = SNAKE_ROWS / 2;
    }
    dir_x = 1; dir_y = 0;
    next_x = 1; next_y = 0;
    score = 0;
    game_over = 0;
    paused = 0;
    move_accum = 0;
    move_interval = 150;
    spawn_food();
}

static void step_snake(void) {
    dir_x = next_x;
    dir_y = next_y;
    int hx = snake[0].x + dir_x;
    int hy = snake[0].y + dir_y;
    if (hx < 0 || hy < 0 || hx >= SNAKE_COLS || hy >= SNAKE_ROWS) {
        game_over = 1;
        lgame_audio_error();
        return;
    }
    int eaten = (hx == food_x && hy == food_y);
    if (eaten) {
        if (snake_len + 1 > SNAKE_MAX_SEG) { game_over = 1; return; }
        for (int i = snake_len; i > 0; i--) snake[i].x = snake[i - 1].x, snake[i].y = snake[i - 1].y;
        snake_len++;
        snake[0].x = hx;
        snake[0].y = hy;
        score++;
        lgame_play_tone(660, 40, 60);
        if (move_interval > 70) move_interval -= 4;
        spawn_food();
        return;
    }
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].x == hx && snake[i].y == hy) {
            game_over = 1;
            lgame_audio_error();
            return;
        }
    }
    (void)cell_free;
    for (int i = snake_len - 1; i > 0; i--) snake[i].x = snake[i - 1].x, snake[i].y = snake[i - 1].y;
    snake[0].x = hx;
    snake[0].y = hy;
}

static void update_snake(uint64_t dt_ms) {
    if (lgame_key_pressed(27)) {
        lgame.running = 0;
        return;
    }
    if (lgame_key_pressed(32)) {  /* SPACE: pause / restart */
        if (game_over) init_snake();
        else paused = !paused;
        return;
    }
    if (game_over || paused) return;

    if (lgame_key_pressed(KEY_UP) || lgame_key_pressed('w') || lgame_key_pressed('W')) {
        if (dir_y != 1) { next_x = 0; next_y = -1; }
    } else if (lgame_key_pressed(KEY_DOWN) || lgame_key_pressed('s') || lgame_key_pressed('S')) {
        if (dir_y != -1) { next_x = 0; next_y = 1; }
    } else if (lgame_key_pressed(KEY_LEFT) || lgame_key_pressed('a') || lgame_key_pressed('A')) {
        if (dir_x != 1) { next_x = -1; next_y = 0; }
    } else if (lgame_key_pressed(KEY_RIGHT) || lgame_key_pressed('d') || lgame_key_pressed('D')) {
        if (dir_x != -1) { next_x = 1; next_y = 0; }
    }

    move_accum += dt_ms;
    if (move_accum >= move_interval) {
        move_accum = 0;
        step_snake();
    }
}

static void draw_snake(void) {
    lgame_clear(lgame_color_hex(0x0A1410));
    lgame_title(lgame.title);

    lgame_draw_rect(board_x - 2, board_y - 2, SNAKE_COLS * SNAKE_TILE + 4,
                    SNAKE_ROWS * SNAKE_TILE + 4, lgame_color_hex(0x334433));

    for (int i = 0; i < snake_len; i++) {
        int px = board_x + snake[i].x * SNAKE_TILE;
        int py = board_y + snake[i].y * SNAKE_TILE;
        lgame_color_t c = (i == 0) ? lgame_color_hex(0x44FF44) : lgame_color_hex(0x228822);
        lgame_fill_rect(px + 1, py + 1, SNAKE_TILE - 2, SNAKE_TILE - 2, c);
        if (i == 0) {
            int ex = px + SNAKE_TILE / 2 + dir_x * 5;
            int ey = py + SNAKE_TILE / 2 + dir_y * 5;
            lgame_fill_rect(ex - 2, ey - 2, 4, 4, lgame_color_hex(0x000000));
        }
    }

    if (food_x >= 0) {
        int px = board_x + food_x * SNAKE_TILE;
        int py = board_y + food_y * SNAKE_TILE;
        lgame_fill_rect(px + 3, py + 3, SNAKE_TILE - 6, SNAKE_TILE - 6, lgame_color_hex(0xFF5533));
    }

    char sc[32];
    __builtin_sprintf(sc, "SCORE  %d", score);
    lgame_draw_text_scaled(board_x, board_y + SNAKE_ROWS * SNAKE_TILE + 8, sc,
                           lgame_color_hex(0xFFFFFF), 2);

    if (paused && !game_over)
        lgame_draw_text_scaled(lgame.width / 2 - 60, lgame.height / 2 - 12, "PAUSED",
                               lgame_color_hex(0xCCCCCC), 2);
    if (game_over) {
        lgame_draw_text_scaled(lgame.width / 2 - 90, lgame.height / 2 - 24, "GAME OVER",
                               lgame_color_hex(0xFF4444), 2);
        lgame_draw_text(lgame.width / 2 - 70, lgame.height / 2 + 8,
                        "Press SPACE to restart, ESC to quit", lgame_color_hex(0xCCCCCC));
    }
}

int lgame_snake_run(void) {
    if (lgame_init(800, 600, "LGame Snake") != LGAME_OK) {
        return -1;
    }
    board_x = (lgame.width - SNAKE_COLS * SNAKE_TILE) / 2;
    board_y = 70;
    init_snake();
    lgame_srand(lgame_time_ms());

    while (lgame_running()) {
        lgame_frame_begin();
        lgame_input_poll();
        update_snake(lgame.dt_ms);
        if (!lgame.running) break;
        draw_snake();
        lgame_present();
    }

    lgame_quit();
    return 0;
}