#include "lgame.h"
#include "keyboard.h"

#define PADDLE_W 12
#define PADDLE_H 80
#define BALL_SIZE 12
#define PADDLE_SPEED 400.0f

typedef struct {
    float x, y;
    float vx, vy;
} ball_t;

static ball_t ball;
static float paddle1_y, paddle2_y;
static int score1 = 0, score2 = 0;
static int game_over = 0;

#define DT_SEC(dt_ms)  ((float)(dt_ms) / 1000.0f)

void reset_ball(void) {
    ball.x = lgame.width / 2.0f;
    ball.y = lgame.height / 2.0f;
    ball.vx = (lgame_rand(0, 1) ? 300.0f : -300.0f);
    ball.vy = (float)lgame_rand(-200, 200);
}

void init_game(void) {
    paddle1_y = lgame.height / 2.0f - PADDLE_H / 2.0f;
    paddle2_y = lgame.height / 2.0f - PADDLE_H / 2.0f;
    score1 = score2 = 0;
    game_over = 0;
    reset_ball();
}

void update_paddles(uint64_t dt_ms) {
    float dt = DT_SEC(dt_ms);
    if (lgame_key_down(KEY_UP))
        paddle2_y -= PADDLE_SPEED * dt;
    if (lgame_key_down(KEY_DOWN))
        paddle2_y += PADDLE_SPEED * dt;

    if (lgame_key_down('w') || lgame_key_down('W'))
        paddle1_y -= PADDLE_SPEED * dt;
    if (lgame_key_down('s') || lgame_key_down('S'))
        paddle1_y += PADDLE_SPEED * dt;

    if (paddle1_y < 0) paddle1_y = 0;
    if (paddle1_y > lgame.height - PADDLE_H) paddle1_y = lgame.height - PADDLE_H;
    if (paddle2_y < 0) paddle2_y = 0;
    if (paddle2_y > lgame.height - PADDLE_H) paddle2_y = lgame.height - PADDLE_H;
}

void update_ball(uint64_t dt_ms) {
    float dt = DT_SEC(dt_ms);
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;

    if (ball.y <= 0 || ball.y >= lgame.height - BALL_SIZE) {
        ball.vy = -ball.vy;
        if (ball.y <= 0) ball.y = 0;
        if (ball.y >= lgame.height - BALL_SIZE) ball.y = lgame.height - BALL_SIZE;
    }

    if (ball.x <= PADDLE_W &&
        ball.y + BALL_SIZE >= paddle1_y &&
        ball.y <= paddle1_y + PADDLE_H) {
        ball.vx = -ball.vx;
        ball.x = PADDLE_W;
        float hit_pos = (ball.y + BALL_SIZE/2) - (paddle1_y + PADDLE_H/2);
        ball.vy = hit_pos * 5.0f;
        lgame_play_beep(440, 50);
    }

    if (ball.x + BALL_SIZE >= lgame.width - PADDLE_W &&
        ball.y + BALL_SIZE >= paddle2_y &&
        ball.y <= paddle2_y + PADDLE_H) {
        ball.vx = -ball.vx;
        ball.x = lgame.width - PADDLE_W - BALL_SIZE;
        float hit_pos = (ball.y + BALL_SIZE/2) - (paddle2_y + PADDLE_H/2);
        ball.vy = hit_pos * 5.0f;
        lgame_play_beep(440, 50);
    }

    if (ball.x < 0) {
        score2++;
        lgame_play_beep(220, 200);
        if (score2 >= 7) game_over = 1;
        else reset_ball();
    }
    if (ball.x > lgame.width) {
        score1++;
        lgame_play_beep(220, 200);
        if (score1 >= 7) game_over = 1;
        else reset_ball();
    }
}

void draw_game(void) {
    lgame_clear(lgame_color_hex(0x001122FF));

    lgame_fill_rect(PADDLE_W / 2, (int)paddle1_y, PADDLE_W, PADDLE_H, lgame_color_hex(0x00FFFF));
    lgame_fill_rect(lgame.width - PADDLE_W * 3 / 2, (int)paddle2_y, PADDLE_W, PADDLE_H, lgame_color_hex(0xFF00FF));

    lgame_fill_rect((int)ball.x, (int)ball.y, BALL_SIZE, BALL_SIZE, lgame_color_hex(0xFFFFFF));

    for (int i = 0; i < lgame.height; i += 20) {
        lgame_fill_rect(lgame.width / 2 - 2, i, 4, 10, lgame_color_hex(0x444444));
    }

    char score_str[32];
    __builtin_sprintf(score_str, "%d  -  %d", score1, score2);
    lgame_draw_text_scaled(lgame.width / 2 - 60, 20, score_str, lgame_color_hex(0xFFFFFF), 2);

    if (game_over) {
        const char *winner = score1 > score2 ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!";
        lgame_draw_text_scaled(lgame.width / 2 - 120, lgame.height / 2 - 20, winner, lgame_color_hex(0xFFFF00), 2);
        lgame_draw_text(lgame.width / 2 - 80, lgame.height / 2 + 20, "Press SPACE to restart", lgame_color_hex(0x888888));
    }
}

int lgame_pong_run(void) {
    if (lgame_init(800, 600, "LGame Pong") != LGAME_OK) {
        return -1;
    }

    init_game();
    lgame_srand(lgame_time_ms());

    while (lgame_running()) {
        lgame_frame_begin();
        lgame_input_poll();

        if (lgame_key_pressed(27)) break;  // ESC

        if (game_over) {
            if (lgame_key_pressed(32)) {  // SPACE
                init_game();
            }
        } else {
            update_paddles(lgame.dt_ms);
            update_ball(lgame.dt_ms);
        }

        draw_game();
        lgame_present();
    }

    lgame_quit();
    return 0;
}