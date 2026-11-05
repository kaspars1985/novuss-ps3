// Native Sony PlayStation 3 frontend for Novuss::App (PSL1GHT SDK v2).
#include <ppu-lv2.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <rsx/commands.h>
#include <io/pad.h>
#include <audio/audio.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <unistd.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "app.hpp"

using namespace Novuss;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static volatile int g_running = 1;
static volatile int g_sysutilPaused = 0;
static gcmContextData* g_ctx = nullptr;
static void* g_fbPtr[2] = {nullptr, nullptr};
static u32 g_fbOffset[2] = {0, 0};
static int g_width = 1280, g_height = 720;
static u32 g_pitch = 0;
static void* g_hostAddr = nullptr;
static u32 g_fbSize = 0;

static uint32_t* g_ramFb = nullptr;

// Audio state
static u32 g_audioPort = 0;
static bool g_audioOpen = false;
static audioPortConfig g_audioConfig;
static sys_event_queue_t g_audioQueue = 0;
static sys_ipc_key_t g_audioQueueKey = 0;
static sys_ppu_thread_t g_audioThread = 0;
static volatile int g_audioRunning = 0;
static App* g_appPtr = nullptr;

// Persistent pad state per physical port (ioPadGetData only fills when
// data.len > 0). Indexed by physical controller port 0..MAX_PORT_NUM-1.
static padData s_lastData[MAX_PORT_NUM];

// Which physical controller port (0..6) is assigned to Player 0 and Player 1.
static int s_playerPort[2] = {-1, -1};

// ---------------------------------------------------------------------------
// Sysutil callback
// ---------------------------------------------------------------------------
static void onSysutilEvent(u64 status, u64 /*param*/, void* /*usrdata*/) {
    switch (status) {
        case SYSUTIL_MENU_OPEN:
            g_sysutilPaused = 1;
            break;
        case SYSUTIL_MENU_CLOSE:
            g_sysutilPaused = 0;
            break;
        case SYSUTIL_EXIT_GAME:
            g_running = 0;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// RSX helpers (libNoRSX / NoRSXutil pattern)
// ---------------------------------------------------------------------------
static void waitFlip() {
    while (gcmGetFlipStatus() != 0) usleep(300);
    gcmResetFlipStatus();
}

static int flipFrame(gcmContextData* context, s32 buffer) {
    if (gcmSetFlip(context, buffer) == 0) {
        rsxFlushBuffer(context);
        gcmSetWaitFlip(context);
        return 1;
    }
    return 0;
}

static void setRenderTarget(gcmContextData* ctx, int idx) {
    gcmSurface sf;
    memset(&sf, 0, sizeof(sf));
    sf.colorFormat = GCM_SURFACE_X8R8G8B8;
    sf.colorTarget = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorLocation[1] = GCM_LOCATION_RSX;
    sf.colorLocation[2] = GCM_LOCATION_RSX;
    sf.colorLocation[3] = GCM_LOCATION_RSX;
    sf.colorOffset[0] = g_fbOffset[idx];
    sf.colorOffset[1] = 0;
    sf.colorOffset[2] = 0;
    sf.colorOffset[3] = 0;
    sf.colorPitch[0] = g_pitch;
    sf.colorPitch[1] = 64;
    sf.colorPitch[2] = 64;
    sf.colorPitch[3] = 64;
    sf.depthFormat = GCM_SURFACE_ZETA_Z16;
    sf.depthLocation = GCM_LOCATION_RSX;
    sf.depthOffset = 0;
    sf.depthPitch = 64;
    sf.type = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias = GCM_SURFACE_CENTER_1;
    sf.width = g_width;
    sf.height = g_height;
    sf.x = 0;
    sf.y = 0;
    rsxSetSurface(ctx, &sf);
}

// ---------------------------------------------------------------------------
// Video init
// ---------------------------------------------------------------------------
static int initVideo() {
    g_hostAddr = memalign(1024 * 1024, 32 * 1024 * 1024);
    if (!g_hostAddr) return -1;
    if (rsxInit(&g_ctx, 0x100000, 32 * 1024 * 1024, g_hostAddr) != 0) {
        free(g_hostAddr);
        g_hostAddr = nullptr;
        return -1;
    }

    videoState state;
    if (videoGetState(0, 0, &state) != 0) return -1;

    // Prefer 720p so the PPU->RSX framebuffer copy is 3.68 MB instead of
    // 8.29 MB; the PS3 hardware scales to the TV.
    u32 resId = VIDEO_RESOLUTION_720;
    g_width = 1280;
    g_height = 720;
    g_pitch = g_width * 4;

    // Synchronize RSX backend before reconfiguring video.
    rsxSetWriteBackendLabel(g_ctx, 255, 1);
    rsxSetWaitLabel(g_ctx, 255, 1);
    rsxSetWriteBackendLabel(g_ctx, 255, 2);
    rsxFlushBuffer(g_ctx);
    while (*(vu32*)gcmGetLabelAddress(255) != 2) usleep(30);

    videoConfiguration vcfg;
    memset(&vcfg, 0, sizeof(vcfg));
    vcfg.resolution = resId;
    vcfg.format = VIDEO_BUFFER_FORMAT_XRGB;
    vcfg.aspect = state.displayMode.aspect;
    vcfg.pitch = g_pitch;
    if (videoConfigure(0, &vcfg, nullptr, 0) != 0) {
        // Fall back to the display's native resolution.
        videoResolution res;
        if (videoGetResolution(state.displayMode.resolution, &res) == 0) {
            g_width = res.width;
            g_height = res.height;
        } else {
            g_width = 1280;
            g_height = 720;
        }
        g_pitch = g_width * 4;

        memset(&vcfg, 0, sizeof(vcfg));
        vcfg.resolution = state.displayMode.resolution;
        vcfg.format = VIDEO_BUFFER_FORMAT_XRGB;
        vcfg.aspect = state.displayMode.aspect;
        vcfg.pitch = g_pitch;
        if (videoConfigure(0, &vcfg, nullptr, 0) != 0) return -1;
    }
    if (videoGetState(0, 0, &state) != 0) return -1;

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    gcmResetFlipStatus();

    g_fbSize = g_height * g_pitch;
    for (int i = 0; i < 2; ++i) {
        g_fbPtr[i] = rsxMemalign(64, g_fbSize);
        if (!g_fbPtr[i]) {
            if (g_fbPtr[0]) { rsxFree(g_fbPtr[0]); g_fbPtr[0] = nullptr; }
            if (g_fbPtr[1]) { rsxFree(g_fbPtr[1]); g_fbPtr[1] = nullptr; }
            return -1;
        }
        rsxAddressToOffset(g_fbPtr[i], &g_fbOffset[i]);
        gcmSetDisplayBuffer(i, g_fbOffset[i], g_pitch, g_width, g_height);
    }

    g_ramFb = (uint32_t*)memalign(64, g_fbSize);
    if (!g_ramFb) {
        if (g_fbPtr[0]) { rsxFree(g_fbPtr[0]); g_fbPtr[0] = nullptr; }
        if (g_fbPtr[1]) { rsxFree(g_fbPtr[1]); g_fbPtr[1] = nullptr; }
        return -1;
    }
    memset(g_ramFb, 0, g_fbSize);

    flipFrame(g_ctx, 0);
    setRenderTarget(g_ctx, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Audio thread (SDL_psl1ghtaudio.o event queue pattern)
// ---------------------------------------------------------------------------
static void audioThreadMain(void* /*arg*/) {
    sys_event_t ev;
    u32 blockIdx = 0;
    while (g_audioRunning) {
        if (sysEventQueueReceive(g_audioQueue, &ev, 20000) == 0) {
            float* dst = (float*)(uintptr_t)g_audioConfig.audioDataStart +
                         blockIdx * AUDIO_BLOCK_SAMPLES * 2;
            if (g_appPtr && !g_sysutilPaused) {
                g_appPtr->audio.mixStereoFloat(dst, AUDIO_BLOCK_SAMPLES);
            } else {
                memset(dst, 0, AUDIO_BLOCK_SAMPLES * 2 * sizeof(float));
            }
            blockIdx = (blockIdx + 1) % AUDIO_BLOCK_8;
        }
    }
    sysThreadExit(0);
}

static int initAudio(App* app) {
    g_appPtr = app;
    if (audioInit() != 0) return -1;
    audioPortParam params;
    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = 0;
    params.level = 1.0f;
    if (audioPortOpen(&params, &g_audioPort) != 0) return -1;
    if (audioGetPortConfig(g_audioPort, &g_audioConfig) != 0) return -1;
    if (audioCreateNotifyEventQueue(&g_audioQueue, &g_audioQueueKey) != 0) return -1;
    if (audioSetNotifyEventQueue(g_audioQueueKey) != 0) return -1;
    sysEventQueueDrain(g_audioQueue);
    if (audioPortStart(g_audioPort) != 0) return -1;
    g_audioOpen = true;
    g_audioRunning = 1;
    char thrName[] = "novuss_aud";
    if (sysThreadCreate(&g_audioThread, audioThreadMain, nullptr, 100, 0x4000,
                        THREAD_JOINABLE, thrName) != 0) {
        g_audioRunning = 0;
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Pad helpers
// ---------------------------------------------------------------------------
static float normAxis(unsigned char v) {
    float f = ((float)v - 128.0f) / 127.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    const float dz = 0.15f;
    if (fabsf(f) < dz) return 0.0f;
    return (f > 0) ? (f - dz) / (1.0f - dz) : (f + dz) / (1.0f - dz);
}

// Reset a persistent pad snapshot to a neutral, all-released state so that a
// disconnected controller can never leave buttons stuck pressed.
static void resetPadData(padData& d) {
    memset(&d, 0, sizeof(d));
    d.ANA_L_H = 128;
    d.ANA_L_V = 128;
    d.ANA_R_H = 128;
    d.ANA_R_V = 128;
}

static void readPads(PadState pads[2]) {
    pads[0] = PadState();
    pads[1] = PadState();

    padInfo2 info;
    if (ioPadGetInfo2(&info) != 0) {
        for (int p = 0; p < MAX_PORT_NUM; ++p) resetPadData(s_lastData[p]);
        s_playerPort[0] = -1;
        s_playerPort[1] = -1;
        return;
    }

    // Reset any disconnected port's snapshot so stale button state is cleared.
    for (int p = 0; p < MAX_PORT_NUM; ++p) {
        bool connected = (p < (int)info.max) && ((info.port_status[p] & 1) != 0);
        if (!connected) resetPadData(s_lastData[p]);
    }

    // Assign the first two connected ports (ascending) to Player 0 and 1.
    s_playerPort[0] = -1;
    s_playerPort[1] = -1;
    int nextPlayer = 0;
    for (int p = 0; p < MAX_PORT_NUM && nextPlayer < 2; ++p) {
        bool connected = (p < (int)info.max) && ((info.port_status[p] & 1) != 0);
        if (connected) {
            s_playerPort[nextPlayer] = p;
            ++nextPlayer;
        }
    }

    for (int i = 0; i < 2; ++i) {
        int port = s_playerPort[i];
        if (port < 0) continue;

        padData data;
        memset(&data, 0, sizeof(data));
        if (ioPadGetData((u32)port, &data) == 0 && data.len > 0) {
            s_lastData[port] = data;
        }
        const padData& d = s_lastData[port];

        pads[i].connected = true;
        pads[i].lx = normAxis(d.ANA_L_H);
        pads[i].ly = normAxis(d.ANA_L_V);
        pads[i].rx = normAxis(d.ANA_R_H);
        pads[i].ry = normAxis(d.ANA_R_V);

        pads[i].up       = d.BTN_UP != 0;
        pads[i].down     = d.BTN_DOWN != 0;
        pads[i].left     = d.BTN_LEFT != 0;
        pads[i].right    = d.BTN_RIGHT != 0;
        pads[i].cross    = d.BTN_CROSS != 0;
        pads[i].circle   = d.BTN_CIRCLE != 0;
        pads[i].square   = d.BTN_SQUARE != 0;
        pads[i].triangle = d.BTN_TRIANGLE != 0;
        pads[i].l1       = d.BTN_L1 != 0;
        pads[i].r1       = d.BTN_R1 != 0;
        pads[i].l2       = d.BTN_L2 != 0;
        pads[i].r2       = d.BTN_R2 != 0;
        pads[i].start    = d.BTN_START != 0;
        pads[i].select   = d.BTN_SELECT != 0;
        pads[i].l3       = d.BTN_L3 != 0;
        pads[i].r3       = d.BTN_R3 != 0;
    }
}

// ---------------------------------------------------------------------------
// Rumble drain & timer
// ---------------------------------------------------------------------------
static float s_rumbleTimer[2] = {0.0f, 0.0f};
static bool s_rumbleActive[2] = {false, false};

static void stopAllRumble() {
    padActParam offAct;
    memset(&offAct, 0, sizeof(offAct));
    offAct.small_motor = 0;
    offAct.large_motor = 0;
    for (int p = 0; p < MAX_PORT_NUM; ++p) {
        ioPadSetActDirect((u32)p, &offAct);
    }
    s_rumbleActive[0] = false;
    s_rumbleActive[1] = false;
    s_rumbleTimer[0] = 0.0f;
    s_rumbleTimer[1] = 0.0f;
}

static void drainRumble(App& app, double dt) {
    for (auto& ev : app.rumbleQueue) {
        if (ev.pad < 0 || ev.pad > 1) continue;
        int port = s_playerPort[ev.pad];
        if (port < 0) continue;
        padActParam act;
        memset(&act, 0, sizeof(act));
        float s = ev.strength;
        if (s < 0) s = 0;
        if (s > 1) s = 1;
        act.small_motor = (s > 0.15f) ? 1 : 0;
        act.large_motor = (u8)(s * 255.0f);
        ioPadSetActDirect((u32)port, &act);
        s_rumbleTimer[ev.pad] = (ev.duration > 0.01f) ? ev.duration : 0.05f;
        s_rumbleActive[ev.pad] = true;
    }
    app.rumbleQueue.clear();

    for (int p = 0; p < 2; ++p) {
        if (s_rumbleActive[p]) {
            s_rumbleTimer[p] -= (float)dt;
            if (s_rumbleTimer[p] <= 0.0f) {
                int port = s_playerPort[p];
                if (port >= 0) {
                    padActParam act;
                    memset(&act, 0, sizeof(act));
                    act.small_motor = 0;
                    act.large_motor = 0;
                    ioPadSetActDirect((u32)port, &act);
                }
                s_rumbleActive[p] = false;
                s_rumbleTimer[p] = 0.0f;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int /*argc*/, char** /*argv*/) {
    try {
        if (initVideo() != 0) return 1;
        if (ioPadInit(7) != 0) return 1;

        // Initialize persistent pad snapshots to neutral.
        for (int p = 0; p < MAX_PORT_NUM; ++p) resetPadData(s_lastData[p]);

        sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, onSysutilEvent, nullptr);

        App app;

        initAudio(&app); // non-fatal if it fails

        // initVideo() already flipped buffer 0, so buffer 0 is being displayed
        // and buffer 1 is the free backbuffer.
        int fbIdx = 1;
        u64 lastSec = 0, lastNsec = 0;
        sysGetCurrentTime(&lastSec, &lastNsec);
        double lastTime = (double)lastSec + (double)lastNsec * 1e-9;
        PadState pads[2];

        while (g_running) {
            sysUtilCheckCallback();

            if (g_sysutilPaused) {
                // XMB overlay is open: stop rumble, skip update/render, and
                // keep lastTime fresh so dt does not spike on resume.
                stopAllRumble();
                usleep(16000);
                u64 pSec = 0, pNsec = 0;
                sysGetCurrentTime(&pSec, &pNsec);
                lastTime = (double)pSec + (double)pNsec * 1e-9;
                continue;
            }

            u64 sec = 0, nsec = 0;
            sysGetCurrentTime(&sec, &nsec);
            double now = (double)sec + (double)nsec * 1e-9;
            double dt = now - lastTime;
            lastTime = now;
            if (dt > 0.1) dt = 0.1;
            if (dt < 0.0) dt = 0.0;

            readPads(pads);

            u64 u0Sec = 0, u0Nsec = 0;
            sysGetCurrentTime(&u0Sec, &u0Nsec);
            app.update(dt, pads);
            drainRumble(app, dt);
            u64 u1Sec = 0, u1Nsec = 0;
            sysGetCurrentTime(&u1Sec, &u1Nsec);
            float updateMs = (float)((u1Sec - u0Sec) * 1000.0 + (u1Nsec - u0Nsec) * 1e-6);

            u64 r0Sec = 0, r0Nsec = 0;
            sysGetCurrentTime(&r0Sec, &r0Nsec);
            app.render(g_ramFb, g_width, g_height, g_width);
            u64 r1Sec = 0, r1Nsec = 0;
            sysGetCurrentTime(&r1Sec, &r1Nsec);
            float renderMs = (float)((r1Sec - r0Sec) * 1000.0 + (r1Nsec - r0Nsec) * 1e-6);

            struct mallinfo mi = mallinfo();
            app.profiler.update((float)dt, renderMs, updateMs, (uint32_t)mi.uordblks);

            waitFlip();
            memcpy(g_fbPtr[fbIdx], g_ramFb, g_fbSize);
            flipFrame(g_ctx, fbIdx);
            fbIdx ^= 1;
            setRenderTarget(g_ctx, fbIdx);
        }

        // Cleanup
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);

        if (g_audioRunning) {
            g_audioRunning = 0;
            u64 retval = 0;
            sysThreadJoin(g_audioThread, &retval);
        }
        if (g_audioOpen) {
            audioPortStop(g_audioPort);
            audioRemoveNotifyEventQueue(g_audioQueueKey);
            audioPortClose(g_audioPort);
            g_audioOpen = false;
        }
        if (g_audioQueue) {
            sysEventQueueDestroy(g_audioQueue, 0);
            g_audioQueue = 0;
        }
        audioQuit();
        {
            padActParam offAct;
            memset(&offAct, 0, sizeof(offAct));
            for (int p = 0; p < MAX_PORT_NUM; ++p) {
                ioPadSetActDirect((u32)p, &offAct);
            }
        }
        ioPadEnd();
        gcmSetWaitFlip(g_ctx);
        rsxFinish(g_ctx, 1);

        // Free RSX VRAM and host memory now that the RSX is idle.
        if (g_fbPtr[0]) { rsxFree(g_fbPtr[0]); g_fbPtr[0] = nullptr; }
        if (g_fbPtr[1]) { rsxFree(g_fbPtr[1]); g_fbPtr[1] = nullptr; }
        if (g_ramFb) { free(g_ramFb); g_ramFb = nullptr; }
        if (g_hostAddr) { free(g_hostAddr); g_hostAddr = nullptr; }
        return 0;
    } catch (...) {
        return 1;
    }
}
