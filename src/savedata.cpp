#include "savedata.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__PS3__) || defined(__PPU__)
#include <sysutil/save.h>
#include <sys/memory.h>

namespace Novuss {
namespace Savedata {

namespace {

static const char* kSaveDir = "NOVU00001-SETTINGS";
static const size_t kMemContainerSize = 1 * 1024 * 1024; // 1 MB container
static const size_t kWorkBufferSize = 64 * 1024;        // 64 KB sysutil work buffer

struct SaveContext {
    int step = 0;
    const std::string* iniData = nullptr;
    std::vector<uint8_t> iconData;
    bool success = false;
};

struct LoadContext {
    int step = 0;
    char buf[16 * 1024];
    std::string loadedContent;
    bool success = false;
};

static void saveStatusCb(sysSaveCallbackResult* result, sysSaveStatusIn* in, sysSaveStatusOut* out) {
    (void)in;
    static sysSaveSystemFileParam param;
    std::memset(&param, 0, sizeof(param));

    std::strncpy(param.title, "Novuss", sizeof(param.title) - 1);
    std::strncpy(param.subtitle, "Iestat\xC4\xABjumi", sizeof(param.subtitle) - 1);
    std::strncpy(param.detail, "Novuss galda sp\xC4\x93les iestat\xC4\xABjumi", sizeof(param.detail) - 1);

    out->setParam = &param;
    out->recreateMode = SYS_SAVE_RECREATE_MODE_OVERWRITE_NOT_CORRUPTED;

    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
}

static void saveFileCb(sysSaveCallbackResult* result, sysSaveFileIn* in, sysSaveFileOut* out) {
    (void)in;
    SaveContext* ctx = static_cast<SaveContext*>(result->user_data);
    if (!ctx) {
        result->result = SYS_SAVE_CALLBACK_RESULT_ERROR;
        return;
    }

    if (ctx->step == 0 && !ctx->iconData.empty()) {
        out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
        out->fileType = SYS_SAVE_FILETYPE_CONTENT_ICON0;
        out->filename = nullptr;
        out->buffer = ctx->iconData.data();
        out->size = static_cast<u32>(ctx->iconData.size());
        ctx->step = 1;
        result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    } else if (ctx->step <= 1) {
        out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
        out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
        out->filename = const_cast<char*>("settings.ini");
        out->buffer = const_cast<char*>(ctx->iniData->data());
        out->size = static_cast<u32>(ctx->iniData->size());
        ctx->step = 2;
        ctx->success = true;
        result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    } else {
        result->result = SYS_SAVE_CALLBACK_RESULT_DONE;
    }
}

static void loadStatusCb(sysSaveCallbackResult* result, sysSaveStatusIn* in, sysSaveStatusOut* out) {
    (void)out;
    if (in && (in->isNew || in->numFiles == 0)) {
        result->result = SYS_SAVE_CALLBACK_RESULT_NOT_FOUND;
        return;
    }
    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
}

static void loadFileCb(sysSaveCallbackResult* result, sysSaveFileIn* in, sysSaveFileOut* out) {
    LoadContext* ctx = static_cast<LoadContext*>(result->user_data);
    if (!ctx) {
        result->result = SYS_SAVE_CALLBACK_RESULT_ERROR;
        return;
    }

    if (ctx->step == 0) {
        out->fileOperation = SYS_SAVE_FILE_OPERATION_READ;
        out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
        out->filename = const_cast<char*>("settings.ini");
        out->offset = 0;
        out->size = 0; // Read entire file
        out->bufferSize = sizeof(ctx->buf) - 1;
        out->buffer = ctx->buf;
        ctx->step = 1;
        result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    } else if (ctx->step == 1) {
        u32 readBytes = in ? in->previousOperationResultSize : 0;
        if (readBytes > 0 && readBytes < sizeof(ctx->buf)) {
            ctx->buf[readBytes] = '\0';
            ctx->loadedContent.assign(ctx->buf, readBytes);
            ctx->success = true;
        }
        result->result = SYS_SAVE_CALLBACK_RESULT_DONE;
    } else {
        result->result = SYS_SAVE_CALLBACK_RESULT_DONE;
    }
}

static void loadIcon(std::vector<uint8_t>& iconData) {
    static const char* iconPaths[] = {
        "/dev_hdd0/game/NOVU00001/ICON0.PNG",
        "/dev_hdd0/game/NOVU00001/USRDIR/ICON0.PNG",
        "ICON0.PNG",
        "USRDIR/ICON0.PNG"
    };
    for (const char* p : iconPaths) {
        FILE* f = std::fopen(p, "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < 512 * 1024) {
                iconData.resize(sz);
                if (std::fread(iconData.data(), 1, sz, f) == static_cast<size_t>(sz)) {
                    std::fclose(f);
                    return;
                }
            }
            std::fclose(f);
        }
    }
}

} // namespace

bool load(std::string& outIni) {
    sysSaveBufferSettings bufferSettings;
    std::memset(&bufferSettings, 0, sizeof(bufferSettings));
    bufferSettings.maxDirectories = 1;
    bufferSettings.maxFiles = 4;
    bufferSettings.bufferSize = kWorkBufferSize;
    bufferSettings.buffer = std::malloc(bufferSettings.bufferSize);
    if (!bufferSettings.buffer) return false;

    sys_mem_container_t container = 0;
    s32 memRet = sysMemContainerCreate(&container, kMemContainerSize);

    LoadContext ctx;
    ctx.step = 0;
    ctx.success = false;

    s32 ret = sysSaveAutoLoad2(SYS_SAVE_CURRENT_VERSION,
                               kSaveDir,
                               SYS_SAVE_ERROR_DIALOG_NONE,
                               &bufferSettings,
                               loadStatusCb,
                               loadFileCb,
                               (memRet == 0) ? container : 0,
                               &ctx);

    if (memRet == 0 && container != 0) {
        sysMemContainerDestroy(container);
    }
    std::free(bufferSettings.buffer);

    if (ret == SYS_SAVE_RETURN_DONE && ctx.success && !ctx.loadedContent.empty()) {
        outIni = ctx.loadedContent;
        return true;
    }
    return false;
}

bool save(const std::string& iniContent) {
    if (iniContent.empty()) return false;

    sysSaveBufferSettings bufferSettings;
    std::memset(&bufferSettings, 0, sizeof(bufferSettings));
    bufferSettings.maxDirectories = 1;
    bufferSettings.maxFiles = 4;
    bufferSettings.bufferSize = kWorkBufferSize;
    bufferSettings.buffer = std::malloc(bufferSettings.bufferSize);
    if (!bufferSettings.buffer) return false;

    sys_mem_container_t container = 0;
    s32 memRet = sysMemContainerCreate(&container, kMemContainerSize);

    SaveContext ctx;
    ctx.step = 0;
    ctx.iniData = &iniContent;
    ctx.success = false;
    loadIcon(ctx.iconData);

    s32 ret = sysSaveAutoSave2(SYS_SAVE_CURRENT_VERSION,
                               kSaveDir,
                               SYS_SAVE_ERROR_DIALOG_NONE,
                               &bufferSettings,
                               saveStatusCb,
                               saveFileCb,
                               (memRet == 0) ? container : 0,
                               &ctx);

    if (memRet == 0 && container != 0) {
        sysMemContainerDestroy(container);
    }
    std::free(bufferSettings.buffer);

    return (ret == SYS_SAVE_RETURN_DONE && ctx.success);
}

} // namespace Savedata
} // namespace Novuss

#else

namespace Novuss {
namespace Savedata {

bool load(std::string& /*outIni*/) {
    return false;
}

bool save(const std::string& /*iniContent*/) {
    return false;
}

} // namespace Savedata
} // namespace Novuss

#endif
