// =============================================================================
//  ghost_swordsman_example.cpp
//
//  【参考文档】如何为一个新角色接入数据驱动状态机系统
//
//  本文件 *不参与编译*（CMakeLists.txt 中未添加此文件）；
//  直接阅读即可，无需运行。
//
//  ─── 目录 ───────────────────────────────────────────────────────────────
//  §1. 整体架构概述
//  §2. 文件职责分工
//  §3. 如何新增一个角色的 SM 接入（以"骷髅弓手"为例）
//  §4. tickPlayerSM 工作流（每帧发生了什么）
//  §5. 连击机制详解（ComboWindow 如何工作）
//  §6. 朝向翻转机制
//  §7. 帧事件事件表
//  §8. 测试与调试技巧
// =============================================================================


// ─── §1. 整体架构概述 ────────────────────────────────────────────────────────
//
//  引擎层（engine/statemachine/）：纯数据，无游戏逻辑
//    sm_types.h         —— POD 数据结构：StateMachineData / StateNode / Transition …
//    sm_loader.h/cpp    —— JSON 序列化/反序列化（*.sm.json）
//    input_buffer.h/cpp —— 带时间戳的按键缓冲池（支持 0.2s 预输入）
//    state_controller.h/cpp —— 每帧决策:当前状态→下一状态，触发帧事件
//
//  游戏层（game/statemachine/）：游戏逻辑
//    character_sm_setup.h —— setupCharacterSM() 接口声明（各角色共用）
//    ghost_swordsman_sm.cpp —— 鬼剑士专属：setOnStateChanged + setOnFrameEvent
//    skeleton_archer_sm.cpp —— (示例) 骷髅弓手专属逻辑
//    ghost_swordsman_example.cpp —— 本文件，仅作文档用途
//
//  场景层（game/scene/game_scene.cpp）
//    loadPlayerSM()  —— 读取 .sm.json → 调用 setupCharacterSM 注册回调
//    tickPlayerSM()  —— 每帧：构建 activeInputs → sm.update() → 应用结果
//


// ─── §2. 文件职责分工 ────────────────────────────────────────────────────────
//
//  ┌─────────────────┬────────────────────────────────────────────────────┐
//  │ 文件              │ 职责                                               │
//  ├─────────────────┼────────────────────────────────────────────────────┤
//  │ *.sm.json        │ 状态转移条件、帧区间、帧事件 —— 策划可直接编辑    │
//  │ sm_loader.cpp    │ 读/写 .sm.json                                     │
//  │ state_controller │ 按 JSON 配置做转移决策，触发帧事件回调             │
//  │ xxx_sm.cpp       │ 注册回调：什么时候锁移动、打音效、生成判定盒       │
//  │ game_scene.cpp   │ 每帧驱动 SM，翻转精灵，从 result 读取根位移        │
//  └─────────────────┴────────────────────────────────────────────────────┘


// ─── §3. 如何新增角色 SM（以"骷髅弓手 SkeletonArcher"为例）─────────────────
//
//  步骤 A：创建 skeleton_archer_sm.cpp
//  ────────────────────────────────────────────────────────────────────────
//
//  #include "character_sm_setup.h"
//  #include "../../engine/component/controller_component.h"
//  #include "../../engine/component/physics_component.h"
//
//  namespace game::statemachine {
//
//  static void setupSkeletonArcherSM(StateController& sm, GameObject* actor, Context& ctx)
//  {
//      sm.setOnStateChanged([actor](const std::string& from, const std::string& to) {
//          // 进入 SHOOT：播放蓄力音效，锁定横向移动
//          // 离开 SHOOT：停止音效，恢复移动
//      });
//
//      sm.setOnFrameEvent([actor](const std::string& event, int frame) {
//          if (event == "spawn_arrow")
//              ; // TODO: projectileSys->spawn("arrow", pos, dir);
//          if (event.rfind("play_sound:", 0) == 0)
//              ; // tickPlayerSM 统一播放，此处可补充日志
//      });
//  }
//
//  // 在 setupCharacterSM() 的 if/else 链中添加分支：
//  bool setupCharacterSM(const std::string& id, StateController& sm,
//                        GameObject* actor, Context& ctx)
//  {
//      if (id == "GhostSwordsman")  { setupGhostSwordsmanSM(sm, actor, ctx); return true; }
//      if (id == "SkeletonArcher")  { setupSkeletonArcherSM(sm, actor, ctx); return true; }
//      return false;
//  }
//
//  步骤 B：在 CMakeLists.txt 中添加编译单元
//  ────────────────────────────────────────────────────────────────────────
//
//  src/game/statemachine/ghost_swordsman_sm.cpp
//  src/game/statemachine/skeleton_archer_sm.cpp   ← 新增这一行
//
//  步骤 C：创建 SkeletonArcher.sm.json
//  ────────────────────────────────────────────────────────────────────────
//
//  {
//    "character_id": "SkeletonArcher",
//    "initial_state": "IDLE",
//    "states": {
//      "IDLE":  { "animation_id": "idle",    "loop": true,  "transitions": [
//                   {"trigger": "IS_MOVING",  "target": "WALK",  "priority": 1},
//                   {"trigger": "KEY_ATTACK", "target": "SHOOT", "priority": 2}
//               ]},
//      "WALK":  { "animation_id": "walk",    "loop": true,  "transitions": [...] },
//      "SHOOT": { "animation_id": "shoot",   "loop": false, "total_frames": 8,
//                 "transitions": [{"trigger": "ANIM_END", "target": "IDLE", "priority": 1}],
//                 "frame_events": [
//                   {"frame": 5, "event": "spawn_arrow"},
//                   {"frame": 2, "event": "play_sound:arrow_draw"}
//                 ]}
//    }
//  }
//
//  步骤 D：在帧编辑器里为骷髅弓手创建对应动作
//    idle / walk / shoot  （animation_id 须与 JSON 一致）
//
//  步骤 E：在角色编辑器里填写
//    smPath = "assets/textures/Actors/SkeletonArcher.sm.json"
//
//  完成后，通过实体管理面板生成SkeletonArcher，勾选"设为控制对象"即可测试。


// ─── §4. tickPlayerSM 每帧工作流 ─────────────────────────────────────────────
//
//  ① 查询物理状态（grounded/land/vel）
//  ② 构建 activeInputs：
//       持续型（GROUNDED / AIRBORNE / IS_MOVING / KEY_MOVE_L / KEY_MOVE_R …）
//  ③ 朝向更新（moveL/moveR → setFacingDirection + sprite->setFlipped）
//     ⚠ ATTACK 期间跳过翻转，防止出手瞬间换向
//  ④ 瞬间型输入 → sm.pushInput("KEY_ATTACK", time)（带 200ms 缓冲）
//  ⑤ sm.update(dt, activeInputs, time) → UpdateResult
//  ⑥ 应用根位移（result.rootMotionDx * facingSign → physics->applyImpulse）
//  ⑦ 处理帧事件（result.firedEvents）
//     - play_sound:xxx → AudioMixer
//     - spawn_hitbox:N → TODO 战斗系统
//     - spawn_vfx/shake_screen → TODO 特效/摄像机
//  ⑧ 状态变化时同步动画组件（anim->play(stateNode.animationId)）


// ─── §5. 连击机制详解（ComboWindow）──────────────────────────────────────────
//
//  .sm.json 中 StateNode.windows 配置：
//    {"start": 4, "end": 7, "type": 1}   // type 1 = ComboWindow
//
//  运行时流程：
//    1. 玩家在 ATTACK_1 帧 2（Locked 区）按下攻击键
//       → sm.pushInput("KEY_ATTACK", time)  缓冲池记录时间戳 T
//    2. 动画播到帧 4，进入 ComboWindow
//    3. update() 检查 ATTACK_1 的 transitions：
//         {trigger: "KEY_ATTACK", requireWindow: true, windowType: ComboWindow}
//       → 检测到缓冲池有 KEY_ATTACK（时间戳 T 距今 < 200ms）✓
//       → 跳转到 ATTACK_2
//    4. 若 ComboWindow 结束前始终没有 KEY_ATTACK 缓冲：
//       → ANIM_END → IDLE（连招自然结束）
//
//  优先级：多条 Transition 同时满足时，priority 大者优先触发。
//  ATTACK_4（终结技）无 ComboWindow，播完强制 ANIM_END → IDLE。


// ─── §6. 朝向翻转机制 ─────────────────────────────────────────────────────────
//
//  tickPlayerSM 在构建 activeInputs 后（ATTACK 期间跳过）：
//
//    if (!inAttack) {
//        if (moveL) {
//            controller->setFacingDirection(Left);
//            sprite->setFlipped(true);
//        } else if (moveR) {
//            controller->setFacingDirection(Right);
//            sprite->setFlipped(false);
//        }
//    }
//
//  ghost_swordsman_sm.cpp 无需处理翻转逻辑。
//  帧事件里使用 controller->getFacingDirection() 获取当前朝向（用于判定盒偏移）。


// ─── §7. 帧事件约定表 ─────────────────────────────────────────────────────────
//
//  事件字符串格式：动词:参数  （冒号为分隔符）
//
//  ┌──────────────────────┬────────────────────────────────────────────────┐
//  │ 事件字符串             │ 含义 / 处理方                                  │
//  ├──────────────────────┼────────────────────────────────────────────────┤
//  │ play_sound:xxx        │ 播放 assets/audio/xxx.mp3 → tickPlayerSM 处理  │
//  │ spawn_hitbox:N        │ 近战判定盒，N=连招段 → xxx_sm.cpp TODO 战斗系统 │
//  │ spawn_vfx:slash       │ 斩击特效 → xxx_sm.cpp TODO VFX 系统             │
//  │ shake_screen          │ 屏幕震动 → xxx_sm.cpp TODO 摄像机系统           │
//  │ spawn_arrow           │ 生成投射物 → xxx_sm.cpp TODO 投射物系统         │
//  │ enable_hitbox         │ 开启常驻判定盒（适合多帧连续判定）              │
//  │ disable_hitbox        │ 关闭常驻判定盒                                  │
//  └──────────────────────┴────────────────────────────────────────────────┘
//
//  自定义新事件：在 .sm.json 中直接写新字符串，在 setOnFrameEvent 回调里增加分支即可。


// ─── §8. 测试与调试技巧 ──────────────────────────────────────────────────────
//
//  A. 生成指定角色并立即控制
//     →  实体管理面板（开发者工具）：输入角色 profile 路径，点"生成实体"，
//        勾选"设为控制对象"，自动加载 SM 并调用 setupCharacterSM。
//
//  B. 强制跳转到指定状态（调试某段攻击的帧事件）
//     →  在 game_scene.cpp 里临时添加：
//        m_playerSM.forceTransition("ATTACK_4");
//
//  C. 查看实时状态
//     →  开发者头顶显示（HUD）已实现：每帧显示当前状态名 + 动画 clip 名。
//
//  D. 查看状态转移日志
//     →  spdlog 级别需为 info 或以下；ghost_swordsman_sm.cpp 的 setOnStateChanged
//        会在每次转移时打印 "[GhostSwordsman SM] from → to"。
//
//  E. 模拟连击时序
//     →  在 sm.json 中临时将 ComboWindow 的 start 改为 0（整个动画都是连招窗口），
//        方便验证连击跳转逻辑，确认后再改回正常值。

