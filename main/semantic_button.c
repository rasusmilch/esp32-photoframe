#include "semantic_button.h"

#include <string.h>

static bool control_is_valid(semantic_button_control_t control)
{
    return control >= SEMANTIC_BUTTON_CONTROL_UNAVAILABLE &&
           control <= SEMANTIC_BUTTON_CONTROL_NEXT;
}

static semantic_button_event_t short_event(semantic_button_control_t control)
{
    switch (control) {
    case SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR:
        return SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT;
    case SEMANTIC_BUTTON_CONTROL_PREVIOUS:
        return SEMANTIC_BUTTON_EVENT_PREVIOUS_IMAGE;
    case SEMANTIC_BUTTON_CONTROL_NEXT:
        return SEMANTIC_BUTTON_EVENT_NEXT_IMAGE;
    case SEMANTIC_BUTTON_CONTROL_UNAVAILABLE:
        break;
    }
    return SEMANTIC_BUTTON_EVENT_NONE;
}

bool semantic_button_init(semantic_button_t *button, semantic_button_control_t control,
                          semantic_button_config_t config, bool initially_asserted,
                          bool suppress_if_asserted, uint64_t now_ms)
{
    if (button == NULL || !control_is_valid(control)) {
        return false;
    }

    memset(button, 0, sizeof(*button));
    button->control = control;
    button->config = config;

    if (control == SEMANTIC_BUTTON_CONTROL_UNAVAILABLE) {
        return true;
    }
    if (config.debounce_ms == 0 ||
        (control == SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR && config.long_press_ms == 0)) {
        return false;
    }

    button->available = true;
    button->raw_asserted = initially_asserted;
    button->raw_changed_at_ms = now_ms;
    if (initially_asserted && suppress_if_asserted) {
        button->stable_asserted = true;
        button->suppressed_until_release = true;
    }
    return true;
}

semantic_button_event_t semantic_button_update(semantic_button_t *button, bool asserted,
                                               uint64_t now_ms)
{
    if (button == NULL || !button->available) {
        return SEMANTIC_BUTTON_EVENT_NONE;
    }

    if (asserted != button->raw_asserted) {
        button->raw_asserted = asserted;
        button->raw_changed_at_ms = now_ms;
    }

    semantic_button_event_t event = SEMANTIC_BUTTON_EVENT_NONE;
    if (button->raw_asserted != button->stable_asserted &&
        now_ms - button->raw_changed_at_ms >= button->config.debounce_ms) {
        button->stable_asserted = button->raw_asserted;
        uint64_t accepted_at = button->raw_changed_at_ms + button->config.debounce_ms;

        if (button->stable_asserted) {
            button->pressed = true;
            button->long_emitted = false;
            button->pressed_at_ms = accepted_at;
        } else if (button->suppressed_until_release) {
            button->suppressed_until_release = false;
            button->pressed = false;
            button->long_emitted = false;
        } else if (button->pressed) {
            uint64_t duration = accepted_at - button->pressed_at_ms;
            if (button->control == SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR && !button->long_emitted &&
                duration >= button->config.long_press_ms) {
                event = SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY;
                button->long_emitted = true;
            } else if (!button->long_emitted) {
                event = short_event(button->control);
            }
            button->pressed = false;
        }
    }

    if (event == SEMANTIC_BUTTON_EVENT_NONE && button->stable_asserted && button->pressed &&
        !button->suppressed_until_release && !button->long_emitted &&
        button->control == SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR &&
        now_ms - button->pressed_at_ms >= button->config.long_press_ms) {
        button->long_emitted = true;
        event = SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY;
    }

    return event;
}
