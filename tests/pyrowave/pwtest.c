// Host-side test of the PyroWave frame assembly in moonlight-common-c (RtpvAddPacket bypass + depacketizer).
#include "Limelight-internal.h"
#include "Video.h"
#include "RtpVideoQueue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- stubs for the rest of the library ----
int AppVersionQuad[4] = {7, 1, 431, 0};
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
int NegotiatedVideoFormat;
STREAM_STATS streamStats;
void cleanupPlatformSockets(void) {}
int initializePlatformSockets(void) { return 0; }
int enet_initialize(void) { return 0; }
void enet_deinitialize(void) {}
void connectionDetectedFrameLoss(uint32_t s, uint32_t e) { (void)s; (void)e; }
void connectionReceivedCompleteFrame(uint32_t f) { (void)f; }
void connectionSawFrame(uint32_t f) { (void)f; }
void connectionSendFrameFecStatus(PSS_FRAME_FEC_STATUS s) { (void)s; }
void enterLowLatencyMode(void) {}
void exitLowLatencyMode(void) {}
bool isReferenceFrameInvalidationEnabled(void) { return false; }
bool LiGetCurrentHostDisplayHdrMode(void) { return false; }
uint64_t LiGetMillis(void) { static uint64_t t = 1000; return t++; }
void LiRequestIdrFrame(void) {}
void notifyKeyFrameReceived(void) {}

// ---- test harness ----
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

typedef struct { int frame; int length; char data[4096]; } DELIVERED;
static DELIVERED delivered[32];
static int deliveredCount;

static int submit(PDECODE_UNIT du) {
    DELIVERED* d = &delivered[deliveredCount++];
    d->frame = du->frameNumber;
    d->length = 0;
    for (PLENTRY e = du->bufferList; e != NULL; e = e->next) {
        memcpy(d->data + d->length, e->data, e->length);
        d->length += e->length;
    }
    return DR_OK;
}

static RTP_VIDEO_QUEUE queue;
static uint16_t seq;

// Sends one RTP packet with a payload of `len` bytes filled with `fill`.
static void send_packet(uint16_t sequence, uint32_t frame, uint8_t flags, char fill, int len) {
    int total = sizeof(RTP_PACKET) + 4 + sizeof(NV_VIDEO_PACKET) + len;
    char* buffer = calloc(1, total + sizeof(RTPV_QUEUE_ENTRY));
    PRTP_PACKET p = (PRTP_PACKET)buffer;
    p->header = 0x10; // FLAG_EXTENSION
    p->sequenceNumber = sequence;
    p->timestamp = frame * 1500;
    PNV_VIDEO_PACKET nv = (PNV_VIDEO_PACKET)(buffer + sizeof(RTP_PACKET) + 4);
    nv->frameIndex = frame;
    nv->flags = flags | FLAG_CONTAINS_PIC_DATA;
    memset(nv + 1, fill, len);
    if (RtpvAddPacket(&queue, p, total, (PRTPV_QUEUE_ENTRY)(buffer + total)) != RTPF_RET_QUEUED) {
        free(buffer);
    }
}

static void reset(void) {
    deliveredCount = 0;
    NegotiatedVideoFormat = VIDEO_FORMAT_PYROWAVE;
    initializeVideoDepacketizer(1392);
    seq = 100;
}

int main(void) {
    VideoCallbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
    VideoCallbacks.submitDecodeUnit = submit;
    RtpvInitializeQueue(&queue);

    // 1. A complete frame in order is delivered at once, payloads concatenated
    reset();
    send_packet(100, 1, FLAG_SOF, 'a', 10);
    send_packet(101, 1, 0, 'b', 10);
    CHECK(deliveredCount == 0);
    send_packet(102, 1, FLAG_EOF, 'c', 5);
    CHECK(deliveredCount == 1);
    CHECK(delivered[0].frame == 1 && delivered[0].length == 25);
    CHECK(delivered[0].data[0] == 'a' && delivered[0].data[10] == 'b' && delivered[0].data[20] == 'c');

    // 2. Out of order packets are put back in sequence
    reset();
    send_packet(100, 1, FLAG_SOF, 'a', 4);
    send_packet(102, 1, FLAG_EOF, 'c', 4);
    CHECK(deliveredCount == 0);
    send_packet(101, 1, 0, 'b', 4);
    CHECK(deliveredCount == 1);
    CHECK(delivered[0].length == 12 && delivered[0].data[0] == 'a' && delivered[0].data[4] == 'b' && delivered[0].data[8] == 'c');

    // 3. A lost middle packet: the partial frame is delivered when the next frame starts, no IDR needed
    reset();
    send_packet(100, 1, FLAG_SOF, 'a', 4);
    send_packet(102, 1, FLAG_EOF, 'c', 4);
    CHECK(deliveredCount == 0);
    send_packet(103, 2, FLAG_SOF | FLAG_EOF, 'd', 4);
    CHECK(deliveredCount == 2);
    CHECK(delivered[0].frame == 1 && delivered[0].length == 8 && delivered[0].data[0] == 'a' && delivered[0].data[4] == 'c');
    CHECK(delivered[1].frame == 2 && delivered[1].length == 4);

    // 4. A frame lost entirely does not stall the following ones
    reset();
    send_packet(100, 1, FLAG_SOF | FLAG_EOF, 'a', 4);
    send_packet(105, 4, FLAG_SOF | FLAG_EOF, 'b', 4);
    CHECK(deliveredCount == 2 && delivered[1].frame == 4);

    // 5. Duplicates and packets of an already delivered frame are ignored
    reset();
    send_packet(100, 1, FLAG_SOF, 'a', 4);
    send_packet(100, 1, FLAG_SOF, 'a', 4);
    send_packet(101, 1, FLAG_EOF, 'b', 4);
    CHECK(deliveredCount == 1 && delivered[0].length == 8);
    send_packet(102, 1, FLAG_EOF, 'x', 4);
    send_packet(103, 0, 0, 'y', 4);
    CHECK(deliveredCount == 1);

    // 6. Lost first packet: the frame is still delivered (degraded) once the next frame arrives
    reset();
    send_packet(101, 1, 0, 'b', 4);
    send_packet(102, 1, FLAG_EOF, 'c', 4);
    CHECK(deliveredCount == 0);
    send_packet(103, 2, FLAG_SOF | FLAG_EOF, 'd', 4);
    CHECK(deliveredCount == 2 && delivered[0].length == 8);

    // 7. Sequence number wrap-around inside a frame
    reset();
    send_packet(65535, 1, FLAG_SOF, 'a', 4);
    send_packet(0, 1, FLAG_EOF, 'b', 4);
    CHECK(deliveredCount == 1 && delivered[0].length == 8 && delivered[0].data[0] == 'a' && delivered[0].data[4] == 'b');

    printf(failures ? "%d FAILED\n" : "all PyroWave depacketizer tests passed\n", failures);
    return failures != 0;
}
