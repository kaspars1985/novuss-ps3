#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app.hpp"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

struct Frontend {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioDeviceID audioDevice = 0;
    SDL_GameController* controllers[2] = {nullptr, nullptr};
    std::vector<uint32_t> framebuffer;
    Novuss::App app;
    Novuss::PadState pads[2];
    bool running = true;

    Frontend() : framebuffer(static_cast<size_t>(kWidth) * kHeight, 0) {}

    bool initVideo() {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
            return false;
        }
        window = SDL_CreateWindow("Novuss — LNF / PS3 Edition",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  kWidth, kHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
        if (!window) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer) {
            std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return false;
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
        if (!texture) {
            std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }

    bool initAudio() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = 48000;
        want.format = AUDIO_F32SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = &Frontend::audioCallback;
        want.userdata = this;
        SDL_AudioSpec have;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDevice == 0) {
            std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_PauseAudioDevice(audioDevice, 0);
        return true;
    }

    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len) {
        Frontend* self = static_cast<Frontend*>(userdata);
        int frames = len / (2 * static_cast<int>(sizeof(float)));
        if (frames <= 0) return;
        self->app.audio.mixStereoFloat(reinterpret_cast<float*>(stream), frames);
    }

    void initControllers() {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        for (int i = 0; i < SDL_NumJoysticks() && controllers[0] == nullptr && controllers[1] == nullptr; ++i) {
            if (!SDL_IsGameController(i)) continue;
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (!c) continue;
            if (!controllers[0]) controllers[0] = c;
            else if (!controllers[1]) controllers[1] = c;
        }
    }

    void pollControllers() {
        for (int i = 0; i < 2; ++i) {
            Novuss::PadState& p = pads[i];
            SDL_GameController* c = controllers[i];
            p.connected = c != nullptr;
            if (!c) continue;
            auto axis = [&](SDL_GameControllerAxis a) {
                Sint16 v = SDL_GameControllerGetAxis(c, a);
                float f = v / 32767.0f;
                if (std::fabs(f) < 0.15f) f = 0.0f;
                return f;
            };
            auto btn = [&](SDL_GameControllerButton b) {
                return SDL_GameControllerGetButton(c, b) != 0;
            };
            p.lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
            p.ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
            p.rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
            p.ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
            p.up = btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
            p.down = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            p.left = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            p.right = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            p.cross = btn(SDL_CONTROLLER_BUTTON_A);
            p.circle = btn(SDL_CONTROLLER_BUTTON_B);
            p.square = btn(SDL_CONTROLLER_BUTTON_X);
            p.triangle = btn(SDL_CONTROLLER_BUTTON_Y);
            p.l1 = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            p.r1 = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            p.l2 = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000;
            p.r2 = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
            p.start = btn(SDL_CONTROLLER_BUTTON_START);
            p.select = btn(SDL_CONTROLLER_BUTTON_BACK);
            p.l3 = btn(SDL_CONTROLLER_BUTTON_LEFTSTICK);
            p.r3 = btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
        }
    }

    void pollKeyboard() {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        Novuss::PadState& p = pads[0];
        p.connected = true;
        auto k = [&](SDL_Scancode s) { return keys[s] != 0; };
        if (k(SDL_SCANCODE_W)) p.up = true;
        if (k(SDL_SCANCODE_S)) p.down = true;
        if (k(SDL_SCANCODE_A)) p.left = true;
        if (k(SDL_SCANCODE_D)) p.right = true;
        if (k(SDL_SCANCODE_LEFT)) p.lx = -1.0f;
        if (k(SDL_SCANCODE_RIGHT)) p.lx = 1.0f;
        if (k(SDL_SCANCODE_UP)) p.up = true;
        if (k(SDL_SCANCODE_DOWN)) p.down = true;
        if (k(SDL_SCANCODE_LSHIFT) || k(SDL_SCANCODE_RSHIFT)) p.l1 = true;
        if (k(SDL_SCANCODE_SPACE) || k(SDL_SCANCODE_RETURN)) p.cross = true;
        if (k(SDL_SCANCODE_ESCAPE) || k(SDL_SCANCODE_BACKSPACE)) p.circle = true;
        if (k(SDL_SCANCODE_TAB)) p.select = true;
        if (k(SDL_SCANCODE_M)) p.start = true;
        if (k(SDL_SCANCODE_R)) p.square = true;
        if (k(SDL_SCANCODE_F3) || k(SDL_SCANCODE_P)) p.r3 = true;
    }

    void drainRumble() {
        for (const auto& ev : app.rumbleQueue) {
            if (ev.pad < 0 || ev.pad > 1) continue;
            SDL_GameController* c = controllers[ev.pad];
            if (!c) continue;
            Uint16 lo = static_cast<Uint16>(std::min(1.0f, std::max(0.0f, ev.strength)) * 0xFFFF);
            Uint32 ms = static_cast<Uint32>(std::max(0.0f, ev.duration) * 1000.0f);
            SDL_GameControllerRumble(c, lo, lo, ms);
        }
        app.rumbleQueue.clear();
    }

    void renderFrame() {
        app.render(framebuffer.data(), kWidth, kHeight, kWidth);
        SDL_UpdateTexture(texture, nullptr, framebuffer.data(), kWidth * static_cast<int>(sizeof(uint32_t)));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    bool saveBMP(const std::string& path) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            framebuffer.data(), kWidth, kHeight, 32, kWidth * 4,
            SDL_PIXELFORMAT_ARGB8888);
        if (!surf) {
            std::fprintf(stderr, "SDL_CreateRGBSurfaceWithFormatFrom failed: %s\n", SDL_GetError());
            return false;
        }
        int rc = SDL_SaveBMP(surf, path.c_str());
        if (rc != 0) std::fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
        return rc == 0;
    }

    void step(double dt) {
        pollControllers();
        pollKeyboard();
        app.update(dt, pads);
        drainRumble();
    }

    void handleEvent(const SDL_Event& e) {
        if (e.type == SDL_QUIT) running = false;
        else if (e.type == SDL_CONTROLLERDEVICEADDED) {
            SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
            if (c) {
                if (!controllers[0]) controllers[0] = c;
                else if (!controllers[1]) controllers[1] = c;
                else SDL_GameControllerClose(c);
            }
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (int i = 0; i < 2; ++i) {
                if (controllers[i] && SDL_GameControllerFromInstanceID(e.cdevice.which) == controllers[i]) {
                    SDL_GameControllerClose(controllers[i]);
                    controllers[i] = nullptr;
                }
            }
        }
    }

    int runInteractive() {
        if (!initVideo()) return 1;
        initAudio();
        initControllers();
        Uint64 prev = SDL_GetPerformanceCounter();
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) handleEvent(e);
            Uint64 now = SDL_GetPerformanceCounter();
            double dt = static_cast<double>(now - prev) / SDL_GetPerformanceFrequency();
            prev = now;
            if (dt > 0.1) dt = 0.1;

            Uint64 u0 = SDL_GetPerformanceCounter();
            step(dt);
            Uint64 u1 = SDL_GetPerformanceCounter();
            float updateMs = static_cast<float>((u1 - u0) * 1000.0 / SDL_GetPerformanceFrequency());

            Uint64 r0 = SDL_GetPerformanceCounter();
            renderFrame();
            Uint64 r1 = SDL_GetPerformanceCounter();
            float renderMs = static_cast<float>((r1 - r0) * 1000.0 / SDL_GetPerformanceFrequency());

            app.profiler.update(static_cast<float>(dt), renderMs, updateMs, 0);
        }
        shutdown();
        return 0;
    }

    int runScreenshot(const std::string& path) {
        if (!initVideo()) return 1;
        step(1.0 / 60.0);
        renderFrame();
        bool ok = saveBMP(path);
        shutdown();
        return ok ? 0 : 1;
    }

    int runHeadlessTest() {
        if (!initVideo()) return 1;
        // Menu frames
        for (int i = 0; i < 120; ++i) step(1.0 / 60.0);
        renderFrame();
        saveBMP("build/screen_menu.bmp");

        // Start AI match
        app.settings.mode = "ai";
        app.mode = "ai";
        app.startMatch();
        for (int i = 0; i < 30; ++i) step(1.0 / 60.0);

        // Aim and fire
        pads[0].lx = 0.3f;
        pads[0].cross = true;
        for (int i = 0; i < 20; ++i) step(1.0 / 60.0);
        pads[0].cross = false;
        for (int i = 0; i < 180; ++i) step(1.0 / 60.0);
        renderFrame();
        saveBMP("build/screen_match.bmp");

        // Audio pipeline sanity check
        std::vector<float> buf(1024 * 2, 0.0f);
        app.audio.mixStereoFloat(buf.data(), 1024);
        float peak = 0.0f;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        std::printf("headless-test: audio peak=%.4f\n", peak);

        shutdown();
        return 0;
    }

    void shutdown() {
        for (int i = 0; i < 2; ++i) {
            if (controllers[i]) { SDL_GameControllerClose(controllers[i]); controllers[i] = nullptr; }
        }
        if (audioDevice) { SDL_CloseAudioDevice(audioDevice); audioDevice = 0; }
        if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
        if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
        if (window) { SDL_DestroyWindow(window); window = nullptr; }
        SDL_Quit();
    }
};

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless-test" || arg == "--screenshot") {
            headless = true;
            break;
        }
    }
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Frontend fe;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless-test") {
            int rc = fe.runHeadlessTest();
            return rc;
        }
        if (arg == "--screenshot" && i + 1 < argc) {
            int rc = fe.runScreenshot(argv[++i]);
            return rc;
        }
    }
    return fe.runInteractive();
}
