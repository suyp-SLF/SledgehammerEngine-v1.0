/**
 * sm_loader.h  —  兼容转发头
 *
 * SmLoader 已移入 engine::statemachine。
 * 此头文件保留以兼容旧的 #include 路径，并通过 using 在
 * game::statemachine 命名空间内公开 SmLoader。
 */
#pragma once
#include "../../engine/statemachine/sm_loader.h"

namespace game::statemachine {
    using engine::statemachine::SmLoader;
} // namespace game::statemachine
