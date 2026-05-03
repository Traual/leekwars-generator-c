import leekwars_c_v2._engine as e
eng = e.Engine()
eng.add_weapon(item_id=37, name="pistol", cost=3, min_range=1, max_range=7,
               launch_type=1, area_id=1, los=True, max_uses=-1,
               effects=[(1, 15.0, 5.0, 0, 31, 0)])
eng.add_farmer(1, "A", "fr"); eng.add_farmer(2, "B", "fr")
eng.add_team(1, "T1"); eng.add_team(2, "T2")
eng.add_entity(team=0, fid=1, name="A", level=100,
               life=2500, tp=14, mp=6, strength=200, agility=0,
               frequency=100, wisdom=0, resistance=0, science=0,
               magic=0, cores=10, ram=10,
               farmer=1, team_id=1, weapons=[37], cell=72)
eng.add_entity(team=1, fid=2, name="B", level=100,
               life=2500, tp=14, mp=6, strength=200, agility=0,
               frequency=100, wisdom=0, resistance=0, science=0,
               magic=0, cores=10, ram=10,
               farmer=2, team_id=2, weapons=[37], cell=234)
eng.set_seed(1); eng.set_max_turns(30)
eng.set_custom_map(obstacles={}, team1=[72], team2=[234])

calls = [0]
def my_ai(idx, turn):
    calls[0] += 1
    if calls[0] > 4: return 0
    n = eng.n_entities()
    my_team = eng.entity_team(idx)
    target = next((i for i in range(n) if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team), None)
    if target is None:
        print(f"call{calls[0]}: no target", flush=True); return 0
    target_cell = eng.entity_cell(target)
    pre_cell = eng.entity_cell(idx)
    used_mp = eng.move_toward(idx, target_cell, 6)
    post_cell = eng.entity_cell(idx)
    print(f"call{calls[0]} idx={idx} cell {pre_cell}->{post_cell} (used_mp={used_mp}) target={target_cell}", flush=True)
    print(f"  used_tp={eng.entity_used_tp(idx)} used_mp={eng.entity_used_mp(idx)}", flush=True)
    fired = 0
    for _ in range(8):
        rc = eng.fire_weapon(idx, 37, target_cell)
        if rc <= 0:
            print(f"  fire rc={rc} used_tp_after={eng.entity_used_tp(idx)}", flush=True)
            break
        fired += 1
        if not eng.entity_alive(target): break
    return fired

eng.set_ai_callback(my_ai)
eng.run()
print(f"done. winner={eng.winner}")
