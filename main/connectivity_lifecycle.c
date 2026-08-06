#include "connectivity_lifecycle.h"

#include <stddef.h>

static bool identity_matches(const connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                             uint64_t attempt_id, uint64_t generation)
{
    return lifecycle->epoch == epoch && lifecycle->attempt_id == attempt_id &&
           lifecycle->generation == generation;
}

bool connectivity_lifecycle_begin(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                  uint64_t attempt_id, uint64_t generation,
                                  uint8_t required_stop_mask)
{
    if (lifecycle == NULL || lifecycle->state != CONNECTIVITY_SLOT_FREE || epoch == 0 ||
        attempt_id == 0 || generation == 0 || required_stop_mask == 0)
        return false;
    *lifecycle = (connectivity_lifecycle_t) {.state = CONNECTIVITY_SLOT_ACTIVE,
                                             .epoch = epoch,
                                             .attempt_id = attempt_id,
                                             .generation = generation,
                                             .required_stop_mask = required_stop_mask};
    return true;
}

bool connectivity_lifecycle_request_stop(connectivity_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL || lifecycle->state != CONNECTIVITY_SLOT_ACTIVE)
        return false;
    lifecycle->state = CONNECTIVITY_SLOT_STOPPING;
    lifecycle->observed_stop_mask = 0;
    lifecycle->fence_posted = false;
    return true;
}

bool connectivity_lifecycle_observe_stop(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation,
                                         uint8_t stop_mask)
{
    if (lifecycle == NULL || lifecycle->state != CONNECTIVITY_SLOT_STOPPING ||
        !identity_matches(lifecycle, epoch, attempt_id, generation))
        return false;
    lifecycle->observed_stop_mask |= stop_mask;
    if ((lifecycle->observed_stop_mask & lifecycle->required_stop_mask) ==
        lifecycle->required_stop_mask) {
        lifecycle->state = CONNECTIVITY_SLOT_FENCED;
        lifecycle->fence_posted = true;
        return true;
    }
    return false;
}

bool connectivity_lifecycle_accept_fence(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation, uint64_t now_ms,
                                         uint64_t quarantine_ms)
{
    if (lifecycle == NULL || lifecycle->state != CONNECTIVITY_SLOT_FENCED ||
        !identity_matches(lifecycle, epoch, attempt_id, generation))
        return false;
    lifecycle->state = CONNECTIVITY_SLOT_QUARANTINE;
    lifecycle->quarantine_deadline_ms =
        UINT64_MAX - now_ms < quarantine_ms ? UINT64_MAX : now_ms + quarantine_ms;
    return true;
}

bool connectivity_lifecycle_begin_quarantine(connectivity_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->state == CONNECTIVITY_SLOT_QUARANTINE;
}

bool connectivity_lifecycle_release(connectivity_lifecycle_t *lifecycle, uint64_t now_ms)
{
    if (lifecycle == NULL || lifecycle->state != CONNECTIVITY_SLOT_QUARANTINE ||
        now_ms < lifecycle->quarantine_deadline_ms)
        return false;
    *lifecycle = (connectivity_lifecycle_t) {0};
    return true;
}

void connectivity_lifecycle_driver_event(connectivity_lifecycle_t *lifecycle, uint64_t epoch,
                                         uint64_t attempt_id, uint64_t generation)
{
    if (lifecycle != NULL && lifecycle->state == CONNECTIVITY_SLOT_QUARANTINE &&
        identity_matches(lifecycle, epoch, attempt_id, generation))
        lifecycle->state = CONNECTIVITY_SLOT_FAULT;
}
