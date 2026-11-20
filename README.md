# Moonlight Streaming Core Library

moonlight-common-c contains the core GameStream client code (RTSP/SDP negotiation, control stream, RTP video and audio,
depacketizer and FEC) shared between [Moonlight](https://moonlight-stream.org) clients. If you are implementing your
own game streaming client that can use a C library, you probably want the code here.

This repository is the fork `eumarcodev/moonlight-common-c`, used as a submodule by the
[Selene Game Streaming](https://github.com/eumarcodev/Selene-Game-Streaming) Android client and the
[Helios](https://github.com/eumarcodev/Helios-Server) host.

## Credits

moonlight-common-c is created by **Cameron Gutman** and the Moonlight contributors:
[moonlight-stream/moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c). It is used by
[Moonlight PC](https://github.com/moonlight-stream/moonlight-qt), [Moonlight Android](https://github.com/moonlight-stream/moonlight-android),
[Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios) and their forks. This fork keeps the original
license and copyright notices.

## What changed in this fork

- PyroWave video format: negotiated in the SDP (with packet size, maximum frame size, bitrate and still screen preferences sent to the host), and frames assembled from the packets that arrived, delivered as soon as their last packet arrives even if some are missing.
- A control message to change the PyroWave frame budget during a session (Helios extension, matched by Helios).
- Stream statistics counters for the client's performance overlay; on audio overflow only the oldest packet is dropped, and late audio is counted apart.
- Faster FEC recovery, and a fix for an RTSP parser over-read.

## Building

moonlight-common-c requires the _specific_ version of ENet that is bundled as a submodule. This version has changes
required for IPv6 compatibility and retransmission reliability, among other things. These are breaking API/ABI changes
which make moonlight-common-c incompatible with other versions of the ENet library. Runtime linking to another libenet
library will cause your client to crash when connecting to recent versions of GeForce Experience.

```bash
git submodule update --init
cmake -B cmake-build -S . && cmake --build cmake-build    # OpenSSL; -DUSE_MBEDTLS=ON for MbedTLS
sh tests/pyrowave/build.sh    # PyroWave frame assembly and SDP preferences
sh tests/fec/build.sh         # Reed-Solomon FEC
sh tests/rtsp/build.sh        # RTSP parser
```

## License

GPLv3, see [LICENSE.txt](LICENSE.txt).
