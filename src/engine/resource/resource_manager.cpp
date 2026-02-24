#include "resource_manager.h"
#include "texture_manager.h"
#include "audio_manager.h"
#include "font_manager.h"
#include "shader_manager.h"
#include <spdlog/spdlog.h>

namespace engine::resource
{
    void ResourceManager::init(SDL_Renderer *renderer, SDL_GPUDevice *device)
    {
        _renderer = renderer;
        _gpu_device = device;

        // 分发设备指针给子管理器
        if (_texture_manager)
            _texture_manager->setDevice(_renderer, _gpu_device);
        if (_shader_manager)
            _shader_manager->setDevice(_gpu_device);

        // ⚡️ 预创建默认 GPU 采样器
        if (_gpu_device && !_default_sampler)
        {
            SDL_GPUSamplerCreateInfo sampler_info = {};
            sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
            sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;

            // ⚡️ 核心修正：防止采样器在 1.0 边缘处采样到对面的像素
            sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

            _default_sampler = SDL_CreateGPUSampler(_gpu_device, &sampler_info);

            if (_default_sampler)
            {
                spdlog::info("ResourceManager: GPU 默认采样器初始化成功");
            }
            else
            {
                spdlog::error("ResourceManager: 创建采样器失败: {}", SDL_GetError());
            }
        }
        spdlog::info("ResourceManager init 完成。后端: {}", _gpu_device ? "SDL_GPU" : "SDL_Renderer");
    }

    ResourceManager::ResourceManager(SDL_Renderer *renderer, SDL_GPUDevice *device)
        : _renderer(renderer), _gpu_device(device)
    {
        _texture_manager = std::make_unique<TextureManager>(renderer, device);
        _audio_manager = std::make_unique<AudioManager>();
        _font_manager = std::make_unique<FontManager>();

        _shader_manager = std::make_unique<ShaderManager>(device);
        // 如果构造时已经传了 device，直接跑一遍 init 的逻辑
        if (_gpu_device || _renderer)
        {
            init(_renderer, _gpu_device);
        }
    }

    ResourceManager::~ResourceManager()
    {
        if (_gpu_device && _default_sampler)
        {
            SDL_ReleaseGPUSampler(_gpu_device, _default_sampler);
            _default_sampler = nullptr;
        }
        clear();
    }

    void ResourceManager::clear()
    {
        if (_texture_manager)
            _texture_manager->clearTextures();
        if (_audio_manager)
            _audio_manager->clearAudios(); // 修正为你的子管理器方法名
        if (_font_manager)
            _font_manager->clearFonts(); // 修正为你的子管理器方法名
        if (_shader_manager)
            _shader_manager->clear();
        spdlog::trace("所有引擎资源已卸载");
    }

    // --- 纹理转发 ---
    SDL_Texture *ResourceManager::getTexture(const std::string &path)
    {
        return _texture_manager->getLegacyTexture(path);
    }
    SDL_GPUTexture *ResourceManager::getGPUTexture(const std::string &path)
    {
        return _texture_manager->getGPUTexture(path);
    }

    glm::vec2 ResourceManager::getTextureSize(const std::string &path)
    {
        if (_texture_manager)
        {
            return _texture_manager->getTextureSize(path);
        }
        return {0.0f, 0.0f};
    }

    void ResourceManager::clearTextures()
    {
    }

    void ResourceManager::unloadTexture(const std::string &path)
    {
    }

    // --- 音频转发 ---
    MIX_Audio *ResourceManager::getAudio(const std::string &path)
    {
        return _audio_manager->getAudio(path);
    }
    void ResourceManager::unloadAudio(const std::string &path)
    {
        _audio_manager->unloadAudio(path);
    }

    // --- 字体转发 ---
    TTF_Font *ResourceManager::getFont(const std::string &path, int size)
    {
        return _font_manager->getFont(path, size);
    }

    // --- Shader 转发 ---
    SDL_GPUShader *ResourceManager::loadShader(
        const std::string &name,
        const std::string &path,
        uint32_t sampler_count,
        uint32_t uniform_buffer_count,
        uint32_t storage_buffer_count,
        uint32_t storage_texture_count)
    {
        if (!_shader_manager || !_gpu_device)
        {
            spdlog::warn("ShaderManager 未就绪: {}", name);
            return nullptr;
        }

        // ⚡️ 关键：这里不再硬编码 1, 1，而是使用传入的变量
        return _shader_manager->loadShader(
            name,
            path,
            sampler_count,
            uniform_buffer_count,
            storage_buffer_count,
            storage_texture_count);
    }
}