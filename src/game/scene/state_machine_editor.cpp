#include "state_machine_editor.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

// ── 触发器预设 ─────────────────────────────────────────────────────────────
// IMPORTANT: nullptr 必须是最后一项，代码通过遇 nullptr 计算数量
static const char* kTriggers[] = {
    // 按键事件
    "KEY_ATTACK", "KEY_JUMP", "KEY_DASH",
    "KEY_MOVE_L", "KEY_MOVE_R",
    "KEY_BLOCK",  "KEY_SKILL_1", "KEY_SKILL_2", "KEY_SKILL_3",
    // 动画事件
    "ANIM_END",
    // 移动状态
    "NO_INPUT",   "IS_MOVING",  "IS_DASHING",  "IS_ATTACKING",
    // 物理/重力状态
    "GROUNDED",   "AIRBORNE",   "RISING",      "FALLING",
    // 瞬间事件
    "LAND",       "ON_WALL",
    // 自定义（必须始终保持为最后一个有效项）
    "CUSTOM",
    nullptr
};

// 运行时计算一次，避免 hardcode
static int s_kTriggersCount = 0;   // 不含 nullptr
static int s_kCustomIndex   = 0;   // "CUSTOM" 的索引

static void ensureTriggersMeta()
{
    if (s_kTriggersCount > 0) return;
    for (int k = 0; kTriggers[k]; ++k)
    {
        if (std::strcmp(kTriggers[k], "CUSTOM") == 0)
            s_kCustomIndex = k;
        ++s_kTriggersCount;
    }
}

static const char* kWindowNames[] = { "Locked", "ComboWindow", "Cancelable" };

namespace
{
    constexpr int kDevThemeVarCount = 6;
    constexpr int kDevThemeColorCount = 10;

    void pushDevEditorTheme()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.12f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.16f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.24f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.16f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.44f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.40f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.27f, 0.38f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.37f, 0.52f, 1.0f));
    }

    void popDevEditorTheme()
    {
        ImGui::PopStyleColor(kDevThemeColorCount);
        ImGui::PopStyleVar(kDevThemeVarCount);
    }

    void drawDevSectionTitle(const char* title)
    {
        ImGui::SeparatorText(title);
    }

    void drawDevMetric(const char* label, const char* value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        ImGui::TextUnformatted(value);
    }

    void drawDevMetric(const char* label, int value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        ImGui::Text("%d", value);
    }
}

using namespace engine::statemachine;
using game::statemachine::SmLoader;

namespace game::scene {

// ─────────────────────────────────────────────────────────────────────────────
//  公开接口
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::open()
{
    m_open = true;
    scanSmFiles();
}

void StateMachineEditor::toggle()
{
    if (m_open)
        m_open = false;
    else
        open();
}

    void StateMachineEditor::openWithJson(const std::string& path, const std::string& suggestedSavePath)
    {
        open();
        if (!path.empty())
        {
            loadJsonFrom(path);
            m_showLauncher = false;
        }
        else
        {
            // 无已有文件：总是重置到启动器状态，并用建议路径作为新建默认路径
            m_data        = StateMachineData{};
            m_savePath    = suggestedSavePath;
            m_showLauncher = true;
            // 从建议路径中提取 stem（如 "gundom"）预填到新建 ID 输入框
            if (!suggestedSavePath.empty())
            {
                const std::string stem = fs::path(suggestedSavePath).stem().stem().string();
                std::strncpy(m_newFileIdBuf, stem.c_str(), sizeof(m_newFileIdBuf) - 1);
                m_newFileIdBuf[sizeof(m_newFileIdBuf) - 1] = '\0';
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  内联渲染（无独立浮动窗口，嵌入 Tab 使用）
    // ─────────────────────────────────────────────────────────────────────────────
    void StateMachineEditor::renderInline()
    {
        ensureTriggersMeta();
        pushDevEditorTheme();

        // 确保一旦进入 Tab 就"开启"
        if (!m_open)
        {
            m_open = true;
            m_showLauncher = true;
            if (m_smFiles.empty()) scanSmFiles();
        }

        // ── 内联启动页 ─────────────────────────────────────────────────────────
        if (m_showLauncher)
        {
            ImGui::SeparatorText("选择状态机文件");
            ImGui::TextUnformatted("选择已有 *.sm.json，或新建：");

            const float listH = ImGui::GetContentRegionAvail().y - 70.0f;
            ImGui::BeginChild("##smeil_files", ImVec2(0, std::max(40.0f, listH)), ImGuiChildFlags_Borders);
            for (const auto &entry : m_smFiles)
            {
                ImGui::PushID(entry.path.c_str());
                if (ImGui::Selectable(entry.displayName.c_str()))
                {
                    loadJsonFrom(entry.path);
                    m_justLoaded = true;
                    m_showLauncher = false;
                }
                ImGui::PopID();
            }
            if (m_smFiles.empty())
                ImGui::TextDisabled("（暂无 *.sm.json 文件）");
            ImGui::EndChild();

            if (ImGui::Button("刷新列表##smeil")) scanSmFiles();
            ImGui::SameLine();
            // 若已预设建议路径（来自贴图名），显示一键新建按钮
            if (!m_savePath.empty())
            {
                const std::string btnLabel = "一键新建: " + fs::path(m_savePath).filename().string() + "##smeil_quick";
                if (ImGui::Button(btnLabel.c_str()))
                {
                    const std::string stem = fs::path(m_savePath).stem().stem().string();
                    newFile(stem.empty() ? m_newFileIdBuf : stem);
                    m_showLauncher = false;
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("自定义新建##smeil")) ImGui::OpenPopup("SME_NewFile_IL");
            ImGui::SameLine();
            // 快速展开内联文档
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.36f, 0.52f, 0.88f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.72f, 1.00f));
            if (ImGui::Button(m_showHelp ? "隐藏文档##smeil_help" : "? 查看开发文档##smeil_help"))
                m_showHelp = !m_showHelp;
            ImGui::PopStyleColor(2);

            if (ImGui::BeginPopup("SME_NewFile_IL"))
            {
                ImGui::TextUnformatted("角色 ID:");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputText("##smeil_id", m_newFileIdBuf, sizeof(m_newFileIdBuf));
                const bool valid = m_newFileIdBuf[0] != '\0';
                if (!valid) ImGui::BeginDisabled();
                if (ImGui::Button("创建##smeil_cr")) { newFile(m_newFileIdBuf); m_showLauncher = false; ImGui::CloseCurrentPopup(); }
                if (!valid) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("取消##smeil_ca")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // ── launcher 内文档面板 ──────────────────────────────────────────
            if (m_showHelp)
            {
                ImGui::Separator();
                ImGui::BeginChild("##smeil_doc_launcher", ImVec2(0, 360.0f), ImGuiChildFlags_Borders);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.95f, 1.0f, 1.0f));
                ImGui::SeparatorText("状态机开发文档（快速参考）");
                ImGui::PopStyleColor();

                if (ImGui::CollapsingHeader("触发器完整列表", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                    ImGui::TextUnformatted("按键事件（pushInput 调用一次）");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("  KEY_ATTACK  KEY_JUMP  KEY_DASH");
                    ImGui::TextDisabled("  KEY_MOVE_L  KEY_MOVE_R  KEY_BLOCK");
                    ImGui::TextDisabled("  KEY_SKILL_1  KEY_SKILL_2  KEY_SKILL_3");

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                    ImGui::TextUnformatted("持续状态（每帧满足就加入 activeInputs）");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("  IS_MOVING  NO_INPUT  IS_DASHING  IS_ATTACKING");
                    ImGui::TextDisabled("  GROUNDED  AIRBORNE  RISING  FALLING");

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                    ImGui::TextUnformatted("瞬间事件（发生那帧加入）");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("  ANIM_END  LAND  ON_WALL");

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                    ImGui::TextUnformatted("自定义条件（C++ registerCondition 注册，名称任意）");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("  IS_LOW_HP  ENEMY_NEARBY  SKILL_READY  ... 任意命名");
                }

                if (ImGui::CollapsingHeader("代码接入三步走", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                    ImGui::TextUnformatted("步骤 1：注册自定义条件（loadPlayerSM 之后）");
                    ImGui::PopStyleColor();
                    ImGui::TextUnformatted("  m_playerSM.registerCondition(\"IS_LOW_HP\",");
                    ImGui::TextUnformatted("      [&](const StateController&) -> bool {");
                    ImGui::TextUnformatted("          return m_hp < m_maxHp * 0.25f;");
                    ImGui::TextUnformatted("      });");
                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                    ImGui::TextUnformatted("步骤 2：注册回调（loadPlayerSM 之后）");
                    ImGui::PopStyleColor();
                    ImGui::TextUnformatted("  m_playerSM.setOnStateChanged(");
                    ImGui::TextUnformatted("      [this](const std::string& from, const std::string& to) {");
                    ImGui::TextUnformatted("          if (to == \"HURT\") startHurtFlash();");
                    ImGui::TextUnformatted("          if (to == \"DEAD\") triggerDeathSequence();");
                    ImGui::TextUnformatted("      });");
                    ImGui::TextUnformatted("  m_playerSM.setOnFrameEvent(");
                    ImGui::TextUnformatted("      [this](const std::string& event, int /*frame*/) {");
                    ImGui::TextUnformatted("          if (event == \"spawn_hitbox\") createMeleeHitbox(...);");
                    ImGui::TextUnformatted("          if (event.rfind(\"play_sound:\",0)==0) playSound(...);");
                    ImGui::TextUnformatted("      });");
                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                    ImGui::TextUnformatted("步骤 3：每帧驱动（tickPlayerSM 内部）");
                    ImGui::PopStyleColor();
                    ImGui::TextUnformatted("  // KEY 类：按下那帧 pushInput（内置 0.2s 缓冲）");
                    ImGui::TextUnformatted("  if (inp.isActionPressed(\"attack\"))");
                    ImGui::TextUnformatted("      m_playerSM.pushInput(\"KEY_ATTACK\", smTime);");
                    ImGui::TextUnformatted("  // 持续型：每帧满足就加入 activeInputs");
                    ImGui::TextUnformatted("  std::vector<std::string> inputs;");
                    ImGui::TextUnformatted("  if (grounded) inputs.push_back(\"GROUNDED\");");
                    ImGui::TextUnformatted("  else          inputs.push_back(\"AIRBORNE\");");
                    ImGui::TextUnformatted("  if (isMoving) inputs.push_back(\"IS_MOVING\");");
                    ImGui::TextUnformatted("  // 自定义条件无需手动加入，sm.update 内部自动求值");
                    ImGui::TextUnformatted("  auto result = m_playerSM.update(dt, inputs, smTime);");
                    ImGui::TextUnformatted("  // 详细示例见 ghost_swordsman_example.cpp");
                }

                if (ImGui::CollapsingHeader("帧事件推荐命名规范"))
                {
                    ImGui::TextDisabled("  \"play_sound:sword_swing\"  播放音效");
                    ImGui::TextDisabled("  \"spawn_hitbox\"            生成近战判定框");
                    ImGui::TextDisabled("  \"spawn_vfx:slash\"         生成斩击特效");
                    ImGui::TextDisabled("  \"spawn_projectile\"        发射子弹");
                    ImGui::TextDisabled("  \"shake_screen\"            屏幕震动");
                    ImGui::TextDisabled("  \"enable_cancel\"           手动开启取消窗口");
                    ImGui::TextDisabled("  \"root_motion:dx=50\"       注入位移冲量");
                }

                ImGui::EndChild();
            }

            popDevEditorTheme();
            return;
        }

        // ── 内联工具栏（替代菜单栏）─────────────────────────────────────────────
        if (ImGui::Button("保存##smeil")) saveJson();
        ImGui::SameLine();
        if (ImGui::Button("返回选择##smeil")) { m_showLauncher = true; scanSmFiles(); }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_savePath.empty() ? "<未保存>" : m_savePath.c_str());
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextDisabled("初始:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("##smeil_init", m_data.initialState.empty() ? "---" : m_data.initialState.c_str()))
        {
            for (auto &[name, _] : m_data.states)
            {
                bool sel = (name == m_data.initialState);
                if (ImGui::Selectable(name.c_str(), sel)) m_data.initialState = name;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.36f, 0.52f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.72f, 1.00f));
        if (ImGui::Button(m_showHelp ? "隐藏文档##smeil_help2" : "? 开发文档##smeil_help2"))
            m_showHelp = !m_showHelp;
        ImGui::PopStyleColor(2);

        // ── 开发文档面板（折叠式） ────────────────────────────────────────────
        if (m_showHelp)
        {
            ImGui::Separator();
            ImGui::BeginChild("##smeil_doc", ImVec2(0, 420.0f), ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.95f, 1.0f, 1.0f));
            ImGui::SeparatorText("状态机配置说明");
            ImGui::PopStyleColor();

            if (ImGui::CollapsingHeader("一、状态（State）字段说明", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled("  animationId   帧编辑器中定义的动作名，状态激活时播放此动画。");
                ImGui::TextDisabled("  loop          true=循环播放；false=播完后留在最后一帧并触发 ANIM_END。");
                ImGui::TextDisabled("  totalFrames   帧数（用于区间和事件计算，需与帧编辑器一致）。");
                ImGui::Separator();
                ImGui::TextDisabled("  transitions   转换列表（按 priority 降序，优先级大的先检查）：");
                ImGui::TextDisabled("    trigger       触发器名称（见下方触发器列表）。");
                ImGui::TextDisabled("    targetState   目标状态名（必须存在于当前状态机内）。");
                ImGui::TextDisabled("    priority      整数，越大越优先，相同时按定义顺序。");
                ImGui::TextDisabled("    requireWindow 是否要求当前帧在指定区间内（配合连招使用）。");
                ImGui::Separator();
                ImGui::TextDisabled("  windows       帧区间列表（用于连招/取消窗口）：");
                ImGui::TextDisabled("    Locked       锁定：不可被打断。");
                ImGui::TextDisabled("    ComboWindow  连招窗口：在此区间内 KEY_ATTACK 等指令可被缓存消耗。");
                ImGui::TextDisabled("    Cancelable   可取消：可被闪避/技能强制打断。");
                ImGui::Separator();
                ImGui::TextDisabled("  frameEvents   帧事件列表（特定帧触发，由代码监听）：");
                ImGui::TextDisabled("    frame         触发帧号（0 起始）。");
                ImGui::TextDisabled("    event         事件字符串，建议格式 \"类别:参数\"。");
                ImGui::TextDisabled("    示例: \"play_sound:sword\"  \"spawn_hitbox\"  \"shake_screen\"");
            }

            if (ImGui::CollapsingHeader("二、内置触发器列表", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("── 按键事件（只在按下那帧 pushInput 一次）");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("  KEY_ATTACK  KEY_JUMP  KEY_DASH  KEY_MOVE_L  KEY_MOVE_R");
                ImGui::TextDisabled("  KEY_BLOCK   KEY_SKILL_1  KEY_SKILL_2  KEY_SKILL_3");

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("── 持续状态（每帧满足条件就加入 activeInputs）");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("  IS_MOVING   NO_INPUT   IS_DASHING   IS_ATTACKING");
                ImGui::TextDisabled("  GROUNDED    AIRBORNE   RISING       FALLING");

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("── 瞬间事件（只在发生那帧加入 activeInputs）");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("  ANIM_END  （非循环动画播完时自动触发）");
                ImGui::TextDisabled("  LAND      （上一帧 AIRBORNE -> 本帧 GROUNDED）");
                ImGui::TextDisabled("  ON_WALL   （贴墙检测）");
            }

            if (ImGui::CollapsingHeader("三、代码接入：自定义条件", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                ImGui::TextUnformatted("// 在状态机编辑器中，Trigger 字段填写自定义名称（任意字符串）");
                ImGui::TextUnformatted("// 在 C++ 代码中调用 registerCondition 注册逻辑：");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("");
                ImGui::TextUnformatted("  m_playerSM.registerCondition(\"IS_LOW_HP\",");
                ImGui::TextUnformatted("      [this](const StateController&) -> bool {");
                ImGui::TextUnformatted("          return m_hp < m_maxHp * 0.25f;");
                ImGui::TextUnformatted("      });");
                ImGui::Spacing();
                ImGui::TextUnformatted("  m_playerSM.registerCondition(\"NEAR_ENEMY\",");
                ImGui::TextUnformatted("      [this](const StateController& sm) -> bool {");
                ImGui::TextUnformatted("          // sm.getCurrentState() 可查当前状态");
                ImGui::TextUnformatted("          return getNearestEnemyDist() < 80.0f;");
                ImGui::TextUnformatted("      });");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
                ImGui::TextUnformatted("  // 取消注册：");
                ImGui::PopStyleColor();
                ImGui::TextUnformatted("  m_playerSM.unregisterCondition(\"IS_LOW_HP\");");
                ImGui::TextUnformatted("  m_playerSM.clearConditions();  // 清除全部");
            }

            if (ImGui::CollapsingHeader("四、代码接入：状态切换回调", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                ImGui::TextUnformatted("// 监听任意状态切换（前一状态 -> 新状态）");
                ImGui::PopStyleColor();
                ImGui::TextUnformatted("  m_playerSM.setOnStateChanged(");
                ImGui::TextUnformatted("      [this](const std::string& from, const std::string& to) {");
                ImGui::TextUnformatted("          spdlog::debug(\"[SM] {} -> {}\", from, to);");
                ImGui::TextUnformatted("          if (to == \"HURT\")  startHurtFlash();");
                ImGui::TextUnformatted("          if (to == \"DEAD\")  triggerDeathSequence();");
                ImGui::TextUnformatted("          if (from == \"JUMP\" && to == \"IDLE\") spawnLandDust();");
                ImGui::TextUnformatted("      });");
            }

            if (ImGui::CollapsingHeader("五、代码接入：帧事件回调", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                ImGui::TextUnformatted("// 监听 .sm.json 中 frameEvents 配置的帧事件");
                ImGui::TextUnformatted("// 建议 event 字段格式：\"类别:参数\"");
                ImGui::PopStyleColor();
                ImGui::TextUnformatted("  m_playerSM.setOnFrameEvent(");
                ImGui::TextUnformatted("      [this](const std::string& event, int frame) {");
                ImGui::TextUnformatted("          if (event == \"play_sound:sword_swing\")");
                ImGui::TextUnformatted("              playSound(\"sword_swing\");");
                ImGui::TextUnformatted("          if (event == \"spawn_hitbox\")");
                ImGui::TextUnformatted("              createMeleeHitbox(getActorPos(), 48.0f);");
                ImGui::TextUnformatted("          if (event == \"spawn_vfx:slash\")");
                ImGui::TextUnformatted("              emitSlashVFX(getActorPos(), getFacing());");
                ImGui::TextUnformatted("          if (event == \"shake_screen\")");
                ImGui::TextUnformatted("              startScreenShake(0.3f, 8.0f);");
                ImGui::TextUnformatted("      });");
            }

            if (ImGui::CollapsingHeader("六、强制跳转（外部干预）"))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                ImGui::TextUnformatted("// 绕过所有条件，直接切换到指定状态（被击/剧情用）");
                ImGui::PopStyleColor();
                ImGui::TextUnformatted("  m_playerSM.forceTransition(\"HURT\");");
                ImGui::TextUnformatted("  m_playerSM.forceTransition(\"DEAD\");");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
                ImGui::TextUnformatted("  ⚠ 会立即跳转并触发 onStateChanged 回调，但不检查区间/优先级。");
                ImGui::PopStyleColor();
            }

            if (ImGui::CollapsingHeader("七、完整示例文件"))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("完整示例见（仅供参考，不参与编译）：");
                ImGui::PopStyleColor();
                ImGui::TextUnformatted("  src/game/statemachine/ghost_swordsman_example.cpp");
                ImGui::Spacing();
                ImGui::TextDisabled("文件包含三个示例函数：");
                ImGui::BulletText("example_loadPlayerSM()   — 加载后注册条件/回调");
                ImGui::BulletText("example_tickPlayerSM()   — 每帧驱动（含 activeInputs 构建）");
                ImGui::BulletText("example_forceTransitions() — 强制跳转与状态查询");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.5f, 1.0f));
                ImGui::TextUnformatted("实际接入位置（已有基础实现）：");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("  loadPlayerSM()  →  game_scene.cpp ~L4132");
                ImGui::TextDisabled("  tickPlayerSM()  →  game_scene.cpp ~L4209");
                ImGui::TextDisabled("  成员变量: m_playerSM (StateController)");
                ImGui::TextDisabled("            m_playerSMData (StateMachineData)");
            }

            ImGui::EndChild();
            ImGui::Separator();
        }

        // ── 内容布局 ──────────────────────────────────────────────────────────
        const float toolbarH = ImGui::GetTextLineHeightWithSpacing() + 10.0f;
        const float timelineH = 78.0f;
        const float availH  = ImGui::GetContentRegionAvail().y;
        const float contentH = std::max(50.0f, availH - timelineH - toolbarH - 8.0f);
        const float leftW   = 220.0f;

        ImGui::BeginChild("##smeil_left", ImVec2(leftW, contentH), ImGuiChildFlags_Borders);
        renderStateList(contentH);
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##smeil_right", ImVec2(0, contentH), false);
        renderStateDetails();
        ImGui::EndChild();

        const float tlH = std::min(timelineH, std::max(10.0f, ImGui::GetContentRegionAvail().y - 4.0f));
        ImGui::BeginChild("##smeil_tl", ImVec2(0, tlH), ImGuiChildFlags_Borders);
        renderTimeline();
        ImGui::EndChild();

        popDevEditorTheme();
    }

// ─────────────────────────────────────────────────────────────────────────────
//  主渲染入口
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::render()
{
    if (!m_open) return;
    ensureTriggersMeta();  // 确保元数据已初始化

    pushDevEditorTheme();

    ImGui::SetNextWindowSize(ImVec2(1080, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 80), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse;

    bool keepOpen = true;
    if (!ImGui::Begin("状态机编辑器", &keepOpen, flags))
    {
        ImGui::End();
        popDevEditorTheme();
        if (!keepOpen) m_open = false;
        return;
    }
    if (!keepOpen) { m_open = false; ImGui::End(); popDevEditorTheme(); return; }

    renderMenuBar();

    if (m_showLauncher)
    {
        renderLauncher();
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    drawDevSectionTitle("状态机概览");
    if (ImGui::BeginTable("##sme_overview", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableNextColumn();
        drawDevMetric("角色", m_data.characterId.empty() ? "<未设置>" : m_data.characterId.c_str());
        ImGui::TableNextColumn();
        drawDevMetric("初始状态", m_data.initialState.empty() ? "---" : m_data.initialState.c_str());
        ImGui::TableNextColumn();
        drawDevMetric("状态数", static_cast<int>(m_data.states.size()));
        ImGui::TableNextColumn();
        drawDevMetric("文件", m_savePath.empty() ? "<未保存>" : m_savePath.c_str());
        ImGui::EndTable();
    }

    // ── 主布局：左列 + 右列 ────────────────────────────────────────────────
    const float winH         = ImGui::GetContentRegionAvail().y;
    const float timelineH    = 78.0f;
    const float contentH     = winH - timelineH - 8.0f;
    const float leftW        = 220.0f;

    // 左：状态列表
    ImGui::BeginChild("##sme_left", ImVec2(leftW, contentH), true);
    renderStateList(contentH);
    ImGui::EndChild();

    ImGui::SameLine();

    // 右：状态详情
    ImGui::BeginChild("##sme_right", ImVec2(0, contentH), false);
    renderStateDetails();
    ImGui::EndChild();

    // 底部：时间轴预览
    ImGui::BeginChild("##sme_timeline", ImVec2(0, timelineH), true);
    renderTimeline();
    ImGui::EndChild();

    ImGui::End();
    popDevEditorTheme();
}

// ─────────────────────────────────────────────────────────────────────────────
//  菜单栏
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::renderMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("文件"))
    {
        if (ImGui::MenuItem("返回启动页"))
        {
            m_showLauncher = true;
        }
        if (ImGui::MenuItem("保存", "Ctrl+S", false, !m_savePath.empty()))
            saveJson();
        if (ImGui::MenuItem("另存为..."))
        {
            // 简单弹窗另存：在角色列表目录保存新文件
            ImGui::OpenPopup("SME_SaveAs");
        }
        ImGui::EndMenu();
    }

    // 另存为弹窗
    if (ImGui::BeginPopup("SME_SaveAs"))
    {
        ImGui::TextUnformatted("保存路径:");
        static char saveBuf[256] = "assets/textures/Characters/new.sm.json";
        ImGui::InputText("##savepath", saveBuf, sizeof(saveBuf));
        if (ImGui::Button("确定") && saveBuf[0])
        {
            m_savePath = saveBuf;
            saveJson();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // 顶部信息栏
    ImGui::TextDisabled("角色");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_data.characterId.empty() ? "<未设置>" : m_data.characterId.c_str());
    ImGui::SameLine(0.0f, 18.0f);
    ImGui::TextDisabled("初始状态");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);

    // 初始状态下拉
    if (ImGui::BeginCombo("##init_state",
                          m_data.initialState.empty() ? "---"
                                                      : m_data.initialState.c_str(),
                          ImGuiComboFlags_None))
    {
        for (auto& [name, _] : m_data.states)
        {
            bool sel = (name == m_data.initialState);
            if (ImGui::Selectable(name.c_str(), sel))
                m_data.initialState = name;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::EndMenuBar();
}

// ─────────────────────────────────────────────────────────────────────────────
//  启动页（文件选择）
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::renderLauncher()
{
    drawDevSectionTitle("项目选择");
    ImGui::TextUnformatted("选择已有状态机，或在角色目录下创建一个新的状态机文件。");

    // 列表
    const float listH = ImGui::GetContentRegionAvail().y - 88.0f;
    ImGui::BeginChild("##sme_launcher_list", ImVec2(0, listH), true);
    for (const auto& entry : m_smFiles)
    {
        if (ImGui::Selectable(entry.displayName.c_str()))
            loadJsonFrom(entry.path);
    }
    if (m_smFiles.empty())
        ImGui::TextDisabled("（暂无 *.sm.json 文件）");
    ImGui::EndChild();

    drawDevSectionTitle("操作");
    if (ImGui::Button("刷新列表", ImVec2(110.0f, 0.0f))) scanSmFiles();
    ImGui::SameLine();
    if (ImGui::Button("新建状态机", ImVec2(132.0f, 0.0f))) ImGui::OpenPopup("SME_NewFile");

    // 新建弹窗
    if (ImGui::BeginPopup("SME_NewFile"))
    {
        ImGui::TextUnformatted("角色 ID（用于文件名）:");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##newid", m_newFileIdBuf, sizeof(m_newFileIdBuf));
        const bool valid = m_newFileIdBuf[0] != '\0';
        if (!valid) ImGui::BeginDisabled();
        if (ImGui::Button("创建"))
        {
            newFile(m_newFileIdBuf);
            ImGui::CloseCurrentPopup();
        }
        if (!valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  左列：状态列表
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::renderStateList(float listH)
{
    drawDevSectionTitle("状态列表");
    ImGui::TextDisabled("共 %d 个状态", static_cast<int>(m_data.states.size()));

    const float btnH  = 28.0f;
    const float scrollH = listH - 108.0f;

    ImGui::BeginChild("##sme_states", ImVec2(0, scrollH), false);
    for (auto& [name, node] : m_data.states)
    {
        const bool selected = (name == m_selState);
        if (ImGui::Selectable(name.c_str(), selected))
        {
            m_selState    = name;
            m_selTransIdx = m_selWinIdx = m_selRmIdx = m_selEvtIdx = -1;
        }
    }
    ImGui::EndChild();

    drawDevSectionTitle("编辑");

    // 新建状态按钮
    if (ImGui::Button("+ 新建状态", ImVec2(-1, btnH)))
        ImGui::OpenPopup("SME_NewState");

    // 删除状态按钮
    const bool canDel = !m_selState.empty() &&
                        m_selState != "IDLE" && m_selState != "MOVE";
    if (!canDel) ImGui::BeginDisabled();
    if (ImGui::Button("删除当前状态", ImVec2(-1, btnH)))
    {
        m_data.states.erase(m_selState);
        m_selState.clear();
    }
    if (!canDel) ImGui::EndDisabled();

    // 新建状态弹窗
    if (ImGui::BeginPopup("SME_NewState"))
    {
        ImGui::TextUnformatted("状态名称（全大写）:");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("##ns", m_newStateNameBuf, sizeof(m_newStateNameBuf));
        const bool valid = m_newStateNameBuf[0] != '\0' &&
                           m_data.states.find(m_newStateNameBuf) == m_data.states.end();
        if (!valid) ImGui::BeginDisabled();
        if (ImGui::Button("创建"))
        {
            StateNode newNode;
            newNode.animationId = m_newStateNameBuf;
            newNode.loop        = false;
            newNode.totalFrames = 8;
            m_data.states[m_newStateNameBuf] = newNode;
            m_selState = m_newStateNameBuf;
            std::memset(m_newStateNameBuf, 0, sizeof(m_newStateNameBuf));
            ImGui::CloseCurrentPopup();
        }
        if (!valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消"))
        {
            std::memset(m_newStateNameBuf, 0, sizeof(m_newStateNameBuf));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  右侧：状态详情（分页）
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::renderStateDetails()
{
    if (m_selState.empty() || m_data.states.find(m_selState) == m_data.states.end())
    {
        ImGui::TextDisabled("← 从左侧选中一个状态开始编辑");
        return;
    }

    StateNode& node = m_data.states.at(m_selState);

    drawDevSectionTitle("当前状态");
    if (ImGui::BeginTable("##sme_state_summary", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableNextColumn();
        drawDevMetric("名称", m_selState.c_str());
        ImGui::TableNextColumn();
        drawDevMetric("动画", node.animationId.empty() ? "<未设置>" : node.animationId.c_str());
        ImGui::TableNextColumn();
        drawDevMetric("总帧数", node.totalFrames);
        ImGui::TableNextColumn();
        drawDevMetric("循环", node.loop ? "是" : "否");
        ImGui::EndTable();
    }

    if (!ImGui::BeginTabBar("##sme_tabs")) return;

    // ── Tab 1: 基本属性 ────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("基本属性"))
    {
        // 动画 ID
        static char animBuf[64] = {};
        static std::string lastState;
        if (lastState != m_selState)
        {
            std::strncpy(animBuf, node.animationId.c_str(), sizeof(animBuf) - 1);
            lastState = m_selState;
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::TextUnformatted("动画 ID:");  ImGui::SameLine();
        if (ImGui::InputText("##animId", animBuf, sizeof(animBuf)))
            node.animationId = animBuf;

        // 循环 & 总帧数
        ImGui::Checkbox("循环播放", &node.loop);
        ImGui::SameLine(160.0f);
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("总帧数", &node.totalFrames);
        if (node.totalFrames < 1) node.totalFrames = 1;

        ImGui::EndTabItem();
    }

    // ── Tab 2: 帧区间 ─────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("帧区间"))
    {
        ImGui::TextUnformatted("定义各帧所在的 window 类型（Locked / ComboWindow / Cancelable）");
        ImGui::Spacing();

        if (ImGui::BeginTable("##wins", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("起帧", ImGuiTableColumnFlags_WidthFixed, 58);
            ImGui::TableSetupColumn("止帧", ImGuiTableColumnFlags_WidthFixed, 58);
            ImGui::TableSetupColumn("类型",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("操作",  ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)node.windows.size(); i++)
            {
                auto& w = node.windows[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##ws", &w.startFrame); if (w.startFrame < 0) w.startFrame = 0;

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##we", &w.endFrame); if (w.endFrame < w.startFrame) w.endFrame = w.startFrame;

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                int wt = static_cast<int>(w.type);
                if (ImGui::Combo("##wt", &wt, kWindowNames, 3))
                    w.type = static_cast<WindowType>(wt);

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("X"))
                {
                    node.windows.erase(node.windows.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("+ 添加帧区间"))
        {
            FrameWindow nw;
            nw.startFrame = 0;
            nw.endFrame   = std::max(0, node.totalFrames - 1);
            nw.type       = WindowType::Locked;
            node.windows.push_back(nw);
        }
        ImGui::EndTabItem();
    }

    // ── Tab 3: 根位移 ─────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("根位移"))
    {
        ImGui::TextUnformatted("每帧注入到物理体的位移量（像素/帧）");
        ImGui::Spacing();

        if (ImGui::BeginTable("##rm", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("帧",   ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableSetupColumn("dx",   ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("dy",   ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)node.rootMotion.size(); i++)
            {
                auto& rm = node.rootMotion[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1); ImGui::InputInt("##f", &rm.frame);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##dx", &rm.dx, 0, 0, "%.1f");

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##dy", &rm.dy, 0, 0, "%.1f");

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("X"))
                {
                    node.rootMotion.erase(node.rootMotion.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("+ 添加根位移"))
            node.rootMotion.push_back({0, 5.0f, 0.0f});

        ImGui::EndTabItem();
    }

    // ── Tab 4: 帧事件 ─────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("帧事件"))
    {
        ImGui::TextUnformatted("在指定帧触发的事件回调（play_sound:xxx / spawn_vfx:yyy / shake_screen）");
        ImGui::Spacing();

        if (ImGui::BeginTable("##fe", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("帧",   ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableSetupColumn("事件", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)node.frameEvents.size(); i++)
            {
                auto& fe = node.frameEvents[i];
                // 每行独立缓冲区，避免跨帧静态数组越界
                char evtBuf[256];
                std::strncpy(evtBuf, fe.event.c_str(), sizeof(evtBuf) - 1);
                evtBuf[sizeof(evtBuf) - 1] = '\0';

                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1); ImGui::InputInt("##ef", &fe.frame);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##ev", evtBuf, sizeof(evtBuf)))
                    fe.event = evtBuf;

                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("X"))
                {
                    node.frameEvents.erase(node.frameEvents.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("+ 添加帧事件"))
        {
            node.frameEvents.push_back({0, "play_sound:xxx"});
        }
        ImGui::EndTabItem();
    }

    // ── Tab 5: 转换条件 ────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("转换条件"))
    {
        ImGui::TextUnformatted("这里定义「什么触发 → 跳转到哪个状态」");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("##trans", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("优先级", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("触发器", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("目标状态", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("需要区间", ImGuiTableColumnFlags_WidthFixed, 65);
            ImGui::TableSetupColumn("区间类型", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("操作",    ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            // 不使用静态数组—— 每行用栈上 buf，避免跨帧索引越界
            for (int i = 0; i < (int)node.transitions.size(); i++)
            {
                auto& t = node.transitions[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                // 优先级
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##pri", &t.priority);

                // 触发器
                ImGui::TableSetColumnIndex(1);
                // 匹配当前 trigger 在预设列表中的位置
                int selTrig = s_kCustomIndex;  // 默认自定义
                for (int k = 0; k < s_kTriggersCount; ++k)
                {
                    if (t.trigger == kTriggers[k]) { selTrig = k; break; }
                }
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::Combo("##trig", &selTrig, kTriggers, s_kTriggersCount))
                {
                    if (selTrig != s_kCustomIndex)
                        t.trigger = kTriggers[selTrig];
                    // 选 CUSTOM 时保持原内容
                }
                if (selTrig == s_kCustomIndex)
                {
                    // 栈上 buf，每帧初始化
                    char ctBuf[64];
                    std::strncpy(ctBuf, t.trigger.c_str(), sizeof(ctBuf) - 1);
                    ctBuf[sizeof(ctBuf) - 1] = '\0';
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::InputText("##ctrig", ctBuf, sizeof(ctBuf)))
                        t.trigger = ctBuf;
                }

                // 目标状态
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##tgt", t.targetState.empty() ? "---" : t.targetState.c_str()))
                {
                    for (auto& [sname, _] : m_data.states)
                    {
                        bool sel = (sname == t.targetState);
                        if (ImGui::Selectable(sname.c_str(), sel))
                            t.targetState = sname;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // 需要区间
                ImGui::TableSetColumnIndex(3);
                ImGui::Checkbox("##rw", &t.requireWindow);

                // 区间类型
                ImGui::TableSetColumnIndex(4);
                if (!t.requireWindow) ImGui::BeginDisabled();
                int wt = static_cast<int>(t.windowType);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##wtype", &wt, kWindowNames, 3))
                    t.windowType = static_cast<WindowType>(wt);
                if (!t.requireWindow) ImGui::EndDisabled();

                // 删除
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton("X"))
                {
                    node.transitions.erase(node.transitions.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("+ 添加转换"))
        {
            Transition t;
            t.trigger     = "KEY_ATTACK";
            t.targetState = m_data.initialState;
            t.priority    = 5;
            node.transitions.push_back(t);
        }

        // 按优先级排序
        ImGui::SameLine();
        if (ImGui::Button("按优先级排序"))
        {
            std::sort(node.transitions.begin(), node.transitions.end(),
                      [](const Transition& a, const Transition& b){
                          return a.priority > b.priority;
                      });
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

// ─────────────────────────────────────────────────────────────────────────────
//  时间轴预览（底部）
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::renderTimeline()
{
    if (m_selState.empty() || m_data.states.find(m_selState) == m_data.states.end())
    {
        ImGui::TextDisabled("无状态");
        return;
    }

    const StateNode& node = m_data.states.at(m_selState);
    const int totalFrames = std::max(1, node.totalFrames);

    drawDevSectionTitle("帧区间预览");
    ImGui::TextDisabled("红: Locked  蓝: ComboWindow  绿: Cancelable");

    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    const float  availW   = ImGui::GetContentRegionAvail().x - 4.0f;
    const float  barH     = 22.0f;
    const float  pxPerFrm = availW / float(totalFrames);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 底色（Locked 灰）
    dl->AddRectFilled(origin, ImVec2(origin.x + availW, origin.y + barH),
                      IM_COL32(80, 80, 80, 200), 4.0f);

    // 各个区间
    for (const auto& w : node.windows)
    {
        const float x0 = origin.x + w.startFrame  * pxPerFrm;
        const float x1 = origin.x + (w.endFrame + 1) * pxPerFrm;

        ImU32 col;
        switch (w.type)
        {
            case WindowType::ComboWindow: col = IM_COL32(60, 160, 230, 220); break;
            case WindowType::Cancelable:  col = IM_COL32(60, 200, 100, 220); break;
            default:                      col = IM_COL32(200, 80,  80, 220); break;
        }
        dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + barH), col, 3.0f);

        // 标签
        const char* label = kWindowNames[static_cast<int>(w.type)];
        if ((x1 - x0) > 40.0f)
            dl->AddText(ImVec2(x0 + 4.0f, origin.y + 4.0f), IM_COL32(255, 255, 255, 230), label);
    }

    // 帧分隔线
    for (int f = 1; f < totalFrames; f++)
    {
        const float x = origin.x + f * pxPerFrm;
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + barH),
                    IM_COL32(255, 255, 255, 50));
    }

    // 占位
    ImGui::Dummy(ImVec2(availW, barH));
}

// ─────────────────────────────────────────────────────────────────────────────
//  文件扫描
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::scanSmFiles()
{
    m_smFiles.clear();
    const fs::path dir = "assets/textures/Characters";
    if (!fs::exists(dir)) { fs::create_directories(dir); return; }

    for (const auto& entry : fs::directory_iterator(dir))
    {
        const auto& p = entry.path();
        if (p.extension() == ".json" &&
            p.stem().extension() == ".sm")
        {
            SmEntry e;
            e.path        = p.string();
            e.displayName = p.stem().stem().string() + ".sm.json";
            m_smFiles.push_back(e);
        }
    }
    std::sort(m_smFiles.begin(), m_smFiles.end(),
              [](const SmEntry& a, const SmEntry& b){ return a.displayName < b.displayName; });
}

// ─────────────────────────────────────────────────────────────────────────────
//  新建文件（预置 IDLE / MOVE）
// ─────────────────────────────────────────────────────────────────────────────
void StateMachineEditor::newFile(const std::string& characterId)
{
    m_data              = StateMachineData{};
    m_data.characterId  = characterId;
    m_data.initialState = "IDLE";

    // IDLE 状态（animationId 必须匹配 AnimationComponent 注册的 clip 名，区分大小写）
    StateNode idle;
    idle.animationId  = "idle";   // createPlayer 注册为 "idle"
    idle.loop         = true;
    idle.totalFrames  = 8;
    idle.transitions.push_back({"KEY_ATTACK", "ATTACK_1", 5, false, WindowType::Locked});
    idle.transitions.push_back({"KEY_JUMP",   "JUMP",     4, false, WindowType::Locked});
    idle.transitions.push_back({"IS_MOVING",  "MOVE",     3, false, WindowType::Locked});
    m_data.states["IDLE"] = std::move(idle);

    // MOVE 状态
    StateNode move;
    move.animationId  = "walk";   // createPlayer 注册为 "walk"
    move.loop         = true;
    move.totalFrames  = 8;
    move.transitions.push_back({"KEY_ATTACK", "ATTACK_1", 5, false, WindowType::Locked});
    move.transitions.push_back({"NO_INPUT",   "IDLE",     1, false, WindowType::Locked});
    m_data.states["MOVE"] = std::move(move);

    m_savePath     = "assets/textures/Characters/" + characterId + ".sm.json";
    m_selState     = "IDLE";
    m_showLauncher = false;
    m_justLoaded   = true;   // 新建也视为刚加载

    std::memset(m_newFileIdBuf, 0, sizeof(m_newFileIdBuf));
    spdlog::info("[SMEditor] 新建状态机: {}", characterId);

    // 立即保存并通知 character_editor 回写路径到 profile
    saveJson();
}

void StateMachineEditor::loadJsonFrom(const std::string& path)
{
    StateMachineData d;
    if (SmLoader::load(path, d))
    {
        m_data         = std::move(d);
        m_savePath     = path;
        m_selState     = m_data.initialState;
        m_showLauncher = false;
        m_justLoaded   = true;   // 通知 game_scene 调用 loadPlayerSM
    }
    else
    {
        spdlog::warn("[SMEditor] 加载失败: {}", SmLoader::lastError());
    }
}

void StateMachineEditor::saveJson()
{
    if (m_savePath.empty()) { spdlog::warn("[SMEditor] 保存路径为空"); return; }

    // 确保目录存在
    fs::path p(m_savePath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());

    SmLoader::save(m_data, m_savePath);
    m_justSaved = true;
}

} // namespace game::scene
