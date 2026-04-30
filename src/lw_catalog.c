/*
 * lw_catalog.c -- flat attack-spec registry.
 *
 * Implementation: linear search keyed by item_id. The catalog is
 * populated once at fight init so insertion cost doesn't matter; all
 * subsequent lookups are O(N) but N is small (~100).
 */

#include "lw_catalog.h"
#include <string.h>


typedef struct {
    int          item_id;
    LwAttackSpec spec;
    int          used;
} CatalogEntry;


static CatalogEntry g_catalog[LW_CATALOG_MAX];
static int          g_catalog_count = 0;


int lw_catalog_register(int item_id, const LwAttackSpec *spec) {
    if (item_id < 0 || spec == NULL) return -1;

    /* Replace if already present. */
    for (int i = 0; i < g_catalog_count; i++) {
        if (g_catalog[i].used && g_catalog[i].item_id == item_id) {
            g_catalog[i].spec = *spec;
            return 0;
        }
    }

    if (g_catalog_count >= LW_CATALOG_MAX) return -1;
    g_catalog[g_catalog_count].item_id = item_id;
    g_catalog[g_catalog_count].spec = *spec;
    g_catalog[g_catalog_count].used = 1;
    g_catalog_count++;
    return 0;
}


const LwAttackSpec* lw_catalog_get(int item_id) {
    if (item_id < 0) return NULL;
    for (int i = 0; i < g_catalog_count; i++) {
        if (g_catalog[i].used && g_catalog[i].item_id == item_id) {
            return &g_catalog[i].spec;
        }
    }
    return NULL;
}


void lw_catalog_clear(void) {
    memset(g_catalog, 0, sizeof(g_catalog));
    g_catalog_count = 0;
}


int lw_catalog_size(void) {
    int count = 0;
    for (int i = 0; i < g_catalog_count; i++) {
        if (g_catalog[i].used) count++;
    }
    return count;
}
