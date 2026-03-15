#include "sdl_renderer.h"
#include "../core/context.h"
#include "camera.h"
#include "../resource/resource_manager.h"
#include "../component/tilelayer_component.h"
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace engine::render
{
    /**
     * @brief 构造函数，初始化渲染器对象
     * @param renderer SDL渲染器指针，用于实际的渲染操作
     * @param resource_manager 资源管理器指针，用于管理渲染所需的资源
     * @throws std::runtime_error 当renderer或resource_manager为空时抛出异常
     * @note 构造函数会设置默认绘制颜色为黑色(0,0,0,255)
     * @note 构造过程会记录trace和error级别的日志
     */
    SDLRenderer::SDLRenderer(SDL_Renderer *renderer)
        : _sdl_renderer(renderer)
    {

        spdlog::trace("构造 Renderer 完成");
    }

    /**
     * @brief 绘制精灵到屏幕
     *
     * @param camera 相机对象，用于世界坐标到屏幕坐标的转换
     * @param sprite 要绘制的精灵对象
     * @param position 精灵在世界坐标系中的位置
     * @param scale 精灵的缩放比例，x和y方向可以不同
     * @param angle 精灵的旋转角度（度）
     *
     * @note 该函数会执行以下操作：
     *       1. 获取精灵对应的纹理资源
     *       2. 计算精灵的源矩形和目标矩形
     *       3. 应用相机变换将世界坐标转换为屏幕坐标
     *       4. 检查精灵是否在可视区域内
     *       5. 使用SDL渲染旋转后的精灵
     *
     * @warning 如果纹理获取失败或源矩形无效，函数会记录错误并返回
     *
     * @details 特殊字符处理：
     *          - \t: 制表符
     *          - \r: 回车符
     *          - \n: 换行符
     */
    void SDLRenderer::drawSprite(const Camera &camera,
                                 const Sprite &sprite,
                                 const glm::vec2 &position,
                                 const glm::vec2 &scale,
                                 double angle,
                                 const glm::vec4 &uv_rect)
    {
        if (!_res_mgr)
            return;
        auto texture = _res_mgr->getTexture(sprite.getTextureId());
        if (!texture)
            return;

        float tex_w, tex_h;
        if (!SDL_GetTextureSize(texture, &tex_w, &tex_h))
            return;

        // 1. 还原 Source Rect
        SDL_FRect src_rect = {uv_rect.x * tex_w, uv_rect.y * tex_h, uv_rect.z * tex_w, uv_rect.w * tex_h};

        // 2. 目标矩形计算
        glm::vec2 position_screen = camera.worldToScreen(position);
        glm::vec2 logical_size = sprite.getSize() * scale;

        SDL_FRect dst_rect = {position_screen.x, position_screen.y, logical_size.x * scale.x, logical_size.y * scale.y};

        if (!isRectInViewport(camera, dst_rect))
            return;

        SDL_FlipMode flip = sprite.isFlipped() ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderTextureRotated(_sdl_renderer, texture, &src_rect, &dst_rect, angle, nullptr, flip);
    }
    void SDLRenderer::drawParallax(const Camera &camera,
                                   const Sprite &sprite,
                                   const glm::vec2 &position,
                                   const glm::vec2 &scroll_factor,
                                   const glm::bvec2 &repeat,
                                   const glm::vec2 &scale,
                                   double angle)
    {
        if (!engine::core::Context::Current)
            return;

        auto texture = _res_mgr->getTexture(sprite.getTextureId());
        if (!texture)
        {
            spdlog::error("无法为ID：{}的纹理获取纹理", sprite.getTextureId());
            return;
        }
        auto src_rect = getSpriteRect(sprite);
        if (!src_rect.has_value())
        {
            spdlog::error("无法获取精灵的源矩形，ID：{}", sprite.getTextureId());
            return;
        }

        // 应用相机变换
        glm::vec2 position_screen = camera.worldToScreenWithParallax(position, scroll_factor);
        // 计算缩放后的纹理尺寸
        float scaled_width = src_rect.value().w * scale.x;
        float scaled_height = src_rect.value().h * scale.y;
        glm::vec2 start, stop;
        glm::vec2 viewport_size = camera.getViewportSize();

        if (repeat.x)
        {
            start.x = glm::mod(position_screen.x, scaled_width) - scaled_width;
            stop.x = viewport_size.x;
        }
        else
        {
            start.x = position_screen.x;
            stop.x = glm::mod(position_screen.x + scaled_width, viewport_size.x);
        }
        if (repeat.y)
        {
            start.y = glm::mod(position_screen.y, scaled_height) - scaled_height;
            stop.y = viewport_size.y;
        }
        else
        {
            start.y = position_screen.y;
            stop.y = glm::mod(position_screen.y + scaled_height, viewport_size.y);
        }
        for (float x = start.x; x < stop.x; x += scaled_width)
        {
            for (float y = start.y; y < stop.y; y += scaled_height)
            {
                SDL_FRect dst_rect = {x, y, scaled_width, scaled_height};
                if (!SDL_RenderTexture(_sdl_renderer, texture, nullptr, &dst_rect))
                {
                    spdlog::error("渲染纹理失败，ID {}：{}", sprite.getTextureId(), SDL_GetError());
                    return;
                }
            }
        }
    }
    void SDLRenderer::drawChunkVertices(const Camera &camera,
                                        const std::unordered_map<SDL_GPUTexture *, std::vector<GPUVertex>> &verticesPerTexture,
                                        const glm::vec2 &worldOffset)
    {
        // for (const auto &[texture, vertices] : verticesPerTexture)
        // {
        //     if (vertices.empty())
        //         continue;

        //     // 将顶点从世界坐标转换为屏幕坐标（应用相机变换）
        //     // 这里我们复制一份顶点并转换，或者可以预先在 Chunk 中存储世界坐标，这里实时转换
        //     std::vector<SDL_Vertex> sdlVertices;
        //     sdlVertices.reserve(vertices.size());
        //     for (const auto &v : vertices)
        //     {
        //         glm::vec2 worldPos = v.pos + worldOffset; // v.pos 是 glm::vec2
        //         glm::vec2 screenPos = camera.worldToScreen(worldPos);
        //         // 将 glm::vec4 转换为 SDL_FColor（直接 reinterpret_cast 是安全的，因为布局相同）
        //         const SDL_FColor *colorPtr = reinterpret_cast<const SDL_FColor *>(&v.color);
        //         sdlVertices.push_back({
        //             {screenPos.x, screenPos.y}, // 位置
        //             *colorPtr,                    // 颜色（白色）
        //             {v.uv.x, v.uv.y}            // 纹理坐标
        //         });
        //     }

        //     SDL_RenderGeometry(_sdl_renderer, texture,
        //                        sdlVertices.data(), (int)sdlVertices.size(),
        //                        nullptr, 0);
        // }
    }
    void SDLRenderer::drawChunkBatches(const Camera &camera, const std::unordered_map<SDL_GPUTexture *, engine::world::TextureBatch> &batches, const glm::vec2 &worldOffset)
    {
    }
    /**
     * @brief 在UI上绘制一个精灵
     *
     * @param sprite 要绘制的精灵对象，包含纹理ID、翻转状态等信息
     * @param position 绘制位置的坐标(x, y)
     * @param size 可选参数，指定绘制的尺寸(width, height)。如果不提供，则使用精灵的原始尺寸
     *
     * @note 该函数会处理以下特殊情况：
     *       - 纹理获取失败时记录错误并返回
     *       - 精灵源矩形获取失败时记录错误并返回
     *       - 支持水平翻转绘制
     *       - 支持自定义绘制尺寸
     *
     * @warning 函数内部使用SDL_RenderTextureRotated进行绘制，如果绘制失败会记录SDL错误信息
     */
    void SDLRenderer::drawUISprite(const Sprite &sprite, const glm::vec2 &position, const std::optional<glm::vec2> &size)
    {
        auto texture = _res_mgr->getTexture(sprite.getTextureId());
        if (!texture)
            return;

        float tex_w, tex_h;
        SDL_GetTextureSize(texture, &tex_w, &tex_h);

        // 直接从 Sprite 获取缓存的像素区域（如果有的话）或全图
        auto src_opt = sprite.getSourceRect();
        SDL_FRect src_rect;
        if (src_opt)
        {
            src_rect = {src_opt->position.x, src_opt->position.y, src_opt->size.x, src_opt->size.y};
        }
        else
        {
            src_rect = {0, 0, tex_w, tex_h};
        }

        // 目标大小：优先使用参数，其次使用 Sprite 的逻辑大小
        glm::vec2 draw_size = size.value_or(sprite.getSize());

        SDL_FRect dst_rect = {position.x, position.y, draw_size.x, draw_size.y};

        SDL_RenderTextureRotated(_sdl_renderer, texture, &src_rect, &dst_rect, 0.0f, nullptr,
                                 sprite.isFlipped() ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    }

    /**
     * @brief 将渲染缓冲区的内容呈现到屏幕上
     *
     * 该函数调用SDL的渲染呈现功能，将所有渲染操作的结果显示在屏幕上。
     * 如果渲染失败，会记录错误日志。
     *
     * @note 该函数不处理\t、\r或\n等特殊字符，仅处理渲染缓冲区内容
     */
    void SDLRenderer::present()
    {
        if (!SDL_RenderPresent(_sdl_renderer))
        {
            spdlog::error("渲染失败：{}", SDL_GetError());
        }
    }
    /**
     * @brief 清除屏幕渲染缓冲区
     *
     * 该函数使用SDL的渲染清除功能来清除当前渲染目标的内容。
     * 清除操作可能失败，如果失败会记录错误日志。
     *
     * @note 该函数会处理SDL_RenderClear可能返回的错误情况
     * @note 特殊字符处理：\t(制表符), \r(回车符), \n(换行符)
     */
    void SDLRenderer::clearScreen()
    {
        if (!SDL_RenderClear(_sdl_renderer))
        {
            spdlog::error("清屏失败：{}", SDL_GetError());
        }
    }
    /**
     * @brief 设置渲染器的绘制颜色
     * @details 设置用于后续绘图操作的颜色（RGBA格式），包括特殊字符处理（\t, \r, \n）
     * @param r 红色分量值（0-255）
     * @param g 绿色分量值（0-255）
     * @param b 蓝色分量值（0-255）
     * @param a 透明度分量值（0-255）
     * @note 如果设置失败，会记录错误日志
     */
    void SDLRenderer::setDrawColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
    {
        if (!SDL_SetRenderDrawColor(_sdl_renderer, r, g, b, a))
        {
            spdlog::error("设置绘制颜色失败：{}", SDL_GetError());
        }
    }
    /**
     * @brief 设置渲染器的绘制颜色（浮点数格式）
     *
     * @param r 红色分量，范围 [0.0, 1.0]
     * @param g 绿色分量，范围 [0.0, 1.0]
     * @param b 蓝色分量，范围 [0.0, 1.0]
     * @param a 透明度分量，范围 [0.0, 1.0]
     *
     * @note 该方法会调用 SDL_SetRenderDrawColorFloat 设置颜色
     * @note 如果设置失败，会记录错误日志
     * @note 错误信息包含 SDL 返回的错误字符串
     *
     * @warning 颜色值超出范围可能导致未定义行为
     *
     * @see SDL_SetRenderDrawColorFloat
     */
    void SDLRenderer::setDrawColorFloat(float r, float g, float b, float a)
    {
        if (!SDL_SetRenderDrawColorFloat(_sdl_renderer, r, g, b, a))
        {
            spdlog::error("设置绘制颜色失败：{}", SDL_GetError());
        }
    }
    void SDLRenderer::setResourceManager(engine::resource::ResourceManager *mgr)
    {
        _res_mgr = mgr;
    }
    glm::vec2 SDLRenderer::windowToLogical(float window_x, float window_y) const
    {
        glm::vec2 logical_pos; // 获取窗口的缩放比例
        // 依然使用 SDL 的内置转换
        SDL_RenderCoordinatesFromWindow(_sdl_renderer, window_x, window_y, &logical_pos.x, &logical_pos.y);
        return logical_pos;
    }
    void SDLRenderer::clean()
    {
        if (_sdl_renderer)
        {
            SDL_DestroyRenderer(_sdl_renderer);
            _sdl_renderer = nullptr;
        }
    }
    void SDLRenderer::init()
    {
    }
    /**
     * @brief 获取精灵的矩形区域
     * @details 根据精灵的源矩形配置或纹理尺寸计算并返回精灵的矩形区域。
     *          如果精灵配置了源矩形，则直接返回源矩形（需验证尺寸有效性）。
     *          如果未配置源矩形，则从纹理获取完整尺寸作为矩形区域。
     * @param sprite 要获取矩形区域的精灵对象
     * @return std::optional<SDL_FRect> 成功时返回有效的矩形区域，失败时返回std::nullopt
     * @note 可能返回std::nullopt的情况：
     *       - 纹理获取失败
     *       - 源矩形尺寸无效
     *       - 无法获取纹理尺寸
     */
    std::optional<SDL_FRect> SDLRenderer::getSpriteRect(const Sprite &sprite)
    {
        if (!_res_mgr)
            return std::nullopt;

        SDL_Texture *texture = _res_mgr->getTexture(sprite.getTextureId());
        if (!texture)
        {
            spdlog::error("无法为ID：{}的纹理获取纹理", sprite.getTextureId());
            return std::nullopt;
        }
        auto src_rect = sprite.getSourceRect();
        if (src_rect.has_value())
        {
            auto &r = src_rect.value();
            // 映射关系：
            // SDL_FRect.x = position.x
            // SDL_FRect.y = position.y
            // SDL_FRect.w = size.x (宽度)
            // SDL_FRect.h = size.y (高度)
            SDL_FRect t_src_rect = {
                r.position.x,
                r.position.y,
                r.size.x,
                r.size.y};
            if (t_src_rect.w <= 0 || t_src_rect.h <= 0)
            {
                spdlog::error("源矩形尺寸无效，ID：{}", sprite.getTextureId());
                return std::nullopt;
            }
            spdlog::debug("获取源矩形成功，ID：{}，矩形：{}x{}", sprite.getTextureId(), t_src_rect.w, t_src_rect.h);
            return t_src_rect;
        }
        else
        {
            SDL_FRect result = {0, 0, 0, 0};
            if (!SDL_GetTextureSize(texture, &result.w, &result.h))
            {
                spdlog::error("无法获取纹理尺寸，ID：{}", sprite.getTextureId());
                return std::nullopt;
            }
            spdlog::trace("获取纹理尺寸成功，ID：{}，尺寸：{}x{}", sprite.getTextureId(), result.w, result.h);
            return result;
        }
    }
    /**
     * @brief 检查矩形是否完全位于相机视口内
     *
     * @param camera 相机对象，用于获取视口尺寸
     * @param rect 要检查的矩形，包含位置和尺寸信息
     * @return true 如果矩形完全位于视口内
     * @return false 如果矩形部分或完全位于视口外
     *
     * @note 该方法通过比较矩形的四个边界与视口边界的关系来判断
     */
    bool SDLRenderer::isRectInViewport(const Camera &camera, const SDL_FRect &rect)
    {
        glm::vec2 view = camera.getViewportSize();
        // 只要矩形的右边 > 视口左边 且 矩形的左边 < 视口右边，就说明在视口内
        bool horizontal_overlap = (rect.x + rect.w >= 0) && (rect.x <= view.x);
        bool vertical_overlap = (rect.y + rect.h >= 0) && (rect.y <= view.y);

        return horizontal_overlap && vertical_overlap;
    }
}
