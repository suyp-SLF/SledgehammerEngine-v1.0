#pragma once
/**
 * character_sm_setup.h
 *
 * 角色状态机设置接口。
 *
 * 每个角色对应一个 xxx_sm.cpp 文件，实现自己的 setupSM 函数，并在
 * setupCharacterSM() 里按 characterId 派发。
 *
 * 调用时机：loadPlayerSM() 加载 JSON、init() 之后。
 */

#include "../../engine/statemachine/state_controller.h"
#include "../../engine/core/context.h"
#include "../../engine/object/game_object.h"
#include <string>

namespace game::statemachine {

/**
 * 按角色 ID 找到对应的 setupSM 函数并调用。
 * @param characterId  character.json 里的 "id" 字段，如 "GhostSwordsman"
 * @param sm           已 init() 完成的状态机控制器
 * @param actor        被控角色 GameObject（可从中取 Component）
 * @param context      引擎 Context（可取 InputManager、Time 等）
 * @return 是否找到了对应的设置函数（false 表示用通用默认处理）
 */
bool setupCharacterSM(
    const std::string&                     characterId,
    engine::statemachine::StateController& sm,
    engine::object::GameObject*            actor,
    engine::core::Context&                 context);

} // namespace game::statemachine
