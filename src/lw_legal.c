/*
 * lw_legal.c -- legal_actions enumeration for one entity.
 */

#include "lw_legal.h"
#include "lw_pathfinding.h"
#include "lw_los.h"
#include <string.h>


/* Append to out_actions if space remains. Returns new count. */
static int push(LwAction *out, int n, int max, const LwAction *a) {
    if (n >= max) return n;
    out[n] = *a;
    return n + 1;
}


/*
 * Iterate cells within Manhattan range [mn, mx] of (cx, cy) matching
 * launch_type, calling fn(target_id, ctx) for each. Avoids scanning
 * all 600 map cells per weapon/chip.
 */
typedef struct {
    LwAction       *out;
    int            *n;
    int             max;
    LwActionType    type;
    int             chip_id;
    const LwState  *state;
    const LwAttack *attack;
    int             from_id;
    int             require_target_entity;
    /* LoS cache for this enumeration call: (start, target) -> {-1 unset, 0, 1} */
    int8_t          los_cache[LW_MAX_CELLS];
} EnumCtx;


static int cached_los(EnumCtx *ctx, int target_id, const LwAttack *atk) {
    if (!atk->needs_los) return 1;
    int8_t v = ctx->los_cache[target_id];
    if (v == 0) return 0;
    if (v == 1) return 1;
    int ig[1] = { ctx->from_id };
    int ok = lw_verify_los(&ctx->state->map, ctx->from_id, target_id, ig, 1, 1);
    ctx->los_cache[target_id] = (int8_t)(ok ? 1 : 0);
    return ok;
}


static void enum_attack_targets(EnumCtx *ctx) {
    const LwAttack *atk = ctx->attack;
    const LwTopology *topo = ctx->state->map.topo;
    const LwCell *here = &topo->cells[ctx->from_id];
    int mx = atk->max_range, mn = atk->min_range;
    int cx = here->x, cy = here->y;
    int lt = atk->launch_type;
    int allow_line = (lt & 1) != 0;
    int allow_diag = (lt & 2) != 0;
    int allow_any  = (lt & 4) != 0;

    if (mx <= 0) return;

    for (int dx = -mx; dx <= mx; dx++) {
        int adx = (dx < 0) ? -dx : dx;
        int dy_max = mx - adx;
        for (int dy = -dy_max; dy <= dy_max; dy++) {
            int d = adx + ((dy < 0) ? -dy : dy);
            if (d < mn || d > mx) continue;
            if (d == 0 && mn > 0) continue;

            int ady = (dy < 0) ? -dy : dy;
            int on_line = (dx == 0 || dy == 0);
            int on_diag = (adx == ady) && dx != 0;
            if (on_line && !allow_line) continue;
            if (on_diag && !allow_diag) continue;
            if (!on_line && !on_diag && !allow_any) continue;

            int gx = cx + dx - topo->min_x;
            int gy = cy + dy - topo->min_y;
            if (gx < 0 || gx >= LW_COORD_DIM) continue;
            if (gy < 0 || gy >= LW_COORD_DIM) continue;
            int target_id = topo->coord_lut[gx][gy];
            if (target_id < 0) continue;

            if (ctx->require_target_entity &&
                ctx->state->map.entity_at_cell[target_id] < 0) {
                continue;
            }

            if (!cached_los(ctx, target_id, atk)) continue;

            LwAction a;
            lw_action_init(&a, ctx->type);
            a.target_cell_id = target_id;
            if (ctx->type == LW_ACTION_USE_CHIP) {
                a.chip_id = ctx->chip_id;
            }
            *ctx->n = push(ctx->out, *ctx->n, ctx->max, &a);
        }
    }
}


int lw_legal_actions(const LwState *state,
                     int entity_index,
                     const LwInventoryProfile *profile,
                     LwAction *out_actions,
                     int max_out) {
    if (state == NULL || profile == NULL || out_actions == NULL) return 0;
    if (entity_index < 0 || entity_index >= state->n_entities) return 0;
    const LwEntity *e = &state->entities[entity_index];
    if (!e->alive) return 0;

    int n = 0;

    /* 1) END always available */
    LwAction a;
    lw_action_init(&a, LW_ACTION_END);
    n = push(out_actions, n, max_out, &a);

    int avail_tp = (e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP]) - e->used_tp;
    int avail_mp = (e->base_stats[LW_STAT_MP] + e->buff_stats[LW_STAT_MP]) - e->used_mp;
    int here = e->cell_id;
    if (here < 0) return n;

    /* 2) SET_WEAPON for every other weapon in inventory (cost 1 TP) */
    if (avail_tp >= 1) {
        for (int i = 0; i < e->n_weapons; i++) {
            if (i == e->equipped_weapon) continue;
            lw_action_init(&a, LW_ACTION_SET_WEAPON);
            a.weapon_id = e->weapons[i];
            n = push(out_actions, n, max_out, &a);
        }
    }

    /* Per-call LoS cache shared between weapon and chip enumeration. */
    int8_t los_cache[LW_MAX_CELLS];
    for (int i = 0; i < LW_MAX_CELLS; i++) los_cache[i] = -1;

    /* 3) USE_WEAPON with current weapon, if any and TP available */
    if (e->equipped_weapon >= 0) {
        int slot = e->equipped_weapon;
        const LwAttack *atk = &profile->weapon_attacks[slot];
        int cost = profile->weapon_costs[slot];
        if (avail_tp >= cost) {
            EnumCtx ctx = {
                .out = out_actions, .n = &n, .max = max_out,
                .type = LW_ACTION_USE_WEAPON, .chip_id = -1,
                .state = state, .attack = atk, .from_id = here,
                .require_target_entity = 1,
            };
            memcpy(ctx.los_cache, los_cache, sizeof(los_cache));
            enum_attack_targets(&ctx);
            memcpy(los_cache, ctx.los_cache, sizeof(los_cache));
        }
    }

    /* 4) USE_CHIP for each chip not on cooldown and within TP */
    for (int slot = 0; slot < e->n_chips; slot++) {
        int cost = profile->chip_costs[slot];
        if (avail_tp < cost) continue;
        if (e->chip_cooldown[slot] > 0) continue;
        const LwAttack *atk = &profile->chip_attacks[slot];
        EnumCtx ctx = {
            .out = out_actions, .n = &n, .max = max_out,
            .type = LW_ACTION_USE_CHIP, .chip_id = e->chips[slot],
            .state = state, .attack = atk, .from_id = here,
            .require_target_entity = 1,
        };
        memcpy(ctx.los_cache, los_cache, sizeof(los_cache));
        enum_attack_targets(&ctx);
        memcpy(los_cache, ctx.los_cache, sizeof(los_cache));
    }

    /* 5) MOVE: BFS-bounded reachability */
    if (avail_mp > 0) {
        int dest_ids[LW_MAX_CELLS];
        int paths[LW_MAX_CELLS][LW_MAX_PATH_LEN];
        int path_lens[LW_MAX_CELLS];
        int ndest = lw_bfs_reachable(&state->map, here, avail_mp,
                                     dest_ids, paths, path_lens, LW_MAX_CELLS);
        for (int i = 0; i < ndest; i++) {
            if (n >= max_out) break;
            lw_action_init(&a, LW_ACTION_MOVE);
            a.target_cell_id = dest_ids[i];
            a.path_len = path_lens[i];
            for (int k = 0; k < path_lens[i]; k++) a.path[k] = paths[i][k];
            n = push(out_actions, n, max_out, &a);
        }
    }
    return n;
}
