#include <inttypes.h>
#include <stdio.h>

#include "semantic_button.h"

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                                   \
        }                                                                                   \
    } while (0)

static const semantic_button_config_t CONFIG = {.debounce_ms = 10, .long_press_ms = 100};

static bool init(semantic_button_t *button, semantic_button_control_t control)
{
    return semantic_button_init(button, control, CONFIG, false, false, 0);
}

static bool short_press(semantic_button_t *button, uint64_t start, semantic_button_event_t expected)
{
    CHECK(semantic_button_update(button, true, start) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(button, true, start + 10) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(button, false, start + 30) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(button, false, start + 40) == expected);
    CHECK(semantic_button_update(button, false, start + 50) == SEMANTIC_BUTTON_EVENT_NONE);
    return true;
}

static bool test_configuration(void)
{
    semantic_button_t button;
    CHECK(init(&button, SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR));
    CHECK(!semantic_button_init(NULL, SEMANTIC_BUTTON_CONTROL_NEXT, CONFIG, false, false, 0));
    CHECK(!semantic_button_init(&button, (semantic_button_control_t) 99, CONFIG, false, false, 0));
    CHECK(!semantic_button_init(&button, SEMANTIC_BUTTON_CONTROL_NEXT,
                                (semantic_button_config_t) {.debounce_ms = 0, .long_press_ms = 100},
                                false, false, 0));
    CHECK(!semantic_button_init(&button, SEMANTIC_BUTTON_CONTROL_NEXT,
                                (semantic_button_config_t) {.debounce_ms = 10, .long_press_ms = 10},
                                false, false, 0));
    CHECK(!semantic_button_init(&button, SEMANTIC_BUTTON_CONTROL_NEXT,
                                (semantic_button_config_t) {.debounce_ms = 11, .long_press_ms = 10},
                                false, false, 0));
    CHECK(semantic_button_init(&button, SEMANTIC_BUTTON_CONTROL_UNAVAILABLE,
                               (semantic_button_config_t) {0}, true, true, 0));
    return true;
}

static bool test_debounce_boundaries_and_noise(void)
{
    semantic_button_t button;
    CHECK(init(&button, SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR));

    CHECK(semantic_button_update(&button, true, 1) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 9) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 20) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, true, 30) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 39) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 39) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 60) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, true, 70) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 72) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 74) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 77) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 80) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 90) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, false, 100) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 103) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 106) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 109) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 112) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 122) == SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT);
    CHECK(semantic_button_update(&button, false, 123) == SEMANTIC_BUTTON_EVENT_NONE);
    return true;
}

static bool test_refresh_clear(void)
{
    semantic_button_t button;
    CHECK(init(&button, SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR));
    CHECK(short_press(&button, 0, SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT));

    CHECK(semantic_button_update(&button, true, 100) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 110) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 199) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 209) == SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT);
    CHECK(semantic_button_update(&button, false, 210) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, true, 300) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 310) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 409) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 410) == SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY);
    CHECK(semantic_button_update(&button, true, 411) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 1000) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1010) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1020) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, true, 1100) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 1110) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1210) == SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY);
    CHECK(semantic_button_update(&button, false, 1220) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1221) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&button, true, 1300) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1309) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 1320) == SEMANTIC_BUTTON_EVENT_NONE);
    return true;
}

static bool test_previous_and_next(void)
{
    semantic_button_t previous;
    semantic_button_t next;
    CHECK(init(&previous, SEMANTIC_BUTTON_CONTROL_PREVIOUS));
    CHECK(init(&next, SEMANTIC_BUTTON_CONTROL_NEXT));
    CHECK(short_press(&previous, 0, SEMANTIC_BUTTON_EVENT_PREVIOUS_IMAGE));
    CHECK(short_press(&next, 0, SEMANTIC_BUTTON_EVENT_NEXT_IMAGE));

    CHECK(semantic_button_update(&previous, true, 100) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 105) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 120) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_update(&next, true, 100) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, true, 110) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, true, 1000) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 1010) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 1020) == SEMANTIC_BUTTON_EVENT_NEXT_IMAGE);
    CHECK(semantic_button_update(&next, false, 1030) == SEMANTIC_BUTTON_EVENT_NONE);
    return true;
}

static bool test_held_wake(void)
{
    semantic_button_t refresh;
    semantic_button_t previous;
    semantic_button_t next;
    CHECK(semantic_button_init(&refresh, SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR, CONFIG, true, true,
                               0));
    CHECK(semantic_button_update(&refresh, true, 1000) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&refresh, false, 1010) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&refresh, false, 1020) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(short_press(&refresh, 1100, SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT));
    CHECK(semantic_button_update(&refresh, true, 1200) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&refresh, true, 1210) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&refresh, true, 1310) == SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY);
    CHECK(semantic_button_update(&refresh, true, 1400) == SEMANTIC_BUTTON_EVENT_NONE);

    CHECK(semantic_button_init(&previous, SEMANTIC_BUTTON_CONTROL_PREVIOUS, CONFIG, true, true, 0));
    CHECK(semantic_button_update(&previous, true, 1000) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 1010) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 1020) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(short_press(&previous, 1100, SEMANTIC_BUTTON_EVENT_PREVIOUS_IMAGE));

    CHECK(semantic_button_init(&next, SEMANTIC_BUTTON_CONTROL_NEXT, CONFIG, true, true, 0));
    CHECK(semantic_button_update(&next, true, 1000) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 1010) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 1020) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(short_press(&next, 1100, SEMANTIC_BUTTON_EVENT_NEXT_IMAGE));
    return true;
}

static bool test_independence_and_unavailable(void)
{
    semantic_button_t refresh;
    semantic_button_t previous;
    semantic_button_t next;
    semantic_button_t unavailable;
    CHECK(init(&refresh, SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR));
    CHECK(init(&previous, SEMANTIC_BUTTON_CONTROL_PREVIOUS));
    CHECK(init(&next, SEMANTIC_BUTTON_CONTROL_NEXT));
    CHECK(semantic_button_init(&unavailable, SEMANTIC_BUTTON_CONTROL_UNAVAILABLE,
                               (semantic_button_config_t) {0}, true, true, 0));

    CHECK(semantic_button_update(&refresh, true, 0) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, true, 0) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, true, 0) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&refresh, true, 10) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, true, 10) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 5) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, true, 8) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, true, 18) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 30) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&previous, false, 40) == SEMANTIC_BUTTON_EVENT_PREVIOUS_IMAGE);
    CHECK(semantic_button_update(&next, false, 30) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&next, false, 40) == SEMANTIC_BUTTON_EVENT_NEXT_IMAGE);
    CHECK(semantic_button_update(&refresh, true, 110) == SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY);
    CHECK(semantic_button_update(&refresh, true, 120) == SEMANTIC_BUTTON_EVENT_NONE);

    for (uint64_t time = 0; time < 1000; time += 7) {
        CHECK(semantic_button_update(&unavailable, (time & 1U) != 0, time) ==
              SEMANTIC_BUTTON_EVENT_NONE);
    }
    return true;
}

static bool test_timestamp_wrap(void)
{
    semantic_button_t button;
    uint64_t start = UINT64_MAX - 5;
    CHECK(semantic_button_init(&button, SEMANTIC_BUTTON_CONTROL_NEXT, CONFIG, false, false, start));
    CHECK(semantic_button_update(&button, true, start) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, true, 4) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 20) == SEMANTIC_BUTTON_EVENT_NONE);
    CHECK(semantic_button_update(&button, false, 30) == SEMANTIC_BUTTON_EVENT_NEXT_IMAGE);
    CHECK(semantic_button_update(&button, false, UINT64_MAX - 1) == SEMANTIC_BUTTON_EVENT_NONE);
    return true;
}

int main(void)
{
    if (!test_configuration() || !test_debounce_boundaries_and_noise() || !test_refresh_clear() ||
        !test_previous_and_next() || !test_held_wake() || !test_independence_and_unavailable() ||
        !test_timestamp_wrap()) {
        return 1;
    }
    puts("semantic button tests passed");
    return 0;
}
