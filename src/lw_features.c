/*
 * lw_features.c -- MLP feature extraction.
 *
 * Direct reads from LwState, no Python or attribute lookups. About
 * 5-10 us per state on modern hardware (vs ~100 us for the Python
 * implementation we ported).
 */

#include "lw_features.h"
#include <math.h>
#include <string.h>


/* Normalisation constants (must match ai/training/features.py) */
#define NORM_TP             20.0f
#define NORM_MP             10.0f
#define NORM_LEVEL         200.0f
#define NORM_STR           400.0f
#define NORM_AGI           200.0f
#define NORM_WIS           100.0f
#define NORM_RES           200.0f
#define NORM_SCI           100.0f
#define NORM_MAG           100.0f
#define NORM_RANGE          12.0f
#define NORM_DMG           200.0f
#define NORM_SHIELD_ABS    200.0f
#define NORM_SHIELD_REL    100.0f
#define NORM_TURN           64.0f


static inline float safe_div(float a, float b) {
    return (b == 0.0f) ? 0.0f : (a / b);
}


/* ----- entity slot writer (32 floats) ----- */

static void write_entity_slot32(const LwEntity *e, int my_team, float dist_to_me,
                                const LwTopology *topo, float *out) {
    /* Default to all zeros for absent / dead entities. */
    memset(out, 0, 32 * sizeof(float));
    if (e == NULL || !e->alive) return;

    /* 0  : is_my_team */
    out[0] = (e->team_id == my_team) ? 1.0f : 0.0f;
    /* 1..7 : HP%, TP/max, MP/max, level, x, y, alive */
    out[1] = safe_div((float)e->hp, (float)(e->total_hp ? e->total_hp : 1));
    int total_tp = e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP];
    int total_mp = e->base_stats[LW_STAT_MP] + e->buff_stats[LW_STAT_MP];
    int avail_tp = total_tp - e->used_tp;
    int avail_mp = total_mp - e->used_mp;
    out[2] = safe_div((float)avail_tp, (float)(total_tp ? total_tp : 1));
    out[3] = safe_div((float)avail_mp, (float)(total_mp ? total_mp : 1));
    out[4] = safe_div((float)e->level, NORM_LEVEL);
    if (e->cell_id >= 0 && e->cell_id < topo->n_cells) {
        out[5] = safe_div((float)topo->cells[e->cell_id].x, NORM_RANGE * 3.0f);
        out[6] = safe_div((float)topo->cells[e->cell_id].y, NORM_RANGE * 3.0f);
    }
    out[7] = 1.0f;  /* alive */

    /* 8..13 : 6 stats (indices match engine STAT_*) */
    int strength = e->base_stats[LW_STAT_STRENGTH]   + e->buff_stats[LW_STAT_STRENGTH];
    int agility  = e->base_stats[LW_STAT_AGILITY]    + e->buff_stats[LW_STAT_AGILITY];
    int wisdom   = e->base_stats[LW_STAT_WISDOM]     + e->buff_stats[LW_STAT_WISDOM];
    int resist   = e->base_stats[LW_STAT_RESISTANCE] + e->buff_stats[LW_STAT_RESISTANCE];
    int science  = e->base_stats[LW_STAT_SCIENCE]    + e->buff_stats[LW_STAT_SCIENCE];
    int magic    = e->base_stats[LW_STAT_MAGIC]      + e->buff_stats[LW_STAT_MAGIC];
    out[8]  = safe_div((float)strength, NORM_STR);
    out[9]  = safe_div((float)agility,  NORM_AGI);
    out[10] = safe_div((float)wisdom,   NORM_WIS);
    out[11] = safe_div((float)resist,   NORM_RES);
    out[12] = safe_div((float)science,  NORM_SCI);
    out[13] = safe_div((float)magic,    NORM_MAG);

    /* 14..18 : equipped weapon profile (range/cost/dmg). We don't
     * have the catalog here, so the AI / Python layer should set
     * these via a follow-up call. Leave 0 for now. */
    /* out[14..18] = 0 */

    /* 19..20 : inventory size + max range proxy */
    out[19] = safe_div((float)e->n_weapons, 6.0f);
    /* out[20] = 0 (max range proxy) */

    /* 21..23 : shields + poison total */
    int abs_shield = e->base_stats[LW_STAT_ABSOLUTE_SHIELD]
                   + e->buff_stats[LW_STAT_ABSOLUTE_SHIELD];
    int rel_shield = e->base_stats[LW_STAT_RELATIVE_SHIELD]
                   + e->buff_stats[LW_STAT_RELATIVE_SHIELD];
    out[21] = safe_div((float)abs_shield, NORM_SHIELD_ABS);
    out[22] = safe_div((float)rel_shield, NORM_SHIELD_REL);

    float poison = 0.0f;
    for (int i = 0; i < e->n_effects; i++) {
        if (e->effects[i].id == LW_EFFECT_POISON) {
            poison += (float)e->effects[i].value;
        }
    }
    out[23] = safe_div(poison, NORM_DMG);

    /* 24, 25 : invincible / unhealable status flags */
    out[24] = (e->state_flags & LW_STATE_INVINCIBLE) ? 1.0f : 0.0f;
    out[25] = (e->state_flags & LW_STATE_UNHEALABLE) ? 1.0f : 0.0f;

    /* 26 : distance to me */
    out[26] = safe_div(dist_to_me, NORM_RANGE * 3.0f);
    /* 27..31 reserved */
}


/* ----- top-level extractor ----- */

static int manhattan_distance(const LwTopology *topo, int a_id, int b_id) {
    if (a_id < 0 || b_id < 0) return 0;
    if (a_id >= topo->n_cells || b_id >= topo->n_cells) return 0;
    int dx = topo->cells[a_id].x - topo->cells[b_id].x;
    int dy = topo->cells[a_id].y - topo->cells[b_id].y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}


void lw_extract_mlp(const LwState *state, int my_team, float *out) {
    memset(out, 0, LW_MLP_FEAT_DIM * sizeof(float));
    if (state == NULL || state->map.topo == NULL) return;
    const LwTopology *topo = state->map.topo;

    /* ----- 0..15 globals ----- */
    out[0] = safe_div((float)state->turn, NORM_TURN);
    int st = state->scenario_type;
    if (st >= 0 && st < 4) out[1 + st] = 1.0f;

    int my_alive = 0, en_alive = 0;
    int my_hp = 0, my_tot = 0, en_hp = 0, en_tot = 0;
    int my_tp_sum = 0, my_mp_sum = 0, en_tp_sum = 0, en_mp_sum = 0;
    int me_idx = -1;

    for (int i = 0; i < state->n_entities; i++) {
        const LwEntity *e = &state->entities[i];
        int tot = e->total_hp ? e->total_hp : 1;
        int e_total_tp = e->base_stats[LW_STAT_TP] + e->buff_stats[LW_STAT_TP];
        int e_total_mp = e->base_stats[LW_STAT_MP] + e->buff_stats[LW_STAT_MP];
        if (e->team_id == my_team) {
            if (e->alive) {
                my_alive++;
                if (me_idx < 0) me_idx = i;
            }
            my_hp += e->hp;
            my_tot += tot;
            my_tp_sum += (e_total_tp - e->used_tp);
            my_mp_sum += (e_total_mp - e->used_mp);
        } else {
            if (e->alive) en_alive++;
            en_hp += e->hp;
            en_tot += tot;
            en_tp_sum += (e_total_tp - e->used_tp);
            en_mp_sum += (e_total_mp - e->used_mp);
        }
    }
    out[5]  = safe_div((float)my_alive, 10.0f);
    out[6]  = safe_div((float)en_alive, 10.0f);
    out[7]  = safe_div((float)my_hp, (float)(my_tot ? my_tot : 1));
    out[8]  = safe_div((float)en_hp, (float)(en_tot ? en_tot : 1));
    out[9]  = safe_div((float)my_tp_sum, NORM_TP * (float)(my_alive ? my_alive : 1));
    out[10] = safe_div((float)my_mp_sum, NORM_MP * (float)(my_alive ? my_alive : 1));
    out[11] = safe_div((float)en_tp_sum, NORM_TP * (float)(en_alive ? en_alive : 1));
    out[12] = safe_div((float)en_mp_sum, NORM_MP * (float)(en_alive ? en_alive : 1));

    if (me_idx < 0) return;
    const LwEntity *me = &state->entities[me_idx];

    /* ----- 16..47 me slot ----- */
    write_entity_slot32(me, my_team, 0.0f, topo, out + 16);

    /* Allies (top 3 by FId, excluding me) */
    int ally_idx[3]    = { -1, -1, -1 };
    int ally_fid[3]    = { 0, 0, 0 };
    int ally_count = 0;

    /* Enemies (top 3 by Manhattan distance to me) */
    int enemy_idx[3]   = { -1, -1, -1 };
    int enemy_dist[3]  = { 0, 0, 0 };
    int enemy_count = 0;

    for (int i = 0; i < state->n_entities; i++) {
        if (i == me_idx) continue;
        const LwEntity *e = &state->entities[i];
        if (!e->alive) continue;

        if (e->team_id == my_team) {
            /* Insert sorted by FId ascending, keep top 3 */
            if (ally_count < 3) {
                ally_idx[ally_count] = i;
                ally_fid[ally_count] = e->fid;
                ally_count++;
            } else {
                int worst = 0;
                for (int k = 1; k < 3; k++)
                    if (ally_fid[k] > ally_fid[worst]) worst = k;
                if (e->fid < ally_fid[worst]) {
                    ally_idx[worst] = i;
                    ally_fid[worst] = e->fid;
                }
            }
        } else {
            int d = manhattan_distance(topo, me->cell_id, e->cell_id);
            if (enemy_count < 3) {
                enemy_idx[enemy_count] = i;
                enemy_dist[enemy_count] = d;
                enemy_count++;
            } else {
                int worst = 0;
                for (int k = 1; k < 3; k++)
                    if (enemy_dist[k] > enemy_dist[worst]) worst = k;
                if (d < enemy_dist[worst]) {
                    enemy_idx[worst] = i;
                    enemy_dist[worst] = d;
                }
            }
        }
    }

    /* Sort allies by FId ascending (insertion sort, n=3) */
    for (int i = 1; i < ally_count; i++) {
        int idx = ally_idx[i], fid = ally_fid[i];
        int j = i - 1;
        while (j >= 0 && ally_fid[j] > fid) {
            ally_idx[j + 1] = ally_idx[j];
            ally_fid[j + 1] = ally_fid[j];
            j--;
        }
        ally_idx[j + 1] = idx;
        ally_fid[j + 1] = fid;
    }
    /* Sort enemies by distance ascending */
    for (int i = 1; i < enemy_count; i++) {
        int idx = enemy_idx[i], d = enemy_dist[i];
        int j = i - 1;
        while (j >= 0 && enemy_dist[j] > d) {
            enemy_idx[j + 1] = enemy_idx[j];
            enemy_dist[j + 1] = enemy_dist[j];
            j--;
        }
        enemy_idx[j + 1] = idx;
        enemy_dist[j + 1] = d;
    }

    /* ----- 48..143 ally slots ----- */
    for (int i = 0; i < 3; i++) {
        const LwEntity *a = (ally_idx[i] >= 0) ? &state->entities[ally_idx[i]] : NULL;
        float d = (a != NULL && a->cell_id >= 0 && me->cell_id >= 0)
            ? (float)manhattan_distance(topo, me->cell_id, a->cell_id)
            : 0.0f;
        write_entity_slot32(a, my_team, d, topo, out + 48 + i * 32);
    }

    /* ----- 144..239 enemy slots ----- */
    for (int i = 0; i < 3; i++) {
        const LwEntity *en = (enemy_idx[i] >= 0) ? &state->entities[enemy_idx[i]] : NULL;
        float d = (en != NULL) ? (float)enemy_dist[i] : 0.0f;
        write_entity_slot32(en, my_team, d, topo, out + 144 + i * 32);
    }

    /* ----- 240..255 tactical block ----- */
    if (me->cell_id >= 0 && enemy_count > 0) {
        int nearest_idx = enemy_idx[0];
        int best_d = enemy_dist[0];
        out[240] = safe_div((float)best_d, NORM_RANGE * 3.0f);
        /* 241..255: weapon-can-hit / threat / LoS reserved -- the AI
         * scoring already gets most of the signal from the slot
         * blocks above. Leaving these zero is what the Python code
         * does on the fast path too unless an LwAttack catalog is
         * provided. */
        (void)nearest_idx;
    }
}
