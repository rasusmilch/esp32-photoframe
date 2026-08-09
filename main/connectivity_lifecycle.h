#ifndef CONNECTIVITY_LIFECYCLE_H
#define CONNECTIVITY_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CONNECTIVITY_SLOT_FREE = 0,
    CONNECTIVITY_SLOT_ACTIVE,
    CONNECTIVITY_SLOT_STOPPING,
    CONNECTIVITY_SLOT_FENCED,
    CONNECTIVITY_SLOT_QUARANTINE,
    CONNECTIVITY_SLOT_FAULT,
} connectivity_slot_state_t;

typedef struct {
    connectivity_slot_state_t state;
    uint64_t epoch;
    uint64_t attempt_id;
    uint64_t generation;
    uint8_t required_stop_mask;
    uint8_t observed_stop_mask;
    uint64_t quarantine_deadline_ms;
    bool fence_posted;
} connectivity_lifecycle_t;

bool connectivity_lifecycle_begin(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                  uint64_t attempt_id, uint64_t generation,
                                  uint8_t required_stop_mask);
bool connectivity_lifecycle_request_stop(connectivity_lifecycle_t *lifecycle);
bool connectivity_lifecycle_observe_stop(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation,
                                         uint8_t stop_mask);
bool connectivity_lifecycle_accept_fence(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation, uint64_t now_ms,
                                         uint64_t quarantine_ms);
bool connectivity_lifecycle_begin_quarantine(connectivity_lifecycle_t *lifecycle);
bool connectivity_lifecycle_release(connectivity_lifecycle_t *lifecycle, uint64_t now_ms);
void connectivity_lifecycle_driver_event(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation);

#endif
