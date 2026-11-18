// RTSP parser tests for src/RtspParser.c. RtspParser.c is compiled with malloc()/free() redirected to
// the guard allocator below, which places every block right before an inaccessible page, so any read
// past the end of the parser's message buffer faults instead of silently reading heap bytes.
// Usage: sh tests/rtsp/build.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "Rtsp.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static size_t pageSize(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE); // 4 KB or 16 KB on Android
#endif
}
#define MAX_BLOCKS 256

static struct { void* ptr; void* base; size_t size; } blocks[MAX_BLOCKS];

void* rtsptest_malloc(size_t n) {
    size_t PAGE = pageSize();
    size_t pages = (n + PAGE - 1) / PAGE + 1;
    uint8_t* base;
#ifdef _WIN32
    DWORD old;
    base = VirtualAlloc(NULL, pages * PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (base == NULL || !VirtualProtect(base + (pages - 1) * PAGE, PAGE, PAGE_NOACCESS, &old))
        abort();
#else
    base = mmap(NULL, pages * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED || mprotect(base + (pages - 1) * PAGE, PAGE, PROT_NONE) != 0)
        abort();
#endif
    void* p = base + (pages - 1) * PAGE - n;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr == NULL) {
            blocks[i].ptr = p;
            blocks[i].base = base;
            blocks[i].size = pages * PAGE;
            return p;
        }
    }
    abort();
}

void rtsptest_free(void* p) {
    if (p == NULL)
        return;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (blocks[i].ptr == p) {
#ifdef _WIN32
            VirtualFree(blocks[i].base, 0, MEM_RELEASE);
#else
            munmap(blocks[i].base, blocks[i].size);
#endif
            blocks[i].ptr = NULL;
            return;
        }
    }
    abort();
}

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static int parse(const char* text, int length, PRTSP_MESSAGE msg) {
    // Hand the parser an exact-size copy, like a socket read would.
    char* copy = malloc(length > 0 ? length : 1);
    memcpy(copy, text, length);
    int ret = parseRtspMessage(msg, copy, length);
    free(copy);
    return ret;
}

static void expectMalformed(const char* name, const char* text) {
    RTSP_MESSAGE msg;
    int ret = parse(text, (int)strlen(text), &msg);
    CHECK(ret == RTSP_ERROR_MALFORMED, "%s: returned %d, expected RTSP_ERROR_MALFORMED", name, ret);
    if (ret == RTSP_ERROR_SUCCESS)
        freeMessage(&msg);
}

int main(void) {
    RTSP_MESSAGE msg;
    int ret;

    // Well-formed request
    {
        const char* text = "OPTIONS rtsp://10.0.0.1:48010 RTSP/1.0\r\nCSeq: 1\r\nX-GS-ClientVersion: 14\r\nHost: 10.0.0.1\r\n\r\n";
        ret = parse(text, (int)strlen(text), &msg);
        CHECK(ret == RTSP_ERROR_SUCCESS, "request: returned %d", ret);
        if (ret == RTSP_ERROR_SUCCESS) {
            CHECK(msg.type == TYPE_REQUEST, "request: type %d", msg.type);
            CHECK(!strcmp(msg.message.request.command, "OPTIONS"), "request: command %s", msg.message.request.command);
            CHECK(msg.sequenceNumber == 1, "request: CSeq %d", msg.sequenceNumber);
            CHECK(!strcmp(getOptionContent(msg.options, "Host"), "10.0.0.1"), "request: Host");
            CHECK(msg.payload == NULL && msg.payloadLength == 0, "request: unexpected payload");
            freeMessage(&msg);
        }
    }

    // Response with a payload
    {
        const char* text = "RTSP/1.0 200 OK\r\nCSeq: 2\r\nContent-length: 5\r\n\r\nv=0\r\n";
        ret = parse(text, (int)strlen(text), &msg);
        CHECK(ret == RTSP_ERROR_SUCCESS, "response: returned %d", ret);
        if (ret == RTSP_ERROR_SUCCESS) {
            CHECK(msg.type == TYPE_RESPONSE, "response: type %d", msg.type);
            CHECK(msg.message.response.statusCode == 200, "response: status %d", msg.message.response.statusCode);
            CHECK(msg.sequenceNumber == 2, "response: CSeq %d", msg.sequenceNumber);
            CHECK(msg.payloadLength == 5 && !memcmp(msg.payload, "v=0\r\n", 5), "response: payload");
            freeMessage(&msg);
        }
    }

    // Existing leniency: RTSP over ENet may lack the final CRLF, or one byte of it
    {
        const char* text = "RTSP/1.0 200 OK\r\nCSeq: 3\r\n";
        ret = parse(text, (int)strlen(text), &msg);
        CHECK(ret == RTSP_ERROR_SUCCESS && msg.sequenceNumber == 3, "missing final CRLF: returned %d", ret);
        if (ret == RTSP_ERROR_SUCCESS)
            freeMessage(&msg);

        text = "RTSP/1.0 200 OK\r\nCSeq: 4\r\n\r";
        ret = parse(text, (int)strlen(text), &msg);
        CHECK(ret == RTSP_ERROR_SUCCESS && msg.sequenceNumber == 4, "missing last byte: returned %d", ret);
        if (ret == RTSP_ERROR_SUCCESS)
            freeMessage(&msg);
    }

    // Last header without any CRLF: used to read past the end of the parser's buffer (#11)
    expectMalformed("truncated request", "OPTIONS rtsp://10.0.0.1:48010 RTSP/1.0\r\nCSeq: 1");
    expectMalformed("truncated response", "RTSP/1.0 200 OK\r\nCSeq: 1");
    // Passes Helios' plaintext framing (it contains CRLFCRLF) and then hits the same path
    expectMalformed("header after CRLFCRLF", "OPTIONS rtsp://10.0.0.1:48010 RTSP/1.0\r\n\r\nCSeq: 1");
    expectMalformed("header name only", "OPTIONS rtsp://10.0.0.1:48010 RTSP/1.0\r\nCSeq");
    expectMalformed("empty", "");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
