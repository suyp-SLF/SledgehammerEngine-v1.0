# 标题
cmake_minimum_required(VERSION 3.10.0)
project(LearnSunnyLand VERSION 0.1.0 LANGUAGES C CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置编译选项
if (MSVC)
    add_compile_options(/W4)
elseif(APPLE)
    # 告诉编译器注入内存监控代码
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    # 告诉链接器链接内存监控库
    add_link_options(-fsanitize=address)
    message(STATUS "Mac Address Sanitizer (ASan) 已激活")
    add_compile_options(-Wall -Wextra -Wpedantic)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_BUILD_TYPE Debug)

# 设置编译输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR})

set(TARGET ${PROJECT_NAME}-${CMAKE_SYSTEM_NAME})

# --- 关键：强制只查找你自定义的路径 ---
set(MY_SDL_BASE "/Users/suyp/C++/Library/build")
set(CMAKE_PREFIX_PATH "${MY_SDL_BASE}")
# 告诉 CMake 不要去系统默认路径（如 Homebrew）乱找
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 查找并载入Cmake预设
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_mixer REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# 添加可执行文件
add_executable(
    ${TARGET} 
    src/main.cpp

    src/engine/core/game_app.cpp
    src/engine/core/time.cpp
    src/engine/core/config.cpp
    src/engine/core/context.cpp

    src/engine/resource/resource_manager.cpp
    src/engine/resource/texture_manager.cpp
    src/engine/resource/audio_manager.cpp
    src/engine/resource/font_manager.cpp
    src/engine/resource/resource_types.h
    src/engine/resource/shader_manager.cpp

    src/engine/render/sprite.h
    src/engine/render/render_types.h
    src/engine/render/renderer.h

    src/engine/render/camera.cpp
    src/engine/render/sdl_renderer.cpp
    src/engine/render/vulkan_renderer.cpp
    src/engine/render/sdl3_gpu_renderer.cpp
    
    src/engine/render/sprite_render_system.cpp
    src/engine/render/parallax_render_system.cpp
    src/engine/render/tilelayer_render_system.cpp

    src/engine/component/sprite_component.cpp
    src/engine/component/transform_component.cpp
    src/engine/component/parallax_component.cpp
    src/engine/component/tilelayer_component.cpp

    src/engine/world/tile_info.h
    src/engine/world/chunk.h
    src/engine/world/perlin_noise_generator.cpp
    src/engine/world/chunk_manager.cpp
    src/engine/world/world_config.cpp
    src/engine/world/terrain_generator.cpp

    src/engine/scene/scene.cpp
    src/engine/scene/scene_manager.cpp
    src/engine/scene/level_loader.cpp

    src/engine/object/game_object.cpp

    src/engine/input/input_manager.cpp

    src/engine/utils/math.h

    src/game/scene/game_scene.cpp
    )

# 这样编译器才能进入 /Users/suyp/C++/Library/build/include 找到 SDL3_mixer 文件夹
target_include_directories(${TARGET} PRIVATE "${MY_SDL_BASE}/include")

# 链接库
target_link_libraries(
    ${TARGET}
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    )