# The pack: CelerNet mesh + roaming experiences

**English** | *Português: em breve*

CelerOS devices near each other form **one** system — a pack. The dog, the
SmartDisplay and the watch discover each other over BLE and cooperate with
**no connection at all**: messages cross the area by managed flooding, and
each member announces its **role** (speaker, mic, display, motors, leds,
network) so the others know what it can do for the pack.

## How it works

- **CelerNet (API 26)** — the transport: every node broadcasts
  non-connectable 31-byte advertising frames and repeats new ones it hears
  (TTL budget of hops). Presence every ~3 s, messages up to 434 bytes
  (fragmented), broadcast and unicast (API 27), redundancy in place of
  ACKs. It is infrastructure: survives app switches and reboots, state in
  the `celernet` setting.
- **Pack (API 27)** — the meaning: roles travel in the presence beat
  (derived from the board — no configuration), and the OS speaks typed
  envelopes over the mesh (unicast, deduplicated). The first envelope is
  the **roaming party**: the chiptune playing on one device moves to the
  member with a speaker and **resumes from the same millisecond**
  (`Pack.handoffMusic()`).
- **Matilda app** — the pack panel (system app, replaces the old CelerNet
  app): toggles the mesh, lists members with role/signal/age, sends direct
  or broadcast messages and passes the music along.

Only boards built with Bluetooth join the pack (SmartDisplay, robot dog,
Waveshare watch). The network is open (v1): the filter is the network name
(`celer` by default) — use it for telemetry and toy commands, not secrets.

## Trying it on the bench

```
# on each board (shell via celerctl, or the Matilha app):
celerctl shell --run "..."   # CelerNet.start({}) once; state persists

# the party: play music on the dog (Dog Face music tool or any app),
# then hand it off — it continues on the nearest member with a speaker
Pack.handoffMusic()
```

See the [JS API guide](/maikramer/CelerOS/wiki/JS-API) sections 31–32 for
the full `CelerNet.*` and `Pack.*` reference.
