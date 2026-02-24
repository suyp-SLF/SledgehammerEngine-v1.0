#include "sdl3_gpu_renderer.h"
#include "render_types.h"
#include "sprite.h"
#include "../core/context.h"
#include "../resource/resource_manager.h"
#include "camera.h"
#include <spdlog/spdlog.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

namespace engine::render
{
    SDL3GPURenderer::SDL3GPURenderer(SDL_Window *window) : _window(window)
    {
        initGPU();
    }

    SDL3GPURenderer::~SDL3GPURenderer()
    {
        if (_device)
        {
            SDL_WaitForGPUIdle(_device);
            if (_sprite_pipeline)
                SDL_ReleaseGPUGraphicsPipeline(_device, _sprite_pipeline);
            SDL_DestroyGPUDevice(_device);
        }
    }

    void SDL3GPURenderer::setResourceManager(engine::resource::ResourceManager *mgr)
    {
        _res_mgr = mgr;
        // 当资源管理器到位后，立即尝试创建管线
        if (_device && _window && _res_mgr)
        {
            createPipeline();
        }
    }

    void SDL3GPURenderer::initGPU()
    {

        _device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
        if (!_device)
        {
            spdlog::error("SDL_CreateGPUDevice 失败: {}", SDL_GetError());
            return;
        }
        SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(_device);
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
            spdlog::info("支持 SPIRV");
        if (formats & SDL_GPU_SHADERFORMAT_MSL)
            spdlog::info("支持 MSL");
        if (formats & SDL_GPU_SHADERFORMAT_METALLIB)
            spdlog::info("支持 MetalLib");
        SDL_ClaimWindowForGPUDevice(_device, _window);
    }

    void SDL3GPURenderer::createPipeline()
    {
        if (!_res_mgr || !_device || !_window)
        {
            spdlog::warn("SDL3 GPU: 资源管理器未就绪，跳过管线创建。");
            return;
        }

        // 1. 加载 Shader
        // 顶点着色器：通常只处理坐标，不需要采样器
        SDL_GPUShader *v_shader = _res_mgr->loadShader("sprite_vert", "assets/shaders/sprite.vert", 0, 1);
        SDL_GPUShader *f_shader = _res_mgr->loadShader("sprite_frag", "assets/shaders/sprite.frag", 1, 0);

        if (!v_shader || !f_shader)
        {
            spdlog::error("SDL3 GPU: 无法创建管线，着色器加载失败。");
            return;
        }

        // 2. 配置颜色混合描述符
        SDL_GPUColorTargetDescription color_desc = {};
        color_desc.format = SDL_GetGPUSwapchainTextureFormat(_device, _window);

        // 开启混合
        color_desc.blend_state.enable_blend = true;

        color_desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color_desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_desc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;

        // Alpha 通道的混合
        color_desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_desc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        color_desc.blend_state.color_write_mask = 0xF; // 确保写入 RGBA 所有通道

        // 3. 配置管线描述符
        SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};

        // 绑定着色器
        pipeline_info.vertex_shader = v_shader;
        pipeline_info.fragment_shader = f_shader;

        // 绑定目标信息
        pipeline_info.target_info.num_color_targets = 1;
        pipeline_info.target_info.color_target_descriptions = &color_desc;

        pipeline_info.depth_stencil_state.enable_depth_test = false;
        pipeline_info.depth_stencil_state.enable_depth_write = false;

        // ⚡️ 关键修正：在 SDL 3.2.0 中，不再需要 fragment_sampler_metadata
        // 资源信息已包含在 frag_shader 对象中。

        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        // 4. 创建管线
        if (_sprite_pipeline)
        {
            SDL_ReleaseGPUGraphicsPipeline(_device, _sprite_pipeline);
        }

        _sprite_pipeline = SDL_CreateGPUGraphicsPipeline(_device, &pipeline_info);

        if (!_sprite_pipeline)
        {
            spdlog::error("SDL3 GPU: 创建管线失败: {}", SDL_GetError());
        }
        else
        {
            spdlog::info("SDL3 GPU: 图形管线创建成功！");
        }
    }

    void SDL3GPURenderer::clearScreen()
    {
        _current_cmd = SDL_AcquireGPUCommandBuffer(_device);
        if (!_current_cmd)
            return;

        if (!SDL_AcquireGPUSwapchainTexture(_current_cmd, _window, &_current_swapchain_texture, nullptr, nullptr))
        {
            SDL_SubmitGPUCommandBuffer(_current_cmd); // 没拿到纹理也要提交掉这个空的 cmd
            // 如果走到这里，说明窗口系统没准备好绘制
            spdlog::error("无法获取交换链纹理: {}", SDL_GetError());
            _current_cmd = nullptr;
            return;
        }

        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = _current_swapchain_texture;
        color_target.clear_color = {0.1f, 0.1f, 0.2f, 1.0f};
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        // 仅仅开启 Pass，不在这里做具体的 Pipeline 绑定
        _active_pass = SDL_BeginGPURenderPass(_current_cmd, &color_target, 1, nullptr);
    }
    // void SDL3GPURenderer::drawSprite(const Camera &camera, const Sprite &sprite, const glm::vec2 &position, const glm::vec2 &scale, double angle)
    // {
    //     // 1. 基础安全检查
    //     if (!_active_pass || !_current_cmd || !_sprite_pipeline || !_res_mgr)
    //         return;

    //     SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(sprite.getTextureId());
    //     SDL_GPUSampler *sampler = _res_mgr->getDefaultSampler();

    //     if (!gpu_tex || !sampler)
    //         return;

    //     // 2. 绑定管线
    //     SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);

    //     // --- ⚡️ 临时测试逻辑开始 ---

    //     // 获取当前窗口的像素大小（确保投影矩阵单位正确）
    //     int w, h;
    //     SDL_GetWindowSize(_window, &w, &h);

    //     // 强制创建一个简单的正交投影：左上(0,0)，右下(w,h)
    //     // 注意：glm::ortho 的参数顺序是 (left, right, bottom, top, near, far)
    //     // 这里 top=0, bottom=h 实现了 2D 常见的 Y 轴向下坐标系
    //     glm::mat4 test_p = glm::ortho(0.0f, (float)w, (float)h, 0.0f, 0.0f, 1.0f);

    //     // 强制把物体画在屏幕坐标 (50, 50) 的位置，大小为 200x200 像素
    //     glm::mat4 test_m = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 50.0f, 0.0f));
    //     test_m = glm::scale(test_m, glm::vec3(200.0f, 200.0f, 1.0f));

    //     SpritePushConstants constants;
    //     constants.mvp = test_p * test_m;
    //     constants.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // 强制红色

    //     // --- ⚡️ 临时测试逻辑结束 ---

    //     // 3. 绑定贴图
    //     SDL_GPUTextureSamplerBinding binding = {gpu_tex, sampler};
    //     SDL_BindGPUFragmentSamplers(_active_pass, 0, &binding, 1);

    //     // 4. 推送常量（注意：确保你的着色器现在接收的是 pc.mvp）
    //     SDL_PushGPUVertexUniformData(_current_cmd, 0, &constants, sizeof(constants));

    //     // 5. 执行绘制
    //     SDL_DrawGPUPrimitives(_active_pass, 6, 1, 0, 0);

    //     // 打印一下，确保这个函数确实被每帧调用了
    //     static bool logged = false;
    //     if (!logged)
    //     {
    //         spdlog::info("Debug Draw: Window Size {}x{}, Matrix pushed.", w, h);
    //         logged = true;
    //     }
    // }
    void SDL3GPURenderer::drawSprite(const Camera &camera, const Sprite &sprite, const glm::vec2 &position, const glm::vec2 &scale, double angle)
    {
        if (!_active_pass || !_current_cmd || !_sprite_pipeline || !_res_mgr)
            return;

        SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(sprite.getTextureId());
        SDL_GPUSampler *sampler = _res_mgr->getDefaultSampler();

        if (!gpu_tex || !sampler)
            return;

        // 获取贴图尺寸
        glm::vec2 tex_pixel_size = _res_mgr->getTextureSize(sprite.getTextureId());
        float tex_w = tex_pixel_size.x;
        float tex_h = tex_pixel_size.y;

        if (tex_w <= 0.0f || tex_h <= 0.0f)
            return;

        // --- 变换矩阵计算 ---
        glm::vec2 display_size = sprite.getSize() * scale;

        // ⚡️ 修正：强制物体坐标取整（像素完美对齐）
        glm::vec2 aligned_pos = glm::floor(position);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(aligned_pos, 0.0f));
        if (angle != 0.0)
        {
            model = glm::translate(model, glm::vec3(display_size.x * 0.5f, display_size.y * 0.5f, 0.0f));
            model = glm::rotate(model, glm::radians((float)angle), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, glm::vec3(-display_size.x * 0.5f, -display_size.y * 0.5f, 0.0f));
        }
        model = glm::scale(model, glm::vec3(display_size, 1.0f));

        // 矩阵合并：Projection * View * Model
        glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * model;

        // --- UV 矩形计算与防缝隙处理 ---
        engine::utils::FRect src;
        auto src_opt = sprite.getSourceRect();
        if (src_opt.has_value())
        {
            src = *src_opt;
        }
        else
        {
            src.position = glm::vec2(0.0f, 0.0f);
            src.size = glm::vec2(tex_w, tex_h);
        }

        // ⚡️ 核心黑科技：UV 微收缩 (Inset)
        // 给 UV 坐标增加一个极小的偏移量（约 0.01 像素），防止移动时采样到邻居 Tile 的边缘。
        const float epsilon = 0.005f;

        SpritePushConstants constants;
        constants.mvp = mvp;
        constants.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        constants.uv_rect = glm::vec4(
            (src.position.x + epsilon) / tex_w,
            (src.position.y + epsilon) / tex_h,
            (src.size.x - epsilon * 2.0f) / tex_w,
            (src.size.y - epsilon * 2.0f) / tex_h);

        // --- 执行绘制 ---
        SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);
        SDL_GPUTextureSamplerBinding binding = {gpu_tex, sampler};
        SDL_BindGPUFragmentSamplers(_active_pass, 0, &binding, 1);
        SDL_PushGPUVertexUniformData(_current_cmd, 0, &constants, sizeof(constants));
        SDL_DrawGPUPrimitives(_active_pass, 6, 1, 0, 0);
    }

    void SDL3GPURenderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
    }

    void SDL3GPURenderer::drawParallax(const Camera &camera, const Sprite &sprite, const glm::vec2 &position, const glm::vec2 &scroll_factor, const glm::bvec2 &repeat, const glm::vec2 &scale, double angle)
    {
        // 1. 基础安全检查
        if (!_active_pass || !_current_cmd || !_sprite_pipeline || !_res_mgr)
            return;

        SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(sprite.getTextureId());
        SDL_GPUSampler *sampler = _res_mgr->getDefaultSampler();
        if (!gpu_tex || !sampler)
            return;

        // 获取贴图物理尺寸，用于 UV 映射
        glm::vec2 tex_pixel_size = _res_mgr->getTextureSize(sprite.getTextureId());
        float tex_w = tex_pixel_size.x;
        float tex_h = tex_pixel_size.y;
        if (tex_w <= 0.0f || tex_h <= 0.0f)
            return;

        // 2. 准备绘制状态
        SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);
        SDL_GPUTextureSamplerBinding binding = {gpu_tex, sampler};
        SDL_BindGPUFragmentSamplers(_active_pass, 0, &binding, 1);

        // 3. 计算 UV 矩形 (核心：必须要传给常量缓冲)
        engine::utils::FRect src;
        auto src_opt = sprite.getSourceRect();
        if (src_opt.has_value())
        {
            src = *src_opt;
        }
        else
        {
            src.position = glm::vec2(0.0f, 0.0f);
            src.size = glm::vec2(tex_w, tex_h);
        }

        // 防缝隙微调
        const float eps = 0.005f;
        glm::vec4 uv_rect = glm::vec4(
            (src.position.x + eps) / tex_w,
            (src.position.y + eps) / tex_h,
            (src.size.x - eps * 2.0f) / tex_w,
            (src.size.y - eps * 2.0f) / tex_h);

        // 4. 计算视口与平铺逻辑
        glm::vec2 sprite_size = sprite.getSize() * scale;
        glm::vec2 viewport_size = camera.getViewportSize();

        // 计算屏幕参考位置
        glm::vec2 position_screen = camera.worldToScreenWithParallax(position, scroll_factor);

        glm::vec2 start, stop;
        if (repeat.x)
        {
            start.x = glm::mod(position_screen.x, sprite_size.x) - sprite_size.x;
            stop.x = viewport_size.x + sprite_size.x;
        }
        else
        {
            start.x = position_screen.x;
            stop.x = position_screen.x + sprite_size.x;
        }

        if (repeat.y)
        {
            start.y = glm::mod(position_screen.y, sprite_size.y) - sprite_size.y;
            stop.y = viewport_size.y + sprite_size.y;
        }
        else
        {
            start.y = position_screen.y;
            stop.y = position_screen.y + sprite_size.y;
        }

        // 5. 循环绘制
        for (float x = start.x; x < stop.x; x += sprite_size.x)
        {
            for (float y = start.y; y < stop.y; y += sprite_size.y)
            {
                // 像素对齐
                glm::vec2 draw_pos = glm::floor(glm::vec2(x, y));

                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(draw_pos, 0.0f));
                if (angle != 0.0)
                {
                    model = glm::translate(model, glm::vec3(0.5f * sprite_size.x, 0.5f * sprite_size.y, 0.0f));
                    model = glm::rotate(model, glm::radians((float)angle), glm::vec3(0.0f, 0.0f, 1.0f));
                    model = glm::translate(model, glm::vec3(-0.5f * sprite_size.x, -0.5f * sprite_size.y, 0.0f));
                }
                model = glm::scale(model, glm::vec3(sprite_size, 1.0f));

                SpritePushConstants constants;
                constants.mvp = camera.getProjectionMatrix() * model;
                constants.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                constants.uv_rect = uv_rect; // ⚡️ 修正：必须把计算好的 UV 传给 Shader

                SDL_PushGPUVertexUniformData(_current_cmd, 0, &constants, sizeof(constants));
                SDL_DrawGPUPrimitives(_active_pass, 6, 1, 0, 0);
            }
        }
    }

    glm::vec2 SDL3GPURenderer::windowToLogical(float window_x, float window_y) const
    {
        int win_w, win_h;
        SDL_GetWindowSize(_window, &win_w, &win_h);

        float scale = std::min((float)win_w / _logical_w, (float)win_h / _logical_h);
        float offset_x = (win_w - _logical_w * scale) * 0.5f;
        float offset_y = (win_h - _logical_h * scale) * 0.5f;

        return {(window_x - offset_x) / scale, (window_y - offset_y) / scale};
    }

    void SDL3GPURenderer::clean()
    {
    }

    void SDL3GPURenderer::present()
    {
        if (_active_pass)
        {
            SDL_EndGPURenderPass(_active_pass);
            _active_pass = nullptr;
        }
        if (_current_cmd)
        {
            SDL_SubmitGPUCommandBuffer(_current_cmd);
            _current_cmd = nullptr;
            _current_swapchain_texture = nullptr;
        }
    }
} // namespace engine