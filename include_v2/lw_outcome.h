/*
 * lw_outcome.h -- 1:1 port of outcome/Outcome.java
 *
 * Java:
 *   public class Outcome {
 *       public Actions fight;
 *       public Map<Integer, FarmerLog> logs = new TreeMap<>();
 *       public int winner;
 *       public int duration;
 *       public StatisticsManager statistics;
 *       public Exception exception = null;
 *       public long analyzeTime = 0;
 *       public long compilationTime = 0;
 *       public long executionTime = 0;
 *   }
 *
 * In C, the Map<Integer, FarmerLog> becomes a parallel array of
 * (key, FarmerLog*) pairs since FarmerLog itself is a heap object owned
 * by the lw_leek module.  Iteration order in Java's TreeMap is by key
 * ascending; we preserve that by walking the table sorted on insert.
 *
 * The Exception field is replaced by a (status, message) pair (int +
 * char[]). The Java engine catches all exceptions in
 * Generator.runScenario, so we just record whether one was raised and
 * its message.
 *
 * Reference:
 *   java_reference/src/main/java/com/leekwars/generator/outcome/Outcome.java
 */
#ifndef LW_OUTCOME_H
#define LW_OUTCOME_H

#include <stdint.h>

#include "lw_actions.h"


/* Forward declarations. */
struct LwFarmerLog;
struct LwStatisticsManager;


/* Maximum number of distinct logs (one per AI owner). Battle royale tops
 * out at ~32 distinct AI owners; we round up. */
#define LW_OUTCOME_MAX_LOGS 64

/* Bound for the exception message buffer.  Java exception messages are
 * usually under ~256 chars; we keep them. */
#define LW_OUTCOME_EXC_MAX 512


/* Java: public Map<Integer, FarmerLog> logs.
 *
 * Each entry is (aiOwner, FarmerLog*).  We keep them sorted by key
 * (ascending) on insert so iteration matches Java's TreeMap order. */
typedef struct {
    int                  key;
    struct LwFarmerLog  *value;
} LwOutcomeLogEntry;


typedef struct {
    /* Java: public Actions fight. We store the LwActions by value here
     * (lifetime tied to LwOutcome). */
    LwActions fight;

    /* logs map */
    LwOutcomeLogEntry logs[LW_OUTCOME_MAX_LOGS];
    int               n_logs;

    int winner;
    int duration;

    /* Java: public StatisticsManager statistics; */
    struct LwStatisticsManager *statistics;

    /* Java: public Exception exception = null;
     * In C: status + message. status == 0 means "no exception".
     * message[0] == '\0' is also "no exception". */
    int  exception_status;
    char exception_message[LW_OUTCOME_EXC_MAX];

    int64_t analyze_time;
    int64_t compilation_time;
    int64_t execution_time;
} LwOutcome;


/* Java: outcome = new Outcome();  (default-constructed) */
void lw_outcome_init(LwOutcome *self);


/* logs.containsKey(aiOwner) */
int  lw_outcome_logs_contains_key(const LwOutcome *self, int ai_owner);

/* logs.put(aiOwner, farmerLog).  Caller-owned FarmerLog pointer. */
void lw_outcome_logs_put(LwOutcome *self, int ai_owner, struct LwFarmerLog *log);

/* logs.get(aiOwner) -- returns NULL if absent. */
struct LwFarmerLog* lw_outcome_logs_get(const LwOutcome *self, int ai_owner);


/* Java: outcome.exception = e; (port: just record the message). */
void lw_outcome_set_exception(LwOutcome *self, const char *message);


#endif /* LW_OUTCOME_H */
