#include "sdl3_gpu_renderer.h"
#include "render_types.h"
#include "sprite.h"
#include "../core/context.h"
#include "../resource/resource_manager.h"
#include "../component/tilelayer_component.h"
#include "camera.h"
#include <spdlog/spdlog.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <map>

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
        SDL_ClaimWindowForGPUDevice(_device, _window);

        // --- 1. 创建通用的单位矩形顶点缓冲 (一次性创建) ---
        GPUVertex unit_quad_data[] = {
            {{0.0f, 0.0f}, {0.0f, 0.0f}}, {{1.0f, 0.0f}, {1.0f, 0.0f}}, {{0.0f, 1.0f}, {0.0f, 1.0f}}, {{0.0f, 1.0f}, {0.0f, 1.0f}}, {{1.0f, 0.0f}, {1.0f, 0.0f}}, {{1.0f, 1.0f}, {1.0f, 1.0f}}};

        SDL_GPUBufferCreateInfo unit_info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(unit_quad_data)};
        _unit_quad_buffer = SDL_CreateGPUBuffer(_device, &unit_info);

        // 上传数据 (使用简单的立即提交模式)
        SDL_GPUTransferBufferCreateInfo tb_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sizeof(unit_quad_data)};
        SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(_device, &tb_info);
        void *map = SDL_MapGPUTransferBuffer(_device, tb, false);
        std::memcpy(map, unit_quad_data, sizeof(unit_quad_data));
        SDL_UnmapGPUTransferBuffer(_device, tb);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(_device);
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src_loc = {tb, 0};
        SDL_GPUBufferRegion dst_reg = {_unit_quad_buffer, 0, sizeof(unit_quad_data)};
        SDL_UploadToGPUBuffer(copy, &src_loc, &dst_reg, false);
        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(_device, tb);

        // --- 2. 创建瓦片地图动态缓冲 ---
        SDL_GPUBufferCreateInfo tile_info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(GPUVertex) * 6 * 4000};
        _tile_vertex_buffer = SDL_CreateGPUBuffer(_device, &tile_info);

        // --- 3. 创建采样器 ---
        SDL_GPUSamplerCreateInfo sampler_info = {.min_filter = SDL_GPU_FILTER_NEAREST, .mag_filter = SDL_GPU_FILTER_NEAREST};
        _default_sampler = SDL_CreateGPUSampler(_device, &sampler_info);
    }

    void SDL3GPURenderer::createPipeline()
    {
        if (!_res_mgr || !_device || !_window)
            return;

        SDL_GPUShader *v_shader = _res_mgr->loadShader("sprite_vert", "assets/shaders/sprite.vert", 0, 1);
        SDL_GPUShader *f_shader = _res_mgr->loadShader("sprite_frag", "assets/shaders/sprite.frag", 1, 0);
        if (!v_shader || !f_shader)
            return;

        // 顶点输入配置
        SDL_GPUVertexAttribute attributes[2];
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0};                 // a_pos
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, sizeof(glm::vec2)}; // a_uv

        SDL_GPUVertexBufferDescription buffer_desc = {};
        buffer_desc.slot = 0;
        buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX; // 使用枚举常量
        buffer_desc.instance_step_rate = 0;
        buffer_desc.pitch = sizeof(GPUVertex);
        // 混合状态
        SDL_GPUColorTargetDescription color_desc = {};
        color_desc.format = SDL_GetGPUSwapchainTextureFormat(_device, _window);
        color_desc.blend_state.enable_blend = true;
        color_desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color_desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_desc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_desc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        color_desc.blend_state.color_write_mask = 0xF;

        SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
        pipeline_info.vertex_shader = v_shader;
        pipeline_info.fragment_shader = f_shader;
        pipeline_info.vertex_input_state.num_vertex_attributes = 2;
        pipeline_info.vertex_input_state.vertex_attributes = attributes;
        pipeline_info.vertex_input_state.num_vertex_buffers = 1;
        pipeline_info.vertex_input_state.vertex_buffer_descriptions = &buffer_desc;
        pipeline_info.target_info.num_color_targets = 1;
        pipeline_info.target_info.color_target_descriptions = &color_desc;
        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        if (_sprite_pipeline)
            SDL_ReleaseGPUGraphicsPipeline(_device, _sprite_pipeline);
        _sprite_pipeline = SDL_CreateGPUGraphicsPipeline(_device, &pipeline_info);
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
    void SDL3GPURenderer::drawSprite(const Camera &camera,
                                     const Sprite &sprite,
                                     const glm::vec2 &position,
                                     const glm::vec2 &scale,
                                     double angle,
                                     const glm::vec4 &uv_rect)
    {
        if (!_active_pass || !_sprite_pipeline || !_res_mgr)
            return;

        SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(sprite.getTextureId());
        SDL_GPUSampler *sampler = _res_mgr->getDefaultSampler();
        if (!gpu_tex || !sampler)
            return;

        glm::vec2 tex_total_size = _res_mgr->getTextureSize(sprite.getTextureId());
        if (tex_total_size.x <= 0.0f)
            return;

        // 1. 状态绑定
        SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);
        SDL_GPUBufferBinding v_binding = {_unit_quad_buffer, 0};
        SDL_BindGPUVertexBuffers(_active_pass, 0, &v_binding, 1);

        // 2. ⚡️ 修正尺寸逻辑：优先使用 Sprite 自身的尺寸（来自 Object Layer 的 width/height）
        // 如果 sprite 没有设置 size，再退而求其次用纹理全尺寸
        glm::vec2 base_size = sprite.getSize();
        if (base_size.x <= 0 || base_size.y <= 0)
        {
            base_size = tex_total_size;
        }
        glm::vec2 logical_size = base_size * scale;

        // 3. 变换矩阵计算
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(glm::floor(position), 0.0f));
        if (angle != 0.0)
        {
            glm::vec2 center = logical_size * 0.5f;
            model = glm::translate(model, glm::vec3(center, 0.0f));
            model = glm::rotate(model, glm::radians((float)angle), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::translate(model, glm::vec3(-center, 0.0f));
        }
        model = glm::scale(model, glm::vec3(logical_size, 1.0f));

        SpritePushConstants constants;
        constants.mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * model;
        constants.color = glm::vec4(1.0f);

        // 4. ⚡️ 修正 UV 逻辑
        // 从 Sprite 中获取像素单位的裁剪区域
        engine::utils::FRect src = sprite.getSourceRect().value_or(
            engine::utils::FRect{{0, 0}, tex_total_size});

        // 将像素单位转换为 0.0~1.0 的归一化坐标
        float u = src.position.x / tex_total_size.x;
        float v = src.position.y / tex_total_size.y;
        float uw = src.size.x / tex_total_size.x;
        float vh = src.size.y / tex_total_size.y;

        if (sprite.isFlipped())
        {
            u = u + uw;
            uw = -uw;
        }
        constants.uv_rect = glm::vec4(u, v, uw, vh);

        // 5. 提交绘制
        SDL_GPUTextureSamplerBinding tex_binding = {gpu_tex, sampler};
        SDL_BindGPUFragmentSamplers(_active_pass, 0, &tex_binding, 1);
        SDL_PushGPUVertexUniformData(_current_cmd, 0, &constants, sizeof(constants));
        SDL_DrawGPUPrimitives(_active_pass, 6, 1, 0, 0);
    }

    void SDL3GPURenderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
    }

    void SDL3GPURenderer::drawParallax(const Camera &camera, const Sprite &sprite, const glm::vec2 &position, const glm::vec2 &scroll_factor, const glm::bvec2 &repeat, const glm::vec2 &scale, double angle)
    {
        if (!_active_pass || !_sprite_pipeline || !_res_mgr)
            return;

        SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(sprite.getTextureId());
        SDL_GPUSampler *sampler = _res_mgr->getDefaultSampler();
        if (!gpu_tex || !sampler)
            return;

        glm::vec2 tex_pixel_size = _res_mgr->getTextureSize(sprite.getTextureId());
        if (tex_pixel_size.x <= 0.0f || tex_pixel_size.y <= 0.0f)
            return;

        // --- 1. 核心修复：绑定状态 ---
        SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);

        // ⚡️ 必须绑定顶点缓冲，否则 Draw 调用无效
        SDL_GPUBufferBinding v_binding = {_unit_quad_buffer, 0};
        SDL_BindGPUVertexBuffers(_active_pass, 0, &v_binding, 1);

        SDL_GPUTextureSamplerBinding tex_binding = {gpu_tex, sampler};
        SDL_BindGPUFragmentSamplers(_active_pass, 0, &tex_binding, 1);

        // --- 2. UV 计算 ---
        engine::utils::FRect src;
        auto src_opt = sprite.getSourceRect();
        src = src_opt.has_value() ? *src_opt : engine::utils::FRect{{0, 0}, tex_pixel_size};

        glm::vec4 uv_rect = glm::vec4(src.position.x / tex_pixel_size.x, src.position.y / tex_pixel_size.y,
                                      src.size.x / tex_pixel_size.x, src.size.y / tex_pixel_size.y);

        // --- 3. 视差逻辑 ---
        glm::vec2 sprite_size = sprite.getSize() * scale;
        glm::vec2 viewport_size = camera.getViewportSize();
        glm::vec2 position_screen = camera.worldToScreenWithParallax(position, scroll_factor);

        glm::vec2 start, stop;
        start.x = repeat.x ? glm::mod(position_screen.x, sprite_size.x) - sprite_size.x : position_screen.x;
        stop.x = repeat.x ? viewport_size.x + sprite_size.x : position_screen.x + sprite_size.x;
        start.y = repeat.y ? glm::mod(position_screen.y, sprite_size.y) - sprite_size.y : position_screen.y;
        stop.y = repeat.y ? viewport_size.y + sprite_size.y : position_screen.y + sprite_size.y;

        // --- 4. 循环绘制 ---
        for (float x = start.x; x < stop.x; x += sprite_size.x)
        {
            for (float y = start.y; y < stop.y; y += sprite_size.y)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(glm::floor(glm::vec2(x, y)), 0.0f));
                model = glm::scale(model, glm::vec3(sprite_size, 1.0f));

                SpritePushConstants pc;
                pc.mvp = camera.getProjectionMatrix() * model; // 背景不随 View 矩阵移动
                pc.color = glm::vec4(1.0f);
                pc.uv_rect = uv_rect;

                SDL_PushGPUVertexUniformData(_current_cmd, 0, &pc, sizeof(pc));
                SDL_DrawGPUPrimitives(_active_pass, 6, 1, 0, 0);
            }
        }
    }

    void SDL3GPURenderer::drawTileMap(const Camera &camera,
                                      const glm::ivec2 &map_size,
                                      const glm::vec2 &tile_size,
                                      const std::vector<engine::component::TileData> &tiles,
                                      const glm::vec2 &layer_offset)
    {
        if (!_current_cmd || tiles.empty() || !_res_mgr)
            return;

        // --- 1. 计算裁剪区域 ---
        glm::vec2 cam_pos = camera.getPosition();
        glm::vec2 view_size = camera.getViewportSize();

        int start_col = std::max(0, (int)std::floor((cam_pos.x - layer_offset.x) / tile_size.x));
        int end_col = std::min(map_size.x, (int)std::ceil((cam_pos.x + view_size.x - layer_offset.x) / tile_size.x));
        int start_row = std::max(0, (int)std::floor((cam_pos.y - layer_offset.y) / tile_size.y));
        int end_row = std::min(map_size.y, (int)std::ceil((cam_pos.y + view_size.y - layer_offset.y) / tile_size.y));

        // --- 2. 顶点分类收集 (Batching) ---
        // 使用 std::map 存储：纹理对象 -> 该纹理对应的所有顶点
        static std::map<SDL_GPUTexture *, std::vector<GPUVertex>> batch_map;
        batch_map.clear();
        size_t total_vertex_count = 0;

        for (int y = start_row; y < end_row; ++y)
        {
            for (int x = start_col; x < end_col; ++x)
            {
                const auto &tile = tiles[y * map_size.x + x];
                if (tile.uv_rect.z <= 0.0f)
                    continue; // 跳过空瓦片

                // 获取纹理指针（从缓存获取）
                SDL_GPUTexture *gpu_tex = _res_mgr->getGPUTexture(tile.texture_id);
                if (!gpu_tex)
                    continue;

                // 计算屏幕坐标
                glm::vec2 screen_pos = camera.worldToScreen(glm::vec2(x, y) * tile_size + layer_offset);
                float L = std::floor(screen_pos.x);
                float T = std::floor(screen_pos.y);
                float R = L + tile_size.x;
                float B = T + tile_size.y;

                float u1 = tile.uv_rect.x, v1 = tile.uv_rect.y;
                float u2 = u1 + tile.uv_rect.z, v2 = v1 + tile.uv_rect.w;

                // 存入对应的 Batch
                auto &v_list = batch_map[gpu_tex];
                v_list.push_back({{L, T}, {u1, v1}});
                v_list.push_back({{R, T}, {u2, v1}});
                v_list.push_back({{L, B}, {u1, v2}});
                v_list.push_back({{L, B}, {u1, v2}});
                v_list.push_back({{R, T}, {u2, v1}});
                v_list.push_back({{R, B}, {u2, v2}});

                total_vertex_count += 6;
            }
        }

        if (total_vertex_count == 0)
            return;

        // --- 3. 统一上传顶点数据 ---
        // 结束当前的 Render Pass (SDL3 规定 Copy 操作不能在 Render Pass 中)
        if (_active_pass)
        {
            SDL_EndGPURenderPass(_active_pass);
            _active_pass = nullptr;
        }

        // 创建或使用预分配的 Transfer Buffer
        uint32_t total_data_size = (uint32_t)(total_vertex_count * sizeof(GPUVertex));
        SDL_GPUTransferBufferCreateInfo tb_info = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total_data_size};
        SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(_device, &tb_info);

        GPUVertex *map = (GPUVertex *)SDL_MapGPUTransferBuffer(_device, tb, false);

        // 依次填充数据并记录每个纹理在 Buffer 中的偏移量
        struct DrawCall
        {
            SDL_GPUTexture *tex;
            uint32_t start_vertex;
            uint32_t count;
        };
        std::vector<DrawCall> draw_calls;
        uint32_t current_offset = 0;

        for (auto &[tex, v_list] : batch_map)
        {
            uint32_t count = (uint32_t)v_list.size();
            std::memcpy(map + current_offset, v_list.data(), count * sizeof(GPUVertex));
            draw_calls.push_back({tex, current_offset, count});
            current_offset += count;
        }
        SDL_UnmapGPUTransferBuffer(_device, tb);

        // 执行 Copy 操作到 GPU 显存 Buffer
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(_current_cmd);
        SDL_GPUTransferBufferLocation src_loc = {tb, 0};
        SDL_GPUBufferRegion dst_reg = {_tile_vertex_buffer, 0, total_data_size};
        SDL_UploadToGPUBuffer(copy, &src_loc, &dst_reg, false);
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(_device, tb);

        // --- 4. 分批绘制 (Re-open Render Pass) ---
        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = _current_swapchain_texture;
        color_target.load_op = SDL_GPU_LOADOP_LOAD; // 重要：保留之前的内容
        color_target.store_op = SDL_GPU_STOREOP_STORE;
        _active_pass = SDL_BeginGPURenderPass(_current_cmd, &color_target, 1, nullptr);

        // 设置全局状态
        SDL_BindGPUGraphicsPipeline(_active_pass, _sprite_pipeline);
        SDL_GPUBufferBinding v_binding = {_tile_vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(_active_pass, 0, &v_binding, 1);

        SpritePushConstants pc;
        pc.mvp = camera.getProjectionMatrix();
        pc.color = glm::vec4(1.0f);
        pc.uv_rect = glm::vec4(0, 0, 1, 1); // UV 已包含在顶点属性中

        for (const auto &call : draw_calls)
        {
            // 绑定该批次的纹理
            SDL_GPUTextureSamplerBinding tex_binding = {call.tex, _default_sampler};
            SDL_BindGPUFragmentSamplers(_active_pass, 0, &tex_binding, 1);

            // 推送常量并绘制对应范围的顶点
            SDL_PushGPUVertexUniformData(_current_cmd, 0, &pc, sizeof(pc));
            SDL_DrawGPUPrimitives(_active_pass, call.count, 1, call.start_vertex, 0);
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