#include <stdio.h>
#include <stdlib.h>

#include "connectivity_lifecycle.h"

static int failures;
#define CHECK(expression)                                                 \
    do {                                                                  \
        if (!(expression)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void test_release_order(void)
{
    connectivity_lifecycle_t lifecycle = {0};
    CHECK(connectivity_lifecycle_begin(&lifecycle, 1, 7, 3, 3));
    CHECK(!connectivity_lifecycle_begin(&lifecycle, 2, 8, 4, 1));
    CHECK(connectivity_lifecycle_request_stop(&lifecycle));
    CHECK(!connectivity_lifecycle_release(&lifecycle, 100));
    CHECK(!connectivity_lifecycle_observe_stop(&lifecycle, 1, 7, 3, 1));
    CHECK(connectivity_lifecycle_observe_stop(&lifecycle, 1, 7, 3, 2));
    CHECK(!connectivity_lifecycle_accept_fence(&lifecycle, 1, 8, 3, 100, 2000));
    CHECK(connectivity_lifecycle_accept_fence(&lifecycle, 1, 7, 3, 100, 2000));
    CHECK(!connectivity_lifecycle_release(&lifecycle, 2099));
    CHECK(connectivity_lifecycle_release(&lifecycle, 2100));
    CHECK(connectivity_lifecycle_begin(&lifecycle, 2, 8, 4, 1));
}

static void test_post_fence_event_fails_closed(void)
{
    connectivity_lifecycle_t lifecycle = {0};
    CHECK(connectivity_lifecycle_begin(&lifecycle, 4, 9, 5, 1));
    CHECK(connectivity_lifecycle_request_stop(&lifecycle));
    CHECK(connectivity_lifecycle_observe_stop(&lifecycle, 4, 9, 5, 1));
    CHECK(connectivity_lifecycle_accept_fence(&lifecycle, 4, 9, 5, 0, 2000));
    connectivity_lifecycle_driver_event(&lifecycle, 4, 9, 5);
    CHECK(lifecycle.state == CONNECTIVITY_SLOT_FAULT);
    CHECK(!connectivity_lifecycle_release(&lifecycle, 3000));
}

static void test_stale_evidence_cannot_release(void)
{
    connectivity_lifecycle_t lifecycle = {0};
    CHECK(connectivity_lifecycle_begin(&lifecycle, 8, 12, 6, 1));
    CHECK(connectivity_lifecycle_request_stop(&lifecycle));
    CHECK(!connectivity_lifecycle_observe_stop(&lifecycle, 7, 11, 5, 1));
    CHECK(lifecycle.state == CONNECTIVITY_SLOT_STOPPING);
}

int main(void)
{
    test_release_order();
    test_post_fence_event_fails_closed();
    test_stale_evidence_cannot_release();
    if (failures != 0)
        return EXIT_FAILURE;
    puts("connectivity-lifecycle tests passed");
    return EXIT_SUCCESS;
}
