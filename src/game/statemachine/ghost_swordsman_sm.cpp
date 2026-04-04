/**
 * ghost_swordsman_sm.cpp  —  鬼剑士状态机专属逻辑
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  状态图（只列主要转移，完整配置见 GhostSwordsman.sm.json）
 * ──────────────────────────────────────────────────────────────────────────
 *
 *         ┌─── IS_MOVING ───→ MOVE
 *         │                     │
 *       IDLE ←── NO_INPUT ──────┘
 *         │
 *         └── KEY_ATTACK ──→ ATTACK_1
 *                               │ ANIM_END → IDLE
 *                               │ KEY_ATTACK (ComboWindow) → ATTACK_2
 *                                    │ ANIM_END → IDLE
 *                                    │ KEY_ATTACK (ComboWindow) → ATTACK_3
 *                                         │ ANIM_END → IDLE
 *                                         │ KEY_ATTACK (ComboWindow) → ATTACK_4
 *                                              │ ANIM_END → IDLE
 *
 *  MOVE  ──── KEY_ATTACK ──→ ATTACK_1  （移动中也可起手攻击）
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  帧事件规划（对应 .sm.json 中各状态的 frame_events）
 * ──────────────────────────────────────────────────────────────────────────
 *
 *  ATTACK_1: frame 2 → "spawn_hitbox:1"  "play_sound:ghost_attack1"
 *  ATTACK_2: frame 2 → "spawn_hitbox:2"  "play_sound:ghost_attack2"
 *  ATTACK_3: frame 3 → "spawn_hitbox:3"  "play_sound:ghost_attack3"
 *            frame 4 → "spawn_vfx:slash"
 *  ATTACK_4: frame 4 → "spawn_hitbox:4"  "play_sound:ghost_attack4"
 *            frame 5 → "spawn_vfx:slash"
 *            frame 6 → "shake_screen"
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  连击说明
 * ──────────────────────────────────────────────────────────────────────────
 *
 *  连击窗口（ComboWindow）由 .sm.json 中 StateNode.windows 字段配置：
 *    例：ATTACK_1 的 windows = [{start:3, end:5, type:ComboWindow}]
 *    在这 3 帧内，玩家按攻击键会被 InputBuffer 记录（缓冲 0.2s）。
 *    StateController 在窗口期检测到 KEY_ATTACK 缓冲 → 触发 ATTACK_2 转移。
 *    不需要在本文件里手动管理连击计数。
 *
 * ──────────────────────────────────────────────────────────────────────────
 *  朝向翻转
 * ──────────────────────────────────────────────────────────────────────────
 *
 *  移动朝向由 tickPlayerSM()（game_scene.cpp）统一处理：
 *    - 检测到 moveL → controller->setFacingDirection(Left) + sprite->setFlipped(true)
 *    - 检测到 moveR → controller->setFacingDirection(Right) + sprite->setFlipped(false)
 *    - 在 ATTACK 期间不翻转，避免出手瞬间换向
 *  本文件无需重复处理。
 */

#include "character_sm_setup.h"
#include "../../engine/component/controller_component.h"
#include "../../engine/component/physics_component.h"
#include "../../engine/component/sprite_component.h"
#include <spdlog/spdlog.h>
#include <string>

namespace game::statemachine {

// ─────────────────────────────────────────────────────────────────────────────
static void setupGhostSwordsmanSM(
    engine::statemachine::StateController& sm,
    engine::object::GameObject*            actor,
    engine::core::Context&                 /*context*/)
{
    using namespace engine::statemachine;
    using CC = engine::component::ControllerComponent;

    // ── 状态切换回调 ──────────────────────────────────────────────────────────
    //
    //  职责：
    //    - 进入 ATTACK_*：锁定控制器（禁止 WASD 物理驱动），
    //      此时位移完全由 root_motion 驱动（向前冲）。
    //    - 离开 ATTACK_*：恢复控制器，角色可正常移动。
    //    - 无需在此处更新精灵朝向（tickPlayerSM 已在 moveL/moveR 时处理）。
    //
    sm.setOnStateChanged([actor](const std::string& from, const std::string& to)
    {
        const bool enterAttack = (to.rfind("ATTACK", 0) == 0);
        const bool leaveAttack = (from.rfind("ATTACK", 0) == 0);

        if (auto* ctrl = actor ? actor->getComponent<CC>() : nullptr)
        {
            if (enterAttack && !leaveAttack)
                ctrl->setEnabled(false);   // 进入攻击：锁定 WASD
            else if (leaveAttack && !enterAttack)
                ctrl->setEnabled(true);    // 离开攻击：恢复移动
        }

        spdlog::info("[GhostSwordsman SM] {} → {}", from.empty() ? "(init)" : from, to);
    });

    // ── 帧事件回调 ────────────────────────────────────────────────────────────
    //
    //  "spawn_hitbox:N"  连击第 N 段有效帧，生成近战判定盒。
    //    - hitboxW 随段数递增（越后期出手范围越大）
    //    - actorPos + facingSign * offset 确定判定盒中心
    //    - TODO: 接入战斗系统后替换 spdlog::info 为 combatSys->meleeHit(...)
    //
    //  "play_sound:xxx"  对应攻击音效（路径 assets/audio/xxx.mp3）。
    //    - tickPlayerSM() 统一处理音频播放，此处仅记录诊断日志。
    //
    //  "spawn_vfx:slash" 斩击特效（第 3、4 段）。
    //    - TODO: vfxSys->emit("slash", pos, facingSign);
    //
    //  "shake_screen"    收尾技屏幕震动（第 4 段，帧 6）。
    //    - TODO: cameraSys->shake(0.25f, 6.0f);
    //
    sm.setOnFrameEvent([actor](const std::string& event, int frame)
    {
        (void)frame;

        // ── spawn_hitbox:N ────────────────────────────────────────────────
        if (event.rfind("spawn_hitbox:", 0) == 0)
        {
            const int   comboIdx = std::stoi(event.substr(13));
            const float hitboxW  = 40.0f + static_cast<float>(comboIdx) * 4.0f;
            const float hitboxH  = 36.0f;

            float facingSign = 1.0f;
            if (auto* ctrl = actor ? actor->getComponent<engine::component::ControllerComponent>() : nullptr)
                facingSign = (ctrl->getFacingDirection() == CC::FacingDirection::Left) ? -1.0f : 1.0f;

            glm::vec2 actorPos{0.0f, 0.0f};
            if (auto* phys = actor ? actor->getComponent<engine::component::PhysicsComponent>() : nullptr)
                actorPos = phys->getPosition();

            // TODO: 接入战斗系统 →
            //   combatSys->createMeleeHitbox(actorPos + glm::vec2(hitboxW * 0.5f * facingSign, 0),
            //                                hitboxW, hitboxH, comboIdx);
            spdlog::info("[GhostSwordsman SM] spawn_hitbox:{} @ ({:.0f},{:.0f}) "
                         "facing={:.0f} w={:.0f} h={:.0f}",
                         comboIdx, actorPos.x, actorPos.y, facingSign, hitboxW, hitboxH);
        }

        // ── play_sound:xxx ────────────────────────────────────────────────
        // tickPlayerSM 统一处理音频播放，此处仅记录诊断日志
        if (event.rfind("play_sound:", 0) == 0)
            spdlog::debug("[GhostSwordsman SM] 音效: {}", event.substr(11));

        // ── spawn_vfx:slash ───────────────────────────────────────────────
        if (event == "spawn_vfx:slash")
        {
            // TODO: vfxSys->emit("slash", actorPos, facingSign);
        }

        // ── shake_screen ──────────────────────────────────────────────────
        if (event == "shake_screen")
        {
            // TODO: cameraSys->shake(0.25f, 6.0f);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  派发入口：被 game_scene.cpp loadPlayerSM() 调用
// ─────────────────────────────────────────────────────────────────────────────
bool setupCharacterSM(
    const std::string&                     characterId,
    engine::statemachine::StateController& sm,
    engine::object::GameObject*            actor,
    engine::core::Context&                 context)
{
    if (characterId == "GhostSwordsman")
    {
        setupGhostSwordsmanSM(sm, actor, context);
        return true;
    }
    // 未找到专属设置 → 调用方回退到通用默认处理（ATTACK 自动锁定）
    return false;
}

} // namespace game::statemachine
