import leekwars_c_v2._engine as e
eng = e.Engine()
eng.add_weapon(item_id=37, name="pistol", cost=3, min_range=1, max_range=7,
               launch_type=1, area_id=1, los=True, max_uses=-1,
               effects=[(1, 15.0, 5.0, 0, 31, 0)])
eng.add_team(1, "TA"); eng.add_team(2, "TB")
fid = 1
for i in range(2):
    eng.add_farmer(fid, f"A{i+1}", "fr")
    eng.add_entity(team=0, fid=fid, name=f"A{i+1}", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=fid, team_id=1, weapons=[37], cell=54+18*i)
    fid += 1
for i in range(2):
    eng.add_farmer(fid, f"B{i+1}", "fr")
    eng.add_entity(team=1, fid=fid, name=f"B{i+1}", level=100,
                   life=2500, tp=14, mp=6, strength=200, agility=0,
                   frequency=100, wisdom=0, resistance=0, science=0,
                   magic=0, cores=10, ram=10,
                   farmer=fid, team_id=2, weapons=[37], cell=162+18*i)
    fid += 1
eng.set_seed(1); eng.set_max_turns(30); eng.set_type(2)
eng.set_custom_map(obstacles={}, team1=[54, 72], team2=[162, 180])

calls = [0]
def my_ai(idx, turn):
    calls[0] += 1
    n = eng.n_entities()
    my_team = eng.entity_team(idx)
    target = None
    for i in range(n):
        if i != idx and eng.entity_alive(i) and eng.entity_team(i) != my_team:
            target = i; break
    if calls[0] <= 3:
        print(f"call{calls[0]} idx={idx} my_team={my_team} target={target}", flush=True)
    if target is None:
        return 0
    target_cell = eng.entity_cell(target)
    fired = 0
    for _ in range(8):
        rc = eng.fire_weapon(idx, 37, target_cell)
        if calls[0] <= 3:
            print(f"  fire@{target_cell} -> rc={rc}", flush=True)
        if rc <= 0: break
        fired += 1
        if not eng.entity_alive(target): break
    return fired

eng.set_ai_callback(my_ai)
eng.run()
print(f"done. winner={eng.winner} calls={calls[0]} stream_len={len(eng.stream_dump())}")
