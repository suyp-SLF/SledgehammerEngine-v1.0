#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <SDL3/SDL_gpu.h>
#include <SDL3_ttf/SDL_ttf.h>

// --- 前向声明 ---
// SDL 相关
struct SDL_Renderer;
struct SDL_GPUDevice;
struct SDL_Texture;
struct SDL_GPUTexture;

// SDL_mixer 相关
struct _Mix_Music;
typedef struct _Mix_Music Mix_Music;
struct MIX_Audio;

namespace engine::resource
{
    // 子管理器前向声明
    class TextureManager;
    class AudioManager;
    class FontManager;
    class ShaderManager;

    /**
     * @brief 资源管理器（门面类）
     * 负责统一管理纹理、音频、字体等资源的生命周期
     */
    class ResourceManager
    {
    private:
        // 渲染后端指针（允许其中一个为 nullptr）
        SDL_Renderer *_renderer = nullptr;
        SDL_GPUDevice *_gpu_device = nullptr;

        SDL_GPUSampler *_default_sampler = nullptr; // ⚡️ 默认采样器

        // 组合各个子管理器
        std::unique_ptr<TextureManager> _texture_manager;
        std::unique_ptr<AudioManager> _audio_manager;
        std::unique_ptr<FontManager> _font_manager;
        std::unique_ptr<ShaderManager> _shader_manager;

    public:
        void init(SDL_Renderer *renderer, SDL_GPUDevice *device);
        /**
         * @brief 构造函数
         * @param renderer 旧版渲染器指针（若不使用则传 nullptr）
         * @param device   SDL3 GPU设备指针（若不使用则传 nullptr）
         */
        explicit ResourceManager(SDL_Renderer *renderer, SDL_GPUDevice *device);
        ~ResourceManager();

        // 禁用拷贝与移动
        ResourceManager(const ResourceManager &) = delete;
        ResourceManager &operator=(const ResourceManager &) = delete;
        ResourceManager(ResourceManager &&) = delete;
        ResourceManager &operator=(ResourceManager &&) = delete;

        // 添加获取函数
        SDL_GPUSampler *getDefaultSampler() const { return _default_sampler; }
        // --- 纹理资源接口 ---

        /** @brief 获取适用于 SDL_Renderer 的旧版纹理 */
        SDL_Texture *getTexture(const std::string &path);

        /** @brief 获取适用于 SDL3 GPU 的现代纹理 */
        SDL_GPUTexture *getGPUTexture(const std::string &path);

        /** @brief 获取纹理的原始尺寸 (px) */
        glm::vec2 getTextureSize(const std::string &path);
        void clearTextures();
        /** @brief 从内存中手动卸载特定纹理 */
        void unloadTexture(const std::string &path);

        SDL_GPUDevice* getGPUDevice() const { return _gpu_device; }

        // --- 音频资源接口 ---

        /** @brief 获取音效数据 (WAV/Chunk) */
        MIX_Audio *getAudio(const std::string &path);

        /** @brief 获取音乐数据 (MP3/OGG/Music) */
        Mix_Music *getMusic(const std::string &path);

        MIX_Audio *loadAudio(const std::string &path);
        void unloadAudio(const std::string &path);

        // --- 字体资源接口 ---

        /** @brief 获取指定大小的字体 */
        TTF_Font *getFont(const std::string &path, int point_size);

        void unloadFont(const std::string &path, int point_size);
        // --- Shader 转发 ---
        SDL_GPUShader *loadShader(
            const std::string &name,
            const std::string &path,
            uint32_t sampler_count = 1,        // 默认 1
            uint32_t uniform_buffer_count = 1, // 默认 1
            uint32_t storage_buffer_count = 0,
            uint32_t storage_texture_count = 0);
        // --- 通用管理 ---

        /** @brief 清空所有已加载的资源 */
        void clear();
    };
}