#include "aitvbox/app.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOARD_SIDE 4
#define CELL_COUNT (BOARD_SIDE * BOARD_SIDE)
#define STORAGE_KEY "game-state"

typedef struct {
    unsigned cell[CELL_COUNT];
    unsigned score;
    unsigned random_state;
} game_state_t;

static unsigned next_random(game_state_t *game)
{
    game->random_state =
        game->random_state * 1664525u + 1013904223u;
    return game->random_state;
}

static bool add_tile(game_state_t *game)
{
    unsigned empty[CELL_COUNT];
    unsigned count = 0;
    for (unsigned i = 0; i < CELL_COUNT; i++) {
        if (!game->cell[i])
            empty[count++] = i;
    }
    if (!count)
        return false;
    unsigned index = empty[next_random(game) % count];
    game->cell[index] = next_random(game) % 10 == 0 ? 4 : 2;
    return true;
}

static void new_game(game_state_t *game)
{
    memset(game, 0, sizeof(*game));
    game->random_state = 0x20480000u ^ (unsigned)getpid();
    add_tile(game);
    add_tile(game);
}

static bool decode_state(const char *text, game_state_t *game)
{
    if (!text || !game)
        return false;
    unsigned values[CELL_COUNT + 2];
    const char *cursor = text;
    char *end = NULL;
    for (unsigned i = 0; i < CELL_COUNT + 2; i++) {
        unsigned long value = strtoul(cursor, &end, 10);
        if (end == cursor || value > 0xfffffffful)
            return false;
        values[i] = (unsigned)value;
        if (i + 1 < CELL_COUNT + 2) {
            if (*end != ',')
                return false;
            cursor = end + 1;
        } else if (*end != '\0') {
            return false;
        }
    }
    for (unsigned i = 0; i < CELL_COUNT; i++) {
        unsigned value = values[i];
        if (value && (value & (value - 1)))
            return false;
        game->cell[i] = value;
    }
    game->score = values[CELL_COUNT];
    game->random_state = values[CELL_COUNT + 1] ?
        values[CELL_COUNT + 1] : 0x20480001u;
    return true;
}

static int save_state(const game_state_t *game)
{
    char text[384];
    size_t used = 0;
    for (unsigned i = 0; i < CELL_COUNT; i++) {
        int count = snprintf(text + used, sizeof(text) - used, "%u,",
                             game->cell[i]);
        if (count < 0 || (size_t)count >= sizeof(text) - used)
            return -1;
        used += (size_t)count;
    }
    int count = snprintf(text + used, sizeof(text) - used, "%u,%u",
                         game->score, game->random_state);
    if (count < 0 || (size_t)count >= sizeof(text) - used)
        return -1;
    return aitvbox_storage_write(STORAGE_KEY, text);
}

static void load_state(game_state_t *game)
{
    char text[384];
    bool found = false;
    if (aitvbox_storage_read(
            STORAGE_KEY, text, sizeof(text), &found) ||
        !found || !decode_state(text, game))
        new_game(game);
}

enum direction {
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
};

static unsigned cell_index(enum direction direction,
                           unsigned line, unsigned position)
{
    switch (direction) {
    case MOVE_LEFT:
        return line * BOARD_SIDE + position;
    case MOVE_RIGHT:
        return line * BOARD_SIDE + (BOARD_SIDE - 1 - position);
    case MOVE_UP:
        return position * BOARD_SIDE + line;
    case MOVE_DOWN:
        return (BOARD_SIDE - 1 - position) * BOARD_SIDE + line;
    }
    return 0;
}

static bool move_board(game_state_t *game, enum direction direction)
{
    bool changed = false;
    for (unsigned line = 0; line < BOARD_SIDE; line++) {
        unsigned original[BOARD_SIDE];
        unsigned compact[BOARD_SIDE] = {0};
        unsigned compact_count = 0;
        for (unsigned position = 0; position < BOARD_SIDE; position++) {
            original[position] =
                game->cell[cell_index(direction, line, position)];
            if (original[position])
                compact[compact_count++] = original[position];
        }

        unsigned merged[BOARD_SIDE] = {0};
        unsigned output = 0;
        for (unsigned input = 0; input < compact_count; input++) {
            if (input + 1 < compact_count &&
                compact[input] == compact[input + 1]) {
                merged[output] = compact[input] * 2;
                game->score += merged[output];
                input++;
            } else {
                merged[output] = compact[input];
            }
            output++;
        }
        for (unsigned position = 0; position < BOARD_SIDE; position++) {
            if (original[position] != merged[position])
                changed = true;
            game->cell[cell_index(direction, line, position)] =
                merged[position];
        }
    }
    if (changed)
        add_tile(game);
    return changed;
}

static bool moves_available(const game_state_t *game)
{
    for (unsigned row = 0; row < BOARD_SIDE; row++) {
        for (unsigned column = 0; column < BOARD_SIDE; column++) {
            unsigned current = game->cell[row * BOARD_SIDE + column];
            if (!current)
                return true;
            if (column + 1 < BOARD_SIDE &&
                current == game->cell[row * BOARD_SIDE + column + 1])
                return true;
            if (row + 1 < BOARD_SIDE &&
                current == game->cell[(row + 1) * BOARD_SIDE + column])
                return true;
        }
    }
    return false;
}

static int show_board(const game_state_t *game, bool moved)
{
    char message[512];
    size_t used = (size_t)snprintf(
        message, sizeof(message), "分数: %u%s\n",
        game->score, moved ? "" : "  (此方向无法移动)");
    for (unsigned row = 0; row < BOARD_SIDE && used < sizeof(message); row++) {
        for (unsigned column = 0;
             column < BOARD_SIDE && used < sizeof(message); column++) {
            unsigned value = game->cell[row * BOARD_SIDE + column];
            int count = value ?
                snprintf(message + used, sizeof(message) - used, "%5u", value) :
                snprintf(message + used, sizeof(message) - used, "%5s", ".");
            if (count < 0 || (size_t)count >= sizeof(message) - used)
                return aitvbox_app_reply(false, "棋盘显示失败");
            used += (size_t)count;
        }
        if (used + 1 >= sizeof(message))
            return aitvbox_app_reply(false, "棋盘显示失败");
        message[used++] = '\n';
        message[used] = '\0';
    }
    if (!moves_available(game))
        snprintf(message + used, sizeof(message) - used,
                 "没有可用移动，点击“新游戏”重新开始。");
    return aitvbox_app_reply(true, message);
}

int main(int argc, char **argv)
{
    const char *action = aitvbox_app_action(argc, argv);
    if (!action)
        return 2;

    game_state_t game;
    load_state(&game);
    bool moved = true;
    if (!strcmp(action, "new")) {
        new_game(&game);
    } else if (!strcmp(action, "left")) {
        moved = move_board(&game, MOVE_LEFT);
    } else if (!strcmp(action, "right")) {
        moved = move_board(&game, MOVE_RIGHT);
    } else if (!strcmp(action, "up")) {
        moved = move_board(&game, MOVE_UP);
    } else if (!strcmp(action, "down")) {
        moved = move_board(&game, MOVE_DOWN);
    } else {
        return aitvbox_app_reply(false, "不支持的游戏操作");
    }
    if (save_state(&game))
        return aitvbox_app_reply(false, "无法保存游戏状态");
    return show_board(&game, moved);
}
