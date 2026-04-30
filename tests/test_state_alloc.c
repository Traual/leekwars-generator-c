/* test_state_alloc.c -- alloc/clone/free smoke test. */

#include "lw_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


static void test_alloc_init(void) {
    LwState *s = lw_state_alloc();
    assert(s != NULL);
    assert(s->n_entities == 0);
    assert(s->turn == 0);
    assert(s->max_turns == 64);
    assert(s->winner == -1);
    assert(s->map.topo == NULL);
    /* entity_at_cell should be -1 everywhere */
    for (int i = 0; i < LW_MAX_CELLS; i++) {
        assert(s->map.entity_at_cell[i] == -1);
    }
    lw_state_free(s);
    printf("  test_alloc_init OK\n");
}


static void test_clone(void) {
    LwState *src = lw_state_alloc();
    LwState *dst = lw_state_alloc();

    /* Mutate src */
    src->turn = 17;
    src->seed = 4242;
    src->rng_n = 0xDEADBEEFULL;
    src->n_entities = 3;
    src->entities[0].id = 1;
    src->entities[0].team_id = 0;
    src->entities[0].hp = 1234;
    src->entities[1].id = 2;
    src->entities[1].team_id = 1;
    src->entities[1].hp = 5678;
    src->map.entity_at_cell[42] = 1;
    src->map.entity_at_cell[100] = 2;

    lw_state_clone(dst, src);

    /* Equal */
    assert(dst->turn == 17);
    assert(dst->seed == 4242);
    assert(dst->rng_n == 0xDEADBEEFULL);
    assert(dst->n_entities == 3);
    assert(dst->entities[0].hp == 1234);
    assert(dst->entities[1].hp == 5678);
    assert(dst->map.entity_at_cell[42] == 1);

    /* Independent: mutate dst, src must stay unchanged */
    dst->entities[0].hp = 999;
    assert(src->entities[0].hp == 1234);

    lw_state_free(src);
    lw_state_free(dst);
    printf("  test_clone OK\n");
}


int main(void) {
    printf("test_state_alloc:\n");
    test_alloc_init();
    test_clone();
    return 0;
}
