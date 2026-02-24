#include "texture_manager.h"
#include "resource_types.h"
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>

namespace engine::resource
{
    TextureManager::TextureManager(SDL_Renderer *renderer, SDL_GPUDevice *gpu_device)
        : _renderer(renderer), _gpu_device(gpu_device) {}

    TextureManager::~TextureManager()
    {
        clearTextures();
    }

    // --- 实现头文件中声明的所有公开接口 ---

    SDL_Texture *TextureManager::getLegacyTexture(const std::string &path)
    {
        return getInternal(path).sdl_tex; // 注意这里是 sdl_tex
    }

    SDL_GPUTexture *TextureManager::getGPUTexture(const std::string &path)
    {
        return getInternal(path).gpu_tex; // 注意这里是 gpu_tex
    }

    glm::vec2 TextureManager::getTextureSize(const std::string &path)
    {
        auto &res = getInternal(path);
        return res.size;
    }

    void TextureManager::unloadTexture(const std::string &path)
    {
        if (auto it = _cache.find(path); it != _cache.end())
        {
            it->second.release(_renderer, _gpu_device);
            _cache.erase(it);
        }
    }

    void TextureManager::clearTextures()
    {
        for (auto &[path, res] : _cache)
        {
            res.release(_renderer, _gpu_device);
        }
        _cache.clear();
    }

    // --- 核心逻辑 ---

    TextureResource &TextureManager::getInternal(const std::string &path)
    {
        auto it = _cache.find(path);
        if (it == _cache.end())
        {
            if ((!_renderer &&!_gpu_device) || !forceLoad(path))
            {
                static TextureResource empty; // 失败时返回一个空对象，防止崩溃
                return empty;
            }
            return _cache[path];
        }
        return it->second;
    }

    bool TextureManager::forceLoad(const std::string &path)
    {
        spdlog::info("TextureManager实例地址: {} | 成功加载: {}", (void*)this, path);
        SDL_Surface *surf = IMG_Load(path.c_str());
        if (!surf)
            return false;

        // 确保 Surface 是 RGBA32 格式，方便上传 GPU
        SDL_Surface *converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        if (!converted)
            return false;

        TextureResource res;
        res.size = glm::vec2(converted->w, converted->h);

        if (_renderer)
        {
            res.sdl_tex = SDL_CreateTextureFromSurface(_renderer, converted);

            if (res.sdl_tex) {
                SDL_SetTextureBlendMode(res.sdl_tex, SDL_BLENDMODE_BLEND);
                if(!SDL_SetTextureScaleMode(res.sdl_tex, SDL_SCALEMODE_NEAREST)) {
                    spdlog::error("无法设置为临近插值: {}", SDL_GetError());
                }
            }
        }

        if (_gpu_device)
        {
            res.gpu_tex = uploadToGPU(converted);
        }

        SDL_DestroySurface(converted);
        _cache[path] = res;
        return true;
    }

    SDL_GPUTexture *TextureManager::uploadToGPU(SDL_Surface *surface)
    {
        if (!_gpu_device)
            return nullptr;

        // 1. 创建纹理
        SDL_GPUTextureCreateInfo tci = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = (uint32_t)surface->w,
            .height = (uint32_t)surface->h,
            .layer_count_or_depth = 1,
            .num_levels = 1};
        SDL_GPUTexture *gpuTex = SDL_CreateGPUTexture(_gpu_device, &tci);

        // 2. 传输缓冲 (Staging)
        uint32_t size = (uint32_t)(surface->w * surface->h * 4);
        SDL_GPUTransferBufferCreateInfo tbci = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = size};
        SDL_GPUTransferBuffer *staging = SDL_CreateGPUTransferBuffer(_gpu_device, &tbci);

        void *map = SDL_MapGPUTransferBuffer(_gpu_device, staging, false);
        std::memcpy(map, surface->pixels, size);
        SDL_UnmapGPUTransferBuffer(_gpu_device, staging);

        // 3. 提交复制指令
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(_gpu_device);
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo src = {.transfer_buffer = staging, .offset = 0};
        SDL_GPUTextureRegion dst = {.texture = gpuTex, .w = tci.width, .h = tci.height, .d = 1};

        SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(cmd);

        SDL_ReleaseGPUTransferBuffer(_gpu_device, staging);
        return gpuTex;
    }
}