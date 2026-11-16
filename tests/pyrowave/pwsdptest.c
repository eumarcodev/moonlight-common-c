// Host-side test of the PyroWave preferences in the SDP that moonlight-common-c sends to the host.
#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- stubs for the rest of the library ----
int AppVersionQuad[4] = {7, 1, 431, -1}; // the version Helios reports, it makes IS_SUNSHINE() true
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
AUDIO_RENDERER_CALLBACKS AudioCallbacks;
int NegotiatedVideoFormat;
struct sockaddr_storage RemoteAddr;
bool HighQualitySurroundSupported;
bool HighQualitySurroundEnabled;
int AudioPacketDuration;
bool AudioEncryptionEnabled;
uint16_t RtspPortNumber = 48010;
uint16_t VideoPortNumber = 47998;
uint32_t EncryptionFeaturesSupported;
uint32_t EncryptionFeaturesRequested;
uint32_t EncryptionFeaturesEnabled;
void addrToUrlSafeString(struct sockaddr_storage* addr, char* string, size_t stringLength) {
    (void)addr;
    snprintf(string, stringLength, "127.0.0.1");
}
bool isReferenceFrameInvalidationSupportedByDecoder(void) { return false; }
// Platform.c pulls these in
void cleanupPlatformSockets(void) {}
int initializePlatformSockets(void) { return 0; }
int enet_initialize(void) { return 0; }
void enet_deinitialize(void) {}
void enterLowLatencyMode(void) {}
void exitLowLatencyMode(void) {}

// ---- test harness ----
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

// Builds the SDP for a stream with the given video format and returns it as a string (caller frees).
static char* sdpFor(int videoFormat) {
    int length = 0;
    NegotiatedVideoFormat = videoFormat;
    StreamConfig.width = 1920;
    StreamConfig.height = 1080;
    StreamConfig.fps = 60;
    StreamConfig.bitrate = 60000;
    StreamConfig.packetSize = 1392;
    StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    char* payload = getSdpPayloadForStreamConfig(14, &length);
    if (payload == NULL) {
        return NULL;
    }
    char* copy = calloc(1, length + 1);
    memcpy(copy, payload, length);
    free(payload);
    return copy;
}

int main(void) {
    char* sdp;

    // Nothing set: no PyroWave attributes
    LiSetPyroWavePrefs(0, 0);
    sdp = sdpFor(VIDEO_FORMAT_PYROWAVE);
    CHECK(sdp != NULL);
    CHECK(strstr(sdp, "x-ss-pyrowave.packetSize") == NULL);
    CHECK(strstr(sdp, "x-ss-pyrowave.maxFrameKb") == NULL);
    free(sdp);

    // Both set
    LiSetPyroWavePrefs(1000, 128);
    sdp = sdpFor(VIDEO_FORMAT_PYROWAVE);
    CHECK(sdp != NULL);
    CHECK(strstr(sdp, "a=x-ss-pyrowave.packetSize:1000") != NULL);
    CHECK(strstr(sdp, "a=x-ss-pyrowave.maxFrameKb:128") != NULL);
    // The chroma attribute that the PyroWave ones follow is still there
    CHECK(strstr(sdp, "x-ss-video[0].chromaSamplingType") != NULL);
    free(sdp);

    // Only one set
    LiSetPyroWavePrefs(0, 64);
    sdp = sdpFor(VIDEO_FORMAT_PYROWAVE);
    CHECK(strstr(sdp, "x-ss-pyrowave.packetSize") == NULL);
    CHECK(strstr(sdp, "a=x-ss-pyrowave.maxFrameKb:64") != NULL);
    free(sdp);

    // Out of range values are ignored
    LiSetPyroWavePrefs(100, 99999);
    sdp = sdpFor(VIDEO_FORMAT_PYROWAVE);
    CHECK(strstr(sdp, "x-ss-pyrowave.packetSize") == NULL);
    CHECK(strstr(sdp, "x-ss-pyrowave.maxFrameKb") == NULL);
    free(sdp);

    // Other codecs never get them, even when set
    LiSetPyroWavePrefs(1000, 128);
    sdp = sdpFor(VIDEO_FORMAT_H264);
    CHECK(sdp != NULL);
    CHECK(strstr(sdp, "x-ss-pyrowave") == NULL);
    free(sdp);

    printf(failures ? "FAILED: %d\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
