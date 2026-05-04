# Patches for the leek-wars-client (replay viewer)

The replay viewer plays our local fights through the official leek-wars
Vue client (https://github.com/leek-wars/leek-wars-client). The client
needs four small patches to play a `report.json` from disk without a
real Leek Wars backend running.

These patches are NOT included as a `.patch` file because the client is
a separate repo we don't own; just apply them by hand to your local
checkout, then `npm run build`. ~3 minute build.

After patching + building, run from the engine repo root:

```
python bindings/python/lw_replay.py
```

This regenerates `report.json`, starts the local replay server, and
opens the browser at `http://localhost:8080/fight/local`.

---

## Patches (source-side, in leek-wars-client/src/)

### 1. `component/player/game/texture.ts`

The `onerror` handler for image loading was wrapped in an extra arrow
function (`() => () => {}`), so the inner code never executed when an
image failed to load — `resourceLoaded()` was never called and the
launch counter could stall.

```diff
-		const onerror = () => () => {
+		const onerror = () => {
 			console.warn("Error loading : " + this.path)
 			game.resourceLoaded(this.path)
 			this.texture.removeEventListener('load', onload)
 		}
```

Plus a 5 s safety timeout, in case neither `load` nor `error` ever fire
(can happen with CORS-protected SVGs from the production CDN):

```diff
+		// Hard 5s timeout: if neither load nor error fire (rare, but
+		// happens on some servers without proper CORS) treat it as a
+		// failed load so the launch counter still reaches numData.
+		setTimeout(() => { if (!fired) onerror() }, 5000)
```

### 2. `component/player/game/sound.ts`

Same idea: 5 s safety timeout because some `.mp3` files never fire
`loadeddata` when served by the local proxy (autoplay policy +
Range-response edge cases). Also subscribe to `canplay` /
`canplaythrough` so we don't depend on the `loadeddata` event alone:

```diff
+		this.sound.addEventListener("canplay", listener)
+		this.sound.addEventListener("canplaythrough", listener)
+		setTimeout(() => { if (!fired) listener() }, 5000)
```

### 3. `component/player/player.vue` — `getLogs()`

This was THE blocker. `getLogs()` increments `game.numData++` and then
fires an XHR to `localhost:7000/api/fight/get-logs/...`, expecting a
real LeekWars backend to answer. With no backend, the XHR hangs
forever, the counter never reaches `numData`, and the `launch()` call
that flips the player to `loaded = true` never runs.

```diff
 		getLogs() {
+			// Skip the AI-logs fetch for local replays -- there's no
+			// backend at localhost:7000 to answer it, and the request
+			// counts toward `numData` so the launch() trigger never
+			// fires until it resolves.
+			if (this.fightId === 'local') {
+				return
+			}
 			// if (this.$store.state.farmer) {
 				this.game.numData++
 				LeekWars.get('fight/get-logs/' + this.fightId).then(logs => {
 					this.game.setLogs(logs)
 				})
 			// }
 		}
```

### 4. `model/leekwars.ts` — `SERVER` URL

Leek-skin SVG URLs go through `LeekWars.SERVER + "image/leek/svg/..."`,
which by default hardcodes `https://leekwars.com/`. The production CDN
doesn't return CORS headers, so the local page (`http://localhost:8080`)
can't load them. Route through the local origin so they reach the
reverse-proxy in `replay_serve.py`:

```diff
-	SERVER: 'https://leekwars.com/',
+	// DEV / LOCAL replay servers can't load CORS-protected leek SVGs
+	// from the production CDN, so route asset URLs (leek skins etc.)
+	// through the local origin -- the replay server reverse-proxies
+	// /image/* to leekwars.com without the CORS rejection.
+	SERVER: (DEV || LOCAL) ? '/' : 'https://leekwars.com/',
```

`DEV` is already defined in this file as `port === '8080'`, which is
the port `replay_serve.py` listens on by default.
