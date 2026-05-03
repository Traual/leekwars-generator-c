/*
 * lw_outcome.c -- 1:1 port of outcome/Outcome.java
 *
 * The Java class is a POD that the engine fills in Generator.runScenario.
 * It also exposes a toJson() / toString() for upstream serialization;
 * those are handled by the Cython binding (which knows the Python JSON
 * shape).
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/outcome/Outcome.java
 */
#include "lw_outcome.h"

#include <stddef.h>
#include <string.h>


/* Java: public Outcome() {} -- field defaults. */
void lw_outcome_init(LwOutcome *self) {
    if (self == NULL) return;
    memset(self, 0, sizeof(*self));
    /* Java: public Map<Integer, FarmerLog> logs = new TreeMap<>(); */
    self->n_logs = 0;
    /* fight (Actions) is zero-initialised; full init via lw_actions_init
     * is done by the caller (Generator) when wiring up the fight. */
    lw_actions_init(&self->fight);
    self->winner = 0;
    self->duration = 0;
    self->statistics = NULL;
    self->exception_status = 0;
    self->exception_message[0] = '\0';
    self->analyze_time = 0;
    self->compilation_time = 0;
    self->execution_time = 0;
}


/* Java: outcome.logs.containsKey(aiOwner) */
int lw_outcome_logs_contains_key(const LwOutcome *self, int ai_owner) {
    if (self == NULL) return 0;
    for (int i = 0; i < self->n_logs; i++) {
        if (self->logs[i].key == ai_owner) return 1;
    }
    return 0;
}


/* Java: outcome.logs.put(aiOwner, log);
 *
 * We keep the entries sorted by key ascending (TreeMap order). On
 * collision we overwrite (Map.put semantic). */
void lw_outcome_logs_put(LwOutcome *self, int ai_owner, struct LwFarmerLog *log) {
    if (self == NULL) return;
    /* Update if present. */
    for (int i = 0; i < self->n_logs; i++) {
        if (self->logs[i].key == ai_owner) {
            self->logs[i].value = log;
            return;
        }
    }
    if (self->n_logs >= LW_OUTCOME_MAX_LOGS) return;

    /* Insert sorted by key ascending. */
    int pos = self->n_logs;
    for (int i = 0; i < self->n_logs; i++) {
        if (ai_owner < self->logs[i].key) {
            pos = i;
            break;
        }
    }
    for (int j = self->n_logs; j > pos; j--) {
        self->logs[j] = self->logs[j - 1];
    }
    self->logs[pos].key = ai_owner;
    self->logs[pos].value = log;
    self->n_logs++;
}


struct LwFarmerLog* lw_outcome_logs_get(const LwOutcome *self, int ai_owner) {
    if (self == NULL) return NULL;
    for (int i = 0; i < self->n_logs; i++) {
        if (self->logs[i].key == ai_owner) {
            return self->logs[i].value;
        }
    }
    return NULL;
}


/* Java: outcome.exception = e;
 * Port: store the message + status flag.  Anything callable downstream
 * checks exception_status != 0. */
void lw_outcome_set_exception(LwOutcome *self, const char *message) {
    if (self == NULL) return;
    self->exception_status = 1;
    if (message == NULL) {
        self->exception_message[0] = '\0';
        return;
    }
    strncpy(self->exception_message, message, sizeof(self->exception_message) - 1);
    self->exception_message[sizeof(self->exception_message) - 1] = '\0';
}
