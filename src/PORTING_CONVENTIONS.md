# Java → C porting conventions

This is a **mechanical** Java→C translation, NOT a redesign. Every C
function maps to a Java method, in the same file's package class. Code
order, names, comments, formulas, and operation order MUST match the
Java source.

## Naming

- Java `class Foo` → C `typedef struct { ... } LwFoo;` (file
  `include_v2/lw_foo.h` / `src_v2/lw_foo.c`)
- Java method `Foo.barBaz(int x)` → C `lw_foo_bar_baz(LwFoo *self, int x)`
- Java field `int someField` → C field `int some_field` (snake_case)
- Java constants `public final static int FOO_BAR = 3` → C `#define LW_FOO_BAR 3`
  (or in `include_v2/lw_constants.h` if shared across classes)
- Local variable names: keep verbatim (matches Java for grep-ability)

## Type mapping

| Java                         | C                                   |
|------------------------------|-------------------------------------|
| `int`                        | `int`                               |
| `long`                       | `int64_t`                           |
| `double` / `float`           | `double` / `float`                  |
| `boolean`                    | `int` (0/1) -- avoid `bool`         |
| `String`                     | `const char*` (string literal) or `char[N]` |
| `null`                       | `NULL`                              |
| `Foo obj`                    | `LwFoo *obj`                        |
| `Foo[] arr`                  | `LwFoo *arr; int n_arr;`            |
| `ArrayList<X>`               | `X arr[CAP]; int n_arr;`            |
| `TreeMap<Integer, X>`        | dense `X arr[N_KEYS];` if keys bounded; else linear-probe table |
| `HashMap<Integer, X>`        | same as TreeMap (TreeMaps preserve key order; keep walking by key asc to match) |
| `Integer.MAX_VALUE`          | `INT32_MAX`                         |
| `Math.round(d)` (double)     | `lw_java_round(d)` (= `(int)floor(d+0.5)`, see `lw_util.h`) |
| `Math.floor / ceil / abs`    | `floor / ceil / abs` (libm)         |
| `Math.pow(a, b)`             | `pow(a, b)` (libm)                  |
| `Math.signum(d)`             | `lw_java_signum(d)`                 |
| Object equality `==`         | pointer equality `==`               |
| `entity.getFId()`            | `lw_entity_get_fid(entity)`         |

## Polymorphism

Java has interfaces (`Action`) and abstract base classes
(`Effect`, `Area`). Port with a tag-and-switch dispatch:

```c
typedef enum { LW_AREA_TYPE_CIRCLE1, LW_AREA_TYPE_LASER_LINE, ... } LwAreaType;

typedef struct {
    LwAreaType type;
    /* Common fields; subclass-specific data goes inline if small,
       otherwise via a union. */
} LwArea;

LwCellList lw_area_get_area(const LwArea *self, const LwMap *map, ...) {
    switch (self->type) {
        case LW_AREA_TYPE_CIRCLE1: return lw_area_circle1_get_area(map, ...);
        case LW_AREA_TYPE_LASER_LINE: return lw_area_laser_line_get_area(map, ...);
        ...
    }
}
```

For `Effect` -- there are 56 subclasses, but only `apply()` and
`applyStartTurn()` are virtual. Use the same dispatch pattern with
the existing `LwEffectType` enum from `lw_constants.h`.

## Exceptions → return codes

Java methods that `throw` should return a status int and use an out-pointer
for the result, OR set a thread-local error and return a sentinel. Most of
the engine doesn't throw at all; just port the happy path and add a
comment when omitting a non-firing throw.

## Ownership

- Per-fight state lives in `LwState` (passed as `LwState *state` to most
  functions, mirroring how Java's State is the root).
- Entities live in `state->entities[]`, indexed by entity-array-index.
- The engine never frees individual entities mid-fight; `LwState` owns
  everything end-to-end.
- For variable-length output (paths, target lists), use a caller-owned
  buffer + length: `int lw_pathfinding_path(..., LwCell *out_buf, int out_cap)`
  returns the number of cells written.

## Comments

- Keep the Java doc / inline comments **verbatim** in the C file (above
  the function). They're the spec.
- Add `/* Java: <signature> */` above each ported function.
- Where translation differs (e.g. TreeMap → array), add `/* NOTE: */`
  explaining the equivalence.

## Order of operations is sacred

The byte-for-byte parity test compares action streams. Every state
mutation that emits to the action stream must happen in the **same
order** as the Java source -- including:
- log() calls
- statistics callbacks
- effect.apply() before stack/replace check
- Entity field updates

If the Java source does `target.removeLife(); target.onDirectDamage();`
in that order, the C port does the same in that order. Don't reorder
even if it looks equivalent.

## RNG draws are sacred

Every `state.getRandom().getDouble()` call in Java MUST have an
equivalent `lw_rng_double(&state->rng_n)` call at the same code location
in C. Adding or removing a draw breaks all subsequent damage rolls /
critical rolls / start orders.

## Test as you port

After porting a class, run `bindings/python/test_action_stream_strict.py`
on the existing pistol/odachi seeds to make sure baseline parity (1500
seeds) still holds. Once the new code path activates, extend coverage
to weapons that exercise the newly ported feature.
