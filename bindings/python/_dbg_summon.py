import sys, os
sys.path.insert(0, "C:/Users/aurel/Desktop/leekwars_generator_python")
import leekwars_c_v2._engine as e
import test_v2_summon as t

eng = e.Engine()
print("registering chips...", flush=True)
t.register_all_chips_v2(eng)
print("chips registered", flush=True)
print("registering summons...", flush=True)
t.register_summon_templates_v2(eng)
print("summons registered", flush=True)

# Quick weapon
import json
WEAPONS = {int(s["item"]): s for s in json.load(open("C:/Users/aurel/Desktop/leekwars_generator_python/data/weapons.json")).values()}
raw = WEAPONS[37]
eng.add_weapon(item_id=37, name="pistol", cost=3, min_range=1, max_range=7,
               launch_type=1, area_id=1, los=True, max_uses=-1,
               effects=[(1, 15.0, 5.0, 0, 31, 0)])
eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
eng.add_team(1, "T1"); eng.add_team(2, "T2")
eng.add_entity(team=0, fid=1, name="A", level=100,
               life=2500, tp=14, mp=6, strength=200, agility=0,
               frequency=100, wisdom=0, resistance=0, science=0,
               magic=0, cores=10, ram=10,
               farmer=1, team_id=1, weapons=[37], chips=[73], cell=72)
eng.add_entity(team=1, fid=2, name="B", level=100,
               life=2500, tp=14, mp=6, strength=200, agility=0,
               frequency=100, wisdom=0, resistance=0, science=0,
               magic=0, cores=10, ram=10,
               farmer=2, team_id=2, weapons=[37], cell=144)
eng.set_seed(1); eng.set_max_turns(15)
eng.set_custom_map(obstacles={}, team1=[72], team2=[144])

calls = [0]
def my_ai(idx, turn):
    calls[0] += 1
    print(f"call{calls[0]} idx={idx} turn={turn}", flush=True)
    if turn == 1 and idx == 0:
        rc = eng.use_chip(idx, 73, 90)  # cell next to me
        print(f"  summon rc={rc} n_entities now={eng.n_entities()}", flush=True)
    return 0

eng.set_ai_callback(my_ai)
eng.run()
print("done. winner=", eng.winner)
