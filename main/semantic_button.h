#ifndef SEMANTIC_BUTTON_H
#define SEMANTIC_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SEMANTIC_BUTTON_EVENT_NONE = 0,
    SEMANTIC_BUTTON_EVENT_REFRESH_CURRENT,
    SEMANTIC_BUTTON_EVENT_CLEAR_DISPLAY,
    SEMANTIC_BUTTON_EVENT_PREVIOUS_IMAGE,
    SEMANTIC_BUTTON_EVENT_NEXT_IMAGE,
} semantic_button_event_t;

typedef enum {
    SEMANTIC_BUTTON_CONTROL_UNAVAILABLE = 0,
    SEMANTIC_BUTTON_CONTROL_REFRESH_CLEAR,
    SEMANTIC_BUTTON_CONTROL_PREVIOUS,
    SEMANTIC_BUTTON_CONTROL_NEXT,
} semantic_button_control_t;

typedef struct {
    uint64_t debounce_ms;
    uint64_t long_press_ms;
} semantic_button_config_t;

/**
 * Independent state for one physical control. Callers should treat these fields as opaque.
 * Time is monotonic modulo 2^64; elapsed time uses defined uint64_t unsigned subtraction.
 */
typedef struct {
    semantic_button_control_t control;
    semantic_button_config_t config;
    uint64_t raw_changed_at_ms;
    uint64_t pressed_at_ms;
    bool available;
    bool raw_asserted;
    bool stable_asserted;
    bool pressed;
    bool long_emitted;
    bool suppressed_until_release;
} semantic_button_t;

/**
 * Initialize one classifier.
 *
 * Every usable control requires a nonzero debounce_ms. Refresh/clear additionally requires a
 * nonzero long_press_ms; previous and next ignore long_press_ms. An unavailable control is always
 * accepted as a no-op and ignores timing and initial-state arguments.
 * Set suppress_if_asserted when an initially asserted control must emit nothing until its first
 * debounced release, such as a control already held when sampling begins.
 */
bool semantic_button_init(semantic_button_t *button, semantic_button_control_t control,
                          semantic_button_config_t config, bool initially_asserted,
                          bool suppress_if_asserted, uint64_t now_ms);

/**
 * Consume one raw asserted/deasserted sample and return at most one semantic event.
 *
 * A transition is accepted at raw_changed_at + debounce_ms. Refresh/clear controls emit clear
 * exactly once at or beyond the configured long boundary; otherwise their debounced release emits
 * refresh. Previous and next emit once on debounced release and have no hold/repeat action.
 */
semantic_button_event_t semantic_button_update(semantic_button_t *button, bool asserted,
                                               uint64_t now_ms);

#endif
