# CLAUDE.md

Guidance for Claude Code in this repository. Workspace-wide rules (language, bench, issue tracking) live in `../CLAUDE.md`.

This is `eumarcodev/moonlight-common-c`, the user's fork of the Moonlight streaming core library (C): RTSP/SDP
negotiation, control stream, RTP video/audio queues, depacketizer and FEC. It is a submodule of Selene
(`app/src/main/jni/moonlight-core/moonlight-common-c`, the client that really uses it) and of Helios (`third-party/moonlight-common-c`).
The fork adds the PyroWave stream (partial-frame delivery, SDP preferences, runtime frame-budget control message).

## 1. Critical commands

Run from the repository root, inside MSYS2 UCRT64 on Windows (`C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "<cmd>"`) or on Linux with gcc:

```bash
git submodule update --init       # enet is required
sh tests/pyrowave/build.sh        # PyroWave frame assembly (pwtest) + SDP preferences (pwsdptest)
sh tests/fec/build.sh             # Reed-Solomon FEC; CFLAGS="-O2 -mssse3" exercises the SSSE3 path
sh tests/rtsp/build.sh            # RTSP parser with a guard-page allocator (out-of-bounds reads fault)
cmake -B cmake-build -S . && cmake --build cmake-build    # library build (cmake-*/ is git-ignored) (OpenSSL; -DUSE_MBEDTLS=ON for MbedTLS)
```

The test scripts write to `/tmp/<name>` (override with `OUT=`). The real check of a change is a Selene build with the bumped submodule and a stream session against Helios.

## 2. Architecture map

- Public API: `src/Limelight.h` (what Selene's JNI bridge calls); internal shared state in `src/Limelight-internal.h`.
- Session setup: `src/Connection.c` (start/stop sequence), `src/RtspConnection.c` + `src/RtspParser.c` (RTSP), `src/SdpGenerator.c` (SDP sent to the host, including PyroWave preferences).
- Control stream: `src/ControlStream.c` (control messages, including the Helios extensions 0x3000–0x3002 and the frame-budget message, which must match `Helios-Server/src/stream.cpp`).
- Video: `src/VideoStream.c`, `src/RtpVideoQueue.c` (RTP + FEC recovery), `src/VideoDepacketizer.c` (frame assembly, PyroWave partial frames).
- Audio: `src/AudioStream.c`, `src/RtpAudioQueue.c`. Input: `src/InputStream.c`.
- Platform layer: `src/Platform*.c/h` (threads, sockets, crypto).
- FEC: `reedsolomon/rs.c`. Transport for control: `enet/` (submodule).
- Tests: `tests/{pyrowave,fec,rtsp}` (standalone gcc builds with stubs, no test framework).

## 3. Hard rules

- **IMPORTANT: use only the bundled `enet` submodule.** It has breaking API/ABI changes; linking any other libenet crashes the client on connect. Never edit or bump `enet/` on your own.
- **IMPORTANT: a control-message or SDP change MUST match Helios** (`src/stream.cpp`, SDP parsing): ship the library PR, then the submodule bumps.
- Helios does not use this library's depacketizer: a client-only change needs only the Selene bump.
- Branches: same gitflow as Helios and Selene. `develop` is the default branch: `feature/*` from `develop`, PR to `develop`; `master` moves only through a PR `develop` → `master` (no release workflows here). Never commit directly to `develop` or `master`: the `master-develop` ruleset rejects direct pushes, force-pushes and deletion of both.
- Call the protocol additions "Helios extensions" in new code, PRs and issues.
- No device-specific logic or constants taken from the bench devices.
- Performance claims (FEC, depacketizer) need a measured before/after.

## 4. Workflow preferences

- Change only what the task needs; unrelated findings become a new issue in `eumarcodev/Helios-Server` (component label `moonlight-common-c`).
- No CI runs for this fork: run the matching `tests/*/build.sh` locally before opening the PR.
- Flow: PR here into `develop` → merge → PR `develop` → `master` → separate submodule bump PR in Selene (and in Helios when the host side is affected).
- Conventional Commits with a scope (`feat(pyrowave):`, `fix(rtsp):`, `perf(fec):`), one commit per logical phase.

## 5. Out of scope

Do not change without being asked:
- **`enet/`** submodule.
- **`LICENSE.txt`**.
- **The public API in `src/Limelight.h`** beyond what the task needs: Selene's JNI bridge depends on it.
