# Dedicated server

    ./setup-linux-server.sh ~/vdrift ~/VDriftVR ~/vdrift-data   # build + stage
    TRACKS=ruudskogen,estoril88 LAPS=3 BOTS=4 ./run-server.sh   # host it

`vdrift-server.service` supervises it. Open UDP 28600 inbound.

The server is `vdrift-server`, built from `VDriftVR/server/CMakeLists.txt` against the
`quest-port` branch of VDrift: the physics, track, AI and content-loading parts of the
game with a null GL layer in place of the renderer. It needs the same data tree the
headset has (`VDRIFT_DATA_DIRECTORY`), reads car and track files exactly like the game,
and keeps track records under `$HOME/.vdrift`.

## How a race goes

1. **Lobby.** Players join with their car (Garage settings). The countdown starts once
   `--min-players` are in, and the grid is players first, then `--bots` AI cars.
2. **Loading.** The grid is frozen and announced; every client loads the track and all
   cars and reports ready (or `--load-timeout` passes).
3. **Racing.** The server steps Bullet at 90 Hz with every player's latest inputs and the
   AI's own, and sends a state snapshot of every car 30 times a second - the same bytes a
   VDrift replay stores, so clients have the cars exactly as the server has them.
   Collisions between cars happen here, on the server.
4. **Finished.** When every player has done `--laps` laps (or `--finish-wait` seconds
   after the first one did, or `--max-race-time`), results go out, and after 15 s the
   next track in the rotation opens its lobby. Players stay connected between races.

Someone who joins during a race waits, sees the lobby of the next one, and races then.

## Testing it without a headset

`vd-join-probe.cpp` connects as a player, exercises the whole lobby path and counts
snapshots while creeping forward on 30% throttle:

    g++ -O1 -std=c++17 -I ../../../vdrift/src vd-join-probe.cpp -lenet -o vd-join-probe
    ./vd-join-probe 127.0.0.1 28600 Probe XS 60

Run several with different names to fill the grid.

## Windows + WSL

For development the server runs in WSL. With `networkingMode=mirrored` in
`%USERPROFILE%\.wslconfig` it shares the PC's LAN address, so the headset joins
`<pc-ip>:28600`; the Windows firewall must let UDP 28600 in (as administrator):

    netsh advfirewall firewall add rule name="VDrift server UDP 28600" dir=in action=allow protocol=UDP localport=28600
