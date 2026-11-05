// Native Sony PlayStation 3 frontend for Novuss::App (PSL1GHT SDK v2 + Tiny3D RSX).
#include <ppu-lv2.h>
#include <sysutil/sysutil.h>
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
#include "render_tiny3d.hpp"

using namespace Novuss;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static volatile int g_running = 1;
static volatile int g_sysutilPaused = 0;

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
        if (!RenderTiny3D::init(1280, 720)) return 1;
        if (ioPadInit(7) != 0) return 1;

        // Initialize persistent pad snapshots to neutral.
        for (int p = 0; p < MAX_PORT_NUM; ++p) resetPadData(s_lastData[p]);

        sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, onSysutilEvent, nullptr);

        App app;
        initAudio(&app); // non-fatal if it fails

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
            if (dt > 0.05) dt = 0.05;
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
            RenderTiny3D::renderFrame(app);
            u64 r1Sec = 0, r1Nsec = 0;
            sysGetCurrentTime(&r1Sec, &r1Nsec);
            float renderMs = (float)((r1Sec - r0Sec) * 1000.0 + (r1Nsec - r0Nsec) * 1e-6);

            struct mallinfo mi = mallinfo();
            app.profiler.update((float)dt, renderMs, updateMs, (uint32_t)mi.uordblks);
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

        stopAllRumble();
        ioPadEnd();

        RenderTiny3D::shutdown();
        return 0;
    } catch (...) {
        return 1;
    }
}
