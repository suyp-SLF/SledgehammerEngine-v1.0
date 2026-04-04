/**
 * frame_editor.cpp  —  动作序列帧编辑器（ImGui 实现）
 *
 * 布局：
 *  ┌──────────────────────────────────────────────────────────────┐
 *  │  [菜单栏] 文件  编辑  视图  帮助                              │
 *  ├──────────|──────────────────────────────────|────────────────┤
 *  │ 左侧面板 │           主画布（画布+拖拽）     │  右侧属性面板  │
 *  │ 动作列表 │                                  │  帧 sx/sy/sw/sh│
 *  │ 帧列表   │                                  │  锚点          │
 *  │          │                                  │  判定盒列表    │
 *  ├──────────┴──────────────────────────────────┴────────────────┤
 *  │  时间轴 + 预览区                                              │
 *  ├──────────────────────────────────────────────────────────────┤
 *  │  状态栏                                                       │
 *  └──────────────────────────────────────────────────────────────┘
 */

#include "frame_editor.h"
#include "../../engine/resource/resource_manager.h"

#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui_impl_opengl3_loader.h>  // GL 函数（已被 imgui_impl_opengl3 加载）
#include <SDL3_image/SDL_image.h>       // 用于 CPU 端像素加载
#include <nlohmann/json.hpp>

// GL 常量（imgui 最小加载器未全部暴露）
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D      0x0DE1
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_NEAREST
#define GL_NEAREST         0x2600
#endif
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <queue>
#include <fstream>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace game::scene {

// ─────────────────────────────────────────────────────────────────────────────
//  内部常量
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kMinZoom = 0.25f;
static constexpr float kMaxZoom = 16.0f;

// 判定盒颜色（RGBA8）
static const ImU32 kBoxColor[3] = {
    IM_COL32( 60, 120, 255, 110),  // Hurtbox  蓝
    IM_COL32(255,  50,  50, 110),  // Hitbox   红
    IM_COL32( 50, 200,  50, 110),  // Bodybox  绿
};
static const ImU32 kBoxBorder[3] = {
    IM_COL32(120, 180, 255, 200),
    IM_COL32(255, 130, 130, 200),
    IM_COL32(130, 255, 130, 200),
};
static const char* kBoxTypeNames[3] = { "受击盒 (Hurtbox)", "攻击盒 (Hitbox)", "推挤盒 (Bodybox)" };

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
}

// ─────────────────────────────────────────────────────────────────────────────
//  公共入口
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::openWithJson(const std::string &jsonPath, const std::string &texturePath,
                               const std::string &suggestedSavePath)
{
    open();
    if (!jsonPath.empty())
    {
        loadJSONFrom(jsonPath);
        m_showLauncher = false;
    }
    else
    {
        // 无已有文件：重置到启动器状态，用建议路径作为新建默认路径
        m_actions.clear();
        m_selAction = -1;
        m_selFrame  = -1;
        if (!suggestedSavePath.empty())
        {
            std::strncpy(m_savePath, suggestedSavePath.c_str(), sizeof(m_savePath) - 1);
            m_savePath[sizeof(m_savePath) - 1] = '\0';
        }
        m_showLauncher = true;
    }
    if (!texturePath.empty())
        std::snprintf(m_texturePath, sizeof(m_texturePath), "%s", texturePath.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
//  内联渲染（嵌入 Tab，无独立浮动窗口）
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderInline(engine::resource::ResourceManager &resMgr)
{
    pushDevEditorTheme();

    if (!m_open)
    {
        m_open = true;
        m_showLauncher = true;
        if (m_jsonFiles.empty()) scanJsonFiles();
    }

    // ── 内联启动页 ─────────────────────────────────────────────────────────
    if (m_showLauncher)
    {
        ImGui::SeparatorText("选择帧动画文件");
        ImGui::TextUnformatted("选择已有动画 JSON，或新建：");

        const float listH = ImGui::GetContentRegionAvail().y - 70.0f;
        ImGui::BeginChild("##feil_files", ImVec2(0, std::max(40.0f, listH)), ImGuiChildFlags_Borders);
        for (const auto &je : m_jsonFiles)
        {
            ImGui::PushID(je.path.c_str());
            const std::string lbl = je.displayName + "##feil_item";
            if (ImGui::Button(lbl.c_str(), ImVec2(-1, 26)))
            {
                loadJSONFrom(je.path);
                if (m_texturePath[0] != '\0') { m_glTex = 0; loadTexture(resMgr); }
                m_showLauncher = false;
            }
            ImGui::PopID();
        }
        if (m_jsonFiles.empty())
            ImGui::TextDisabled("（暂无 .json 帧动画文件）");
        ImGui::EndChild();

        if (ImGui::Button("扫描文件##feil_scan")) scanJsonFiles();
        ImGui::SameLine();
        if (ImGui::Button("新建##feil_new"))
        {
            m_actions.clear(); m_selAction = -1; m_selFrame = -1;
            ensureDefaultActions(); m_selAction = 0;
            m_showLauncher = false;
        }
        popDevEditorTheme();
        return;
    }

    // ── 内联工具栏 ─────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("##feil_texpath", m_texturePath, sizeof(m_texturePath));
    ImGui::SameLine();
    if (ImGui::Button("载入纹理##feil")) { m_glTex = 0; loadTexture(resMgr); }
    ImGui::SameLine();
    if (ImGui::Button("保存##feil")) saveJSON();
    ImGui::SameLine();
    if (ImGui::Button("返回##feil")) { scanJsonFiles(); m_showLauncher = true; }
    ImGui::SameLine();
    if (ImGui::Button("\xe2\x86\x92\xe7\x8a\xb6\xe6\x80\x81\xe6\x9c\xba##feil")) m_wantsSmEditor = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("画格子切帧##feil_gc", &m_gridCutMode))
    {
        if (m_gridCutMode)
            m_anchorMode = false;
    }

    if (m_glTex == 0 && m_texturePath[0] != '\0')
        loadTexture(resMgr);

    // ── 内容布局 ──────────────────────────────────────────────────────────
    const float availH  = ImGui::GetContentRegionAvail().y;
    const float tlH     = 150.0f;
    const float statusH = 22.0f;
    const float midH    = std::max(80.0f, availH - tlH - statusH);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

    // 左侧面板
    ImGui::BeginChild("##feil_lp", ImVec2(180, midH), ImGuiChildFlags_Borders);
    renderLeftPanel();
    ImGui::EndChild();
    ImGui::SameLine();

    // 画布
    const float rpW = 220.0f;
    const float cvW = std::max(60.0f, ImGui::GetContentRegionAvail().x - rpW - 4.0f);
    ImGui::BeginChild("##feil_cv", ImVec2(cvW, midH), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    m_cvPos  = ImGui::GetCursorScreenPos();
    m_cvSize = ImGui::GetContentRegionAvail();
    renderCanvas();
    ImGui::EndChild();
    ImGui::SameLine();

    // 右侧面板
    ImGui::BeginChild("##feil_rp", ImVec2(rpW, midH), ImGuiChildFlags_Borders);
    renderRightPanel();
    ImGui::EndChild();

    ImGui::PopStyleVar();

    // 时间轴
    const float actualTlH = std::min(tlH, std::max(10.0f, ImGui::GetContentRegionAvail().y - statusH - 4.0f));
    ImGui::BeginChild("##feil_tl", ImVec2(0, actualTlH), ImGuiChildFlags_Borders);
    renderPreview();
    ImGui::EndChild();

    renderStatusBar();
    popDevEditorTheme();
}

// ─────────────────────────────────────────────────────────────────────────────
//  返回当前帧的贴图区域和锚点（供右侧预览面板实时使用）
// ─────────────────────────────────────────────────────────────────────────────
FrameEditor::FrameViewInfo FrameEditor::peekCurrentFrame() const
{
    FrameViewInfo info;
    if (m_glTex == 0 || m_texW <= 0.0f || m_texH <= 0.0f) return info;
    if (m_selAction < 0 || m_selAction >= static_cast<int>(m_actions.size())) return info;
    const ActionData &a = m_actions[static_cast<size_t>(m_selAction)];
    if (a.frames.empty()) return info;
    const int fi = (m_selFrame >= 0 && m_selFrame < static_cast<int>(a.frames.size()))
                   ? m_selFrame : 0;
    const FrameData &f = a.frames[static_cast<size_t>(fi)];
    info.glTex  = m_glTex; info.texW = m_texW; info.texH = m_texH;
    info.sx     = f.sx; info.sy = f.sy; info.sw = f.sw; info.sh = f.sh;
    info.anchorX = f.anchor_x; info.anchorY = f.anchor_y;
    info.valid  = true;
    return info;
}

void FrameEditor::render(engine::resource::ResourceManager &resMgr)
{
    if (!m_open) return;

    pushDevEditorTheme();

    // ── 启动选择页 ────────────────────────────────────────────────────────
    if (m_showLauncher)
    {
        renderLauncher(resMgr);
        popDevEditorTheme();
        return;
    }

    // 首次打开时居中
    if (m_firstOpen)
    {
        m_firstOpen = false;
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f - 560.0f,
                                 io.DisplaySize.y * 0.5f - 360.0f},
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize({1120.0f, 720.0f}, ImGuiCond_Always);
    }
    else
    {
        ImGui::SetNextWindowSize({1120.0f, 720.0f}, ImGuiCond_FirstUseEver);
    }

    ImGui::SetNextWindowBgAlpha(0.96f);
    if (!ImGui::Begin("动作序列帧编辑器##fe", &m_open,
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    // 确保纹理已加载
    if (m_glTex == 0 && m_texturePath[0] != '\0')
        loadTexture(resMgr);

    // ── 渲染各区域 ──────────────────────────────────────────────
    renderMenuBar(resMgr);

    drawDevSectionTitle("项目概览");
    if (ImGui::BeginTable("##fe_overview", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("文件");
        ImGui::TextUnformatted(m_savePath[0] ? m_savePath : "<未保存>");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("纹理");
        ImGui::TextUnformatted(m_texturePath[0] ? m_texturePath : "<未加载>");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("动作数");
        ImGui::Text("%d", static_cast<int>(m_actions.size()));
        ImGui::TableNextColumn();
        ImGui::TextDisabled("缩放");
        ImGui::Text("%.1fx", m_zoom);
        ImGui::EndTable();
    }

    // 内容区：三列（左 | 中 | 右）
    const float bottomH  = 160.0f;  // 时间轴区域高度
    const float statusH  = 22.0f;
    const float midH     = ImGui::GetContentRegionAvail().y - bottomH - statusH - 6.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

    // 左侧面板
    ImGui::BeginChild("##fe_left", ImVec2(180, midH), ImGuiChildFlags_Borders);
    renderLeftPanel();
    ImGui::EndChild();

    ImGui::SameLine();

    // 中央画布
    const float rightW = 220.0f;
    float canvasW = ImGui::GetContentRegionAvail().x - rightW - 4.0f;
    ImGui::BeginChild("##fe_canvas", ImVec2(canvasW, midH), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    m_cvPos  = ImGui::GetCursorScreenPos();
    m_cvSize = ImGui::GetContentRegionAvail();
    renderCanvas();
    ImGui::EndChild();

    ImGui::SameLine();

    // 右侧属性面板
    ImGui::BeginChild("##fe_right", ImVec2(rightW, midH), ImGuiChildFlags_Borders);
    renderRightPanel();
    ImGui::EndChild();

    ImGui::PopStyleVar();

    // 时间轴 + 预览
    ImGui::BeginChild("##fe_timeline", ImVec2(0, bottomH), ImGuiChildFlags_Borders);
    renderPreview();
    ImGui::EndChild();

    // 状态栏
    renderStatusBar();

    ImGui::End();
    popDevEditorTheme();
}

// ─────────────────────────────────────────────────────────────────────────────
//  菜单栏
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderMenuBar(engine::resource::ResourceManager &resMgr)
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("文件"))
    {
        ImGui::InputText("纹理路径##fepath", m_texturePath, sizeof(m_texturePath));
        if (ImGui::MenuItem("加载纹理"))
        {
            m_glTex = 0; m_pixels.clear();
            loadTexture(resMgr);
            snprintf(m_statusMsg, sizeof(m_statusMsg), "已加载: %s", m_texturePath);
        }
        ImGui::Separator();
        ImGui::TextDisabled("保存路径:");
        ImGui::SameLine();
        ImGui::TextUnformatted(m_savePath);  // 只显示，不允许手动改（保持原文件名）
        if (ImGui::MenuItem("保存 JSON (Ctrl+S)")) saveJSON();
        if (ImGui::MenuItem("另存为 JSON..."))
        {
            // 允许手动修改保存路径
            static char s_altPath[512] = {};
            std::strncpy(s_altPath, m_savePath, sizeof(s_altPath)-1);
            ImGui::OpenPopup("##saveas");
        }
        if (ImGui::BeginPopup("##saveas"))
        {
            static char s_altPath[512] = {};
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("路径##sapath", s_altPath, sizeof(s_altPath));
            if (ImGui::Button("确定##saok") && s_altPath[0] != '\0')
            {
                std::strncpy(m_savePath, s_altPath, sizeof(m_savePath)-1);
                saveJSON();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消##sacancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("返回项目选择")) { scanJsonFiles(); m_showLauncher = true; }
        if (ImGui::MenuItem("关闭")) m_open = false;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("编辑"))
    {
        if (ImGui::MenuItem("新建动作"))
        {
            m_actions.emplace_back();
            m_selAction = (int)m_actions.size() - 1;
            m_selFrame  = -1;
        }
        if (ImGui::MenuItem("删除当前帧", nullptr, false, selFrame() != nullptr))
        {
            auto* a = selAction();
            if (a)
            {
                a->frames.erase(a->frames.begin() + m_selFrame);
                m_selFrame = std::min(m_selFrame, (int)a->frames.size() - 1);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("视图"))
    {
        ImGui::Checkbox("显示网格##fegrid", &m_showGrid);
        if (m_showGrid)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50); ImGui::InputInt("W##fgw", &m_gridW);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50); ImGui::InputInt("H##fgh", &m_gridH);
        }
        ImGui::Checkbox("网格吸附##fesnap", &m_snapToGrid);
        ImGui::Checkbox("洋葱皮##feonion", &m_onionSkin);
        if (m_onionSkin)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::SliderFloat("透明度##foa", &m_onionAlpha, 0.05f, 0.5f);
        }
        if (ImGui::MenuItem("重置视图"))
        {
            m_origin = {0.0f, 0.0f};
            m_zoom = 3.0f;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("工具"))
    {
        if (ImGui::Selectable("\u666e\u901a\u9009\u6846##fesel", !m_anchorMode && !m_gridCutMode))
        { m_anchorMode = false; m_gridCutMode = false; }
        if (ImGui::Selectable("\u9501\u70b9\u8bbe\u7f6e##feanc", m_anchorMode))
        { m_anchorMode = true; m_gridCutMode = false; }
        ImGui::Separator();
        ImGui::Selectable("\u7f51\u683c\u5207\u5272##fegc", m_gridCutMode);
        if (ImGui::IsItemClicked())
        { m_gridCutMode = !m_gridCutMode; m_anchorMode = false; }
        if (m_gridCutMode)
        {
            ImGui::Separator();
            ImGui::Text("\u5e27\u5bbd/\u5e27\u9ad8\uff1a");
            ImGui::SetNextItemWidth(55); ImGui::InputInt("W##gcw", &m_gcFrameW, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputInt("H##gch", &m_gcFrameH, 0);
            ImGui::Text("\u95f4\u8ddd:");
            ImGui::SetNextItemWidth(55); ImGui::InputInt("X##gcsx", &m_gcSpacingX, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputInt("Y##gcsy", &m_gcSpacingY, 0);
            ImGui::Text("\u8d77\u59cb\u504f\u79fb:");
            ImGui::SetNextItemWidth(55); ImGui::InputInt("X##gcox", &m_gcOffsetX, 0);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputInt("Y##gcoy", &m_gcOffsetY, 0);
            m_gcFrameW   = std::max(1, m_gcFrameW);
            m_gcFrameH   = std::max(1, m_gcFrameH);
            m_gcSpacingX = std::max(0, m_gcSpacingX);
            m_gcSpacingY = std::max(0, m_gcSpacingY);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("跳转"))
    {
        if (ImGui::MenuItem("打开状态机编辑器"))
            m_wantsSmEditor = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ─────────────────────────────────────────────────────────────────────────────
//  左侧面板
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderLeftPanel()
{
    ImGui::SeparatorText("动作列表");
    for (int i = 0; i < (int)m_actions.size(); ++i)
    {
        const bool sel = (i == m_selAction);

        // ── 内联重命名模式 ──────────────────────────────────────────────
        if (m_renamingAction == i)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.2f, 0.4f, 1.0f));
            if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                // 回车确认
                if (m_renameBuffer[0] != '\0')
                    std::strncpy(m_actions[i].name, m_renameBuffer, sizeof(ActionData::name) - 1);
                m_renamingAction = -1;
            }
            ImGui::PopStyleColor();
            if (!ImGui::IsItemFocused())
            {
                // 点击别处取消
                if (m_renameBuffer[0] != '\0')
                    std::strncpy(m_actions[i].name, m_renameBuffer, sizeof(ActionData::name) - 1);
                m_renamingAction = -1;
            }
            // 首帧自动获取焦点
            ImGui::SetItemDefaultFocus();
            ImGui::SetKeyboardFocusHere(-1);
            continue;
        }

        // ── 普通显示行 ──────────────────────────────────────────────────
        ImGui::PushID(i);
        if (ImGui::Selectable(m_actions[i].name, sel,
                ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 18)))
        {
            m_selAction = i;
            m_selFrame  = -1;
            // 双击进入重命名
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_renamingAction = i;
                std::strncpy(m_renameBuffer, m_actions[i].name, sizeof(m_renameBuffer) - 1);
            }
        }
        if (sel) ImGui::SetItemDefaultFocus();

        // 右键上下文菜单
        if (ImGui::BeginPopupContextItem("##actctx"))
        {
            if (ImGui::MenuItem("重命名"))
            {
                m_renamingAction = i;
                std::strncpy(m_renameBuffer, m_actions[i].name, sizeof(m_renameBuffer) - 1);
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            bool isProtected = isProtectedAction(i);
            ImGui::BeginDisabled(isProtected);
            if (ImGui::MenuItem("删除动作"))
            {
                m_actions.erase(m_actions.begin() + i);
                if (m_selAction >= (int)m_actions.size())
                    m_selAction = (int)m_actions.size() - 1;
                m_selFrame = -1;
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                ImGui::EndPopup();
                ImGui::PopID();
                return; // 迭代器失效，提前返回
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // ── 底部按钮行 ──────────────────────────────────────────────────────
    if (ImGui::Button("+ 动作", ImVec2(-50, 0)))
    {
        m_actions.emplace_back();
        m_selAction = (int)m_actions.size() - 1;
        m_selFrame  = -1;
        // 直接进入重命名
        m_renamingAction = m_selAction;
        std::strncpy(m_renameBuffer, m_actions.back().name, sizeof(m_renameBuffer) - 1);
    }
    ImGui::SameLine();
    // 删除选中动作按钮（受保护的动作不可删）
    bool canDelete = (m_selAction >= 0 && m_selAction < (int)m_actions.size()
                      && !isProtectedAction(m_selAction));
    ImGui::BeginDisabled(!canDelete);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.12f, 0.12f, 0.9f));
    if (ImGui::Button("X##delact", ImVec2(44, 0)))
    {
        m_actions.erase(m_actions.begin() + m_selAction);
        if (m_selAction >= (int)m_actions.size())
            m_selAction = (int)m_actions.size() - 1;
        m_selFrame = -1;
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(isProtectedAction(m_selAction) ? "IDLE/MOVE 为保护动作，不可删除" : "删除选中动作");

    if (auto* a = selAction())
    {
        ImGui::SeparatorText("帧列表");
        for (int i = 0; i < (int)a->frames.size(); ++i)
        {
            char lbl[32]; snprintf(lbl, sizeof(lbl), "帧 %d  (%dms)", i, a->frames[i].duration_ms);
            const bool fsel = (i == m_selFrame);
            if (ImGui::Selectable(lbl, fsel, 0, ImVec2(0, 16)))
                m_selFrame = i;
            if (fsel) ImGui::SetItemDefaultFocus();
        }
        if (m_selFrame >= 0 && m_selFrame < (int)a->frames.size())
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 0.9f));
            if (ImGui::Button("删除帧", ImVec2(-1, 0)))
            {
                a->frames.erase(a->frames.begin() + m_selFrame);
                m_selFrame = std::min(m_selFrame, (int)a->frames.size() - 1);
            }
            ImGui::PopStyleColor();

            // 上下移动
            ImGui::BeginDisabled(m_selFrame == 0);
            if (ImGui::Button("↑##fmup", ImVec2(40, 0)))
            {
                std::swap(a->frames[m_selFrame], a->frames[m_selFrame - 1]);
                --m_selFrame;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_selFrame >= (int)a->frames.size() - 1);
            if (ImGui::Button("↓##fmdn", ImVec2(40, 0)))
            {
                std::swap(a->frames[m_selFrame], a->frames[m_selFrame + 1]);
                ++m_selFrame;
            }
            ImGui::EndDisabled();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  主画布
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderCanvas()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = m_cvPos;
    const ImVec2 p1 = {p0.x + m_cvSize.x, p0.y + m_cvSize.y};

    // 背景填充（深灰格子模拟透明，浅/深交替 8px）
    {
        const float gs = 8.0f;
        for (float gy = p0.y; gy < p1.y; gy += gs)
        for (float gx = p0.x; gx < p1.x; gx += gs)
        {
            int col = (((int)((gx - p0.x)/gs) + (int)((gy - p0.y)/gs)) & 1);
            dl->AddRectFilled({gx, gy}, {std::min(gx+gs, p1.x), std::min(gy+gs, p1.y)},
                col ? IM_COL32(60,60,60,255) : IM_COL32(45,45,45,255));
        }
    }

    // TODO: 更新画布子窗口鼠标输入
    // InvisibleButton 捕获鼠标
    ImGui::InvisibleButton("##cv_input", m_cvSize,
        ImGuiButtonFlags_MouseButtonLeft  |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovCanvas = ImGui::IsItemHovered();
    ImVec2 mousePos = ImGui::GetIO().MousePos;

    // ── 滚轮缩放 ──────────────────────────────────────────────────────
    if (hovCanvas)
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            // 以鼠标位置为锚点缩放：保持鼠标下方纹理像素不变
            ImVec2 tBefore = screenToTex(mousePos.x, mousePos.y);
            m_zoom = std::clamp(m_zoom * std::pow(1.15f, wheel), kMinZoom, kMaxZoom);
            // 调整 m_origin 使纹理像素对应鼠标位置不移动
            // screen  = p0 + (tex - origin) * zoom
            // origin  = tex - (screen - p0) / zoom
            m_origin.x = tBefore.x - (mousePos.x - p0.x) / m_zoom;
            m_origin.y = tBefore.y - (mousePos.y - p0.y) / m_zoom;
        }
    }

    // ── 中键 / Alt + 左键 / 右键 平移 ───────────────────────────────────
    if (hovCanvas && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)
        || ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().KeyAlt)))
    {
        m_panning   = true;
        m_panAnchor = mousePos;
        m_origOnPan = m_origin;
    }
    if (m_panning)
    {
        m_origin.x = m_origOnPan.x - (mousePos.x - m_panAnchor.x) / m_zoom;
        m_origin.y = m_origOnPan.y - (mousePos.y - m_panAnchor.y) / m_zoom;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)
         || ImGui::IsMouseReleased(ImGuiMouseButton_Right)
         || ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            m_panning = false;
    }

    if (m_glTex)
    {
        // 将整张纹理显示在画布上
        ImVec2 screenTL = texToScreen(0.0f, 0.0f);
        ImVec2 screenBR = texToScreen(m_texW, m_texH);
        // 限制绘制范围（裁剪）
        dl->PushClipRect(p0, p1, true);
        dl->AddImage(
            (ImTextureID)(intptr_t)m_glTex,
            screenTL, screenBR,
            {0.0f, 0.0f}, {1.0f, 1.0f},
            IM_COL32(255, 255, 255, 255));
        dl->PopClipRect();
    }
    else
    {
        dl->AddText({p0.x + 10, p0.y + 10}, IM_COL32(200, 100, 100, 255),
            "纹理未加载 — 请在 [文件] 菜单指定路径并点击 [加载纹理]");
    }

    // ── 画布中央当前帧大预览 ─────────────────────────────────────────
    if (m_glTex)
    {
        auto* cf = selFrame();
        if (cf && cf->sw > 0 && cf->sh > 0)
        {
            float u0 = cf->sx / m_texW;
            float v0 = cf->sy / m_texH;
            float u1 = (cf->sx + cf->sw) / m_texW;
            float v1 = (cf->sy + cf->sh) / m_texH;
            if (cf->flip_x)
                std::swap(u0, u1);

            const float maxW = m_cvSize.x * 0.32f;
            const float maxH = m_cvSize.y * 0.42f;
            const float scale = std::min(maxW / static_cast<float>(cf->sw),
                                         maxH / static_cast<float>(cf->sh));
            const ImVec2 previewSize{
                static_cast<float>(cf->sw) * scale,
                static_cast<float>(cf->sh) * scale
            };
            const ImVec2 previewMin{
                p0.x + (m_cvSize.x - previewSize.x) * 0.5f,
                p0.y + 18.0f
            };
            const ImVec2 previewMax{previewMin.x + previewSize.x, previewMin.y + previewSize.y};

            dl->PushClipRect(p0, p1, true);
            dl->AddRectFilled({previewMin.x - 8.0f, previewMin.y - 8.0f},
                              {previewMax.x + 8.0f, previewMax.y + 8.0f},
                              IM_COL32(10, 16, 24, 210), 8.0f);
            dl->AddRect({previewMin.x - 8.0f, previewMin.y - 8.0f},
                        {previewMax.x + 8.0f, previewMax.y + 8.0f},
                        IM_COL32(255, 220, 90, 180), 8.0f, 0, 1.5f);
            dl->AddImage((ImTextureID)(intptr_t)m_glTex, previewMin, previewMax,
                         {u0, v0}, {u1, v1}, IM_COL32(255, 255, 255, 235));
            dl->AddText({previewMin.x, previewMax.y + 6.0f}, IM_COL32(255, 230, 140, 220),
                        cf->flip_x ? "当前帧预览: 水平反向" : "当前帧预览: 正常朝向");
            dl->PopClipRect();
        }
    }

    // ── 网格 ─────────────────────────────────────────────────────────
    if (m_showGrid && m_glTex && m_zoom * m_gridW > 6.0f)
    {
        dl->PushClipRect(p0, p1, true);
        const ImU32 gc = IM_COL32(255, 255, 255, 30);
        // 垂直线
        float startX = std::floor(m_origin.x / m_gridW) * m_gridW;
        for (float gx = startX; gx <= m_texW; gx += m_gridW)
        {
            float sx = p0.x + (gx - m_origin.x) * m_zoom;
            if (sx < p0.x || sx > p1.x) continue;
            dl->AddLine({sx, p0.y}, {sx, p1.y}, gc);
        }
        // 水平线
        float startY = std::floor(m_origin.y / m_gridH) * m_gridH;
        for (float gy = startY; gy <= m_texH; gy += m_gridH)
        {
            float sy = p0.y + (gy - m_origin.y) * m_zoom;
            if (sy < p0.y || sy > p1.y) continue;
            dl->AddLine({p0.x, sy}, {p1.x, sy}, gc);
        }
        dl->PopClipRect();
    }

    // ── 洋葱皮（上一帧/下一帧）────────────────────────────────────────
    auto* a = selAction();
    if (m_onionSkin && m_glTex && a && m_selFrame >= 0)
    {
        dl->PushClipRect(p0, p1, true);
        auto drawOnion = [&](int fi, ImU32 tint) {
            if (fi < 0 || fi >= (int)a->frames.size()) return;
            const FrameData &fr = a->frames[fi];
            float u0 = fr.sx / m_texW,  v0 = fr.sy / m_texH;
            float u1 = (fr.sx+fr.sw) / m_texW, v1 = (fr.sy+fr.sh) / m_texH;
            if (fr.flip_x)
                std::swap(u0, u1);
            ImVec2 sl = texToScreen((float)fr.sx, (float)fr.sy);
            ImVec2 sr = texToScreen((float)(fr.sx+fr.sw), (float)(fr.sy+fr.sh));
            // 锚点对齐到当前帧锚点
            auto* cf = selFrame();
            if (cf)
            {
                float dx = (float)(cf->anchor_x - fr.anchor_x) * m_zoom;
                float dy = (float)(cf->anchor_y - fr.anchor_y) * m_zoom;
                sl.x += dx; sl.y += dy;
                sr.x += dx; sr.y += dy;
            }
            dl->AddImage((ImTextureID)(intptr_t)m_glTex, sl, sr, {u0,v0}, {u1,v1}, tint);
        };
        uint8_t oa = (uint8_t)(m_onionAlpha * 255.0f);
        drawOnion(m_selFrame - 1, IM_COL32(80, 120, 255, oa));  // 上帧：蓝调
        drawOnion(m_selFrame + 1, IM_COL32(255, 120, 80, oa));  // 下帧：橙调
        dl->PopClipRect();
    }

    // ── 所有已定义帧的轮廓 ────────────────────────────────────────────
    dl->PushClipRect(p0, p1, true);
    if (m_glTex && a)
    {
        for (int fi = 0; fi < (int)a->frames.size(); ++fi)
        {
            const FrameData &fr = a->frames[fi];
            ImVec2 fl = texToScreen((float)fr.sx, (float)fr.sy);
            ImVec2 fr2= texToScreen((float)(fr.sx+fr.sw), (float)(fr.sy+fr.sh));
            ImU32 c = (fi == m_selFrame)
                ? IM_COL32(255, 220, 50, 200)
                : IM_COL32(100, 180, 255, 100);
            dl->AddRect(fl, fr2, c, 0.0f, 0, (fi == m_selFrame) ? 2.0f : 1.0f);

            // 帧序号
            char idx[8]; snprintf(idx, sizeof(idx), "%d", fi);
            dl->AddText({fl.x + 2, fl.y + 1}, IM_COL32(255,255,180,200), idx);
        }
    }
    dl->PopClipRect();

    // ── 判定盒（当前帧）──────────────────────────────────────────────
    auto* cf = selFrame();
    if (cf && m_glTex)
    {
        dl->PushClipRect(p0, p1, true);
        // 锚点屏幕坐标
        ImVec2 anchor  = texToScreen((float)(cf->sx + cf->anchor_x),
                                     (float)(cf->sy + cf->anchor_y));

        // 绘制判定盒
        for (int bi = 0; bi < (int)cf->boxes.size(); ++bi)
        {
            const FrameBox &bx = cf->boxes[bi];
            int t = (int)bx.type;
            // 盒坐标相对于锚点，转化为纹理坐标再转屏幕
            float bx0 = (float)(cf->sx + cf->anchor_x) + bx.x;
            float by0 = (float)(cf->sy + cf->anchor_y) + bx.y;
            ImVec2 bs = texToScreen(bx0,        by0);
            ImVec2 be = texToScreen(bx0 + bx.w, by0 + bx.h);

            bool isSelBox = (bi == m_selBoxIdx);
            dl->AddRectFilled(bs, be, kBoxColor[t]);
            dl->AddRect(bs, be, kBoxBorder[t], 0.0f, 0, isSelBox ? 2.0f : 1.0f);
        }

        // 绘制锚点十字
        const float crossR = 6.0f;
        dl->AddLine({anchor.x - crossR, anchor.y}, {anchor.x + crossR, anchor.y},
            IM_COL32(255, 50, 50, 220), 1.5f);
        dl->AddLine({anchor.x, anchor.y - crossR}, {anchor.x, anchor.y + crossR},
            IM_COL32(255, 50, 50, 220), 1.5f);
        dl->AddCircle(anchor, 4.0f, IM_COL32(255, 50, 50, 220));

        dl->PopClipRect();
    }

    // ── 拖拽选区 ─────────────────────────────────────────────────────
    if (m_dragging)
    {
        ImVec2 sl = texToScreen(m_dragStartTex.x, m_dragStartTex.y);
        ImVec2 se = texToScreen(m_dragEndTex.x,   m_dragEndTex.y);
        dl->AddRectFilled(sl, se, IM_COL32(255, 255, 100, 30));
        dl->AddRect(sl, se, IM_COL32(255, 255, 100, 200));
    }

    // ── 画布鼠标交互 ─────────────────────────────────────────────────
    if (hovCanvas && !m_panning)
    {
        ImVec2 tPos = screenToTex(mousePos.x, mousePos.y);
        // 网格吸附
        if (m_snapToGrid)
        {
            tPos.x = std::floor(tPos.x / m_gridW) * m_gridW;
            tPos.y = std::floor(tPos.y / m_gridH) * m_gridH;
        }

        if (m_anchorMode && cf)
        {
            // 锚点模式：左键设置锚点
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
            {
                cf->anchor_x = (int)(tPos.x - cf->sx);
                cf->anchor_y = (int)(tPos.y - cf->sy);
                snprintf(m_statusMsg, sizeof(m_statusMsg),
                    "锚点更新: (%d, %d)", cf->anchor_x, cf->anchor_y);
            }
        }
        else if (!m_gridCutMode)
        {
            // 普通模式：左键拖拽 = 新帧矩形
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
            {
                m_dragging = true;
                m_dragStartTex = tPos;
                m_dragEndTex   = tPos;
            }
            if (m_dragging)
            {
                m_dragEndTex = tPos;
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    m_dragging = false;
                    confirmDragFrame();
                }
            }

            // 中键单击 = 魔棒 BFS（右键已用于平移）
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !ImGui::GetIO().KeyAlt)
            {
                if (m_pixels.empty()) fetchPixels();
                magicWandAt((int)tPos.x, (int)tPos.y);
            }

            // 左键单击（未拖拽）= 选中已有帧
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging && a)
            {
                for (int fi = (int)a->frames.size() - 1; fi >= 0; --fi)
                {
                    const FrameData &fr = a->frames[fi];
                    if (tPos.x >= fr.sx && tPos.x < fr.sx + fr.sw &&
                        tPos.y >= fr.sy && tPos.y < fr.sy + fr.sh)
                    {
                        m_selFrame = fi;
                        break;
                    }
                }
            }
        }
    }

    // ── 网格切割模式叠加层 ──────────────────────────────────────────────
    if (m_gridCutMode && m_glTex)
        renderGridCutOverlay();

    // ── 状态提示文字：鼠标坐标 ──────────────────────────────────────
    {
        ImVec2 tPos = screenToTex(mousePos.x, mousePos.y);
        char buf[64];
        snprintf(buf, sizeof(buf), "Tex: (%.0f, %.0f)  Zoom: %.1fx", tPos.x, tPos.y, m_zoom);
        ImVec2 textPos = {p0.x + 4, p1.y - 16};
        dl->AddText(textPos, IM_COL32(200, 200, 200, 180), buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  右侧属性面板
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderRightPanel()
{
    auto* a  = selAction();
    auto* cf = selFrame();

    // ── 动作属性 ──────────────────────────────────────────────────────
    if (a)
    {
        drawDevSectionTitle("动作属性");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##actname", a->name, sizeof(a->name));
        ImGui::Checkbox("循环##fl", &a->is_loop);
    }

    if (!cf)
    {
        ImGui::TextDisabled("未选中帧");
        return;
    }

    // ── 帧矩形 ────────────────────────────────────────────────────────
    drawDevSectionTitle("帧矩形");
    ImGui::SetNextItemWidth(60); ImGui::InputInt("X##fsx", &cf->sx); ImGui::SameLine();
    ImGui::SetNextItemWidth(60); ImGui::InputInt("Y##fsy", &cf->sy);
    ImGui::SetNextItemWidth(60); ImGui::InputInt("W##fsw", &cf->sw); ImGui::SameLine();
    ImGui::SetNextItemWidth(60); ImGui::InputInt("H##fsh", &cf->sh);
    cf->sw = std::max(1, cf->sw);
    cf->sh = std::max(1, cf->sh);

    // ── 锚点 ──────────────────────────────────────────────────────────
    drawDevSectionTitle("锚点");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::SetNextItemWidth(60); ImGui::InputInt("X##fax", &cf->anchor_x); ImGui::SameLine();
    ImGui::SetNextItemWidth(60); ImGui::InputInt("Y##fay", &cf->anchor_y);
    ImGui::PopStyleColor();

    bool wasAnchorMode = m_anchorMode;
    ImGui::SameLine();
    if (ImGui::Checkbox("设置模式##feam", &m_anchorMode) && m_anchorMode)
        snprintf(m_statusMsg, sizeof(m_statusMsg), "锚点模式：左键点击设置锚点");
    else if (wasAnchorMode && !m_anchorMode)
        m_statusMsg[0] = '\0';

    // ── 帧延迟 ────────────────────────────────────────────────────────
    drawDevSectionTitle("帧延迟与方向");
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("ms##fdur", &cf->duration_ms);
    cf->duration_ms = std::clamp(cf->duration_ms, 1, 9999);

    // ── 帧方向 ────────────────────────────────────────────────────────
    ImGui::Checkbox("水平反向##fflipx", &cf->flip_x);

    // ── 判定盒 ────────────────────────────────────────────────────────
    drawDevSectionTitle("判定盒");
    const char* kBoxShort[3] = {"受击", "攻击", "推挤"};
    for (int bi = 0; bi < 3; ++bi)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kBoxColor[bi]);
        if (ImGui::Button(kBoxShort[bi],  ImVec2(55, 0)))
        {
            m_activeBoxType = (BoxType)bi;
            m_anchorMode    = false;
            // 添加新盒
            FrameBox nb;
            nb.type = (BoxType)bi;
            nb.x = (float)(-cf->sw / 4);
            nb.y = (float)(-cf->sh / 2);
            nb.w = (float)(cf->sw / 2);
            nb.h = (float)(cf->sh);
            cf->boxes.push_back(nb);
            m_selBoxIdx = (int)cf->boxes.size() - 1;
        }
        ImGui::PopStyleColor();
        if (bi < 2) ImGui::SameLine();
    }
    ImGui::Text("活跃类型：%s", kBoxTypeNames[(int)m_activeBoxType]);

    for (int bi = 0; bi < (int)cf->boxes.size(); ++bi)
    {
        FrameBox &bx = cf->boxes[bi];
        ImGui::PushID(bi);
        bool isSel = (bi == m_selBoxIdx);
        if (isSel) ImGui::PushStyleColor(ImGuiCol_Header, kBoxColor[(int)bx.type]);
        char lbl[32]; snprintf(lbl, sizeof(lbl), "[%s] ##bx%d", kBoxShort[(int)bx.type], bi);
        if (ImGui::CollapsingHeader(lbl, isSel ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        {
            m_selBoxIdx = bi;
            ImGui::SetNextItemWidth(55); ImGui::InputFloat("x##bxx", &bx.x, 0, 0, "%.0f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputFloat("y##bxy", &bx.y, 0, 0, "%.0f");
            ImGui::SetNextItemWidth(55); ImGui::InputFloat("w##bxw", &bx.w, 0, 0, "%.0f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputFloat("h##bxh", &bx.h, 0, 0, "%.0f");
            bx.w = std::max(1.0f, bx.w);
            bx.h = std::max(1.0f, bx.h);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
            if (ImGui::Button("删除##bxdel", ImVec2(-1, 0)))
            {
                cf->boxes.erase(cf->boxes.begin() + bi);
                m_selBoxIdx = std::min(m_selBoxIdx, (int)cf->boxes.size() - 1);
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (isSel) ImGui::PopStyleColor();
                return;
            }
            ImGui::PopStyleColor();
        }
        if (isSel) ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // ── 帧事件 ────────────────────────────────────────────────────────
    drawDevSectionTitle("帧事件");
    for (int ei = 0; ei < (int)cf->events.size(); ++ei)
    {
        ImGui::PushID(ei);
        ImGui::InputText("##fevi", cf->events[ei].name, sizeof(FrameEvent::name));
        ImGui::SameLine();
        if (ImGui::Button("X##evdel", ImVec2(24, 0)))
        {
            cf->events.erase(cf->events.begin() + ei);
            ImGui::PopID();
            return;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("+ 事件", ImVec2(-1, 0)))
    {
        cf->events.emplace_back();
        std::strncpy(cf->events.back().name, "play_se_swing", 63);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  底部时间轴 + 动画预览
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderPreview()
{
    auto *a = selAction();
    if (!a || a->frames.empty())
    {
        ImGui::TextDisabled("无动作或无帧 — 请先提取帧数据");
        return;
    }

    drawDevSectionTitle("动画预览与时间轴");

    // 预览控制
    ImGui::SetNextItemWidth(60);
    ImGui::Checkbox("播放##pvp", &m_previewPlay);
    ImGui::SameLine();
    if (ImGui::Button("|<##pvr")) m_previewFrame = 0, m_previewTimer = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("<##pvprev") && m_previewFrame > 0) --m_previewFrame;
    ImGui::SameLine();
    if (ImGui::Button(">##pvnext") && m_previewFrame < (int)a->frames.size() - 1) ++m_previewFrame;
    ImGui::SameLine();
    ImGui::Text("帧: %d / %d", m_previewFrame, (int)a->frames.size() - 1);

    // 推进预览计时
    if (m_previewPlay)
    {
        m_previewTimer += ImGui::GetIO().DeltaTime * 1000.0f; // 毫秒
        float dur = (float)a->frames[m_previewFrame].duration_ms;
        if (m_previewTimer >= dur)
        {
            m_previewTimer -= dur;
            m_previewFrame = (m_previewFrame + 1) % (int)a->frames.size();
            if (!a->is_loop && m_previewFrame == 0)
            {
                m_previewFrame = (int)a->frames.size() - 1;
                m_previewPlay  = false;
            }
        }
    }

    // 预览小窗（固定 100×100，纹理缩放）
    const float pvSize = 100.0f;
    ImGui::SameLine(0, 12);
    if (m_glTex && m_previewFrame < (int)a->frames.size())
    {
        const FrameData &pf = a->frames[m_previewFrame];
        float u0 = pf.sx / m_texW,        v0 = pf.sy / m_texH;
        float u1 = (pf.sx + pf.sw) / m_texW, v1 = (pf.sy + pf.sh) / m_texH;
        if (pf.flip_x)
            std::swap(u0, u1);
        float aspect = (pf.sh > 0) ? (float)pf.sw / pf.sh : 1.0f;
        float pvW = pvSize * aspect;
        float pvH = pvSize;
        ImGui::Image((ImTextureID)(intptr_t)m_glTex, {pvW, pvH}, {u0, v0}, {u1, v1});
    }
    else
    {
        ImGui::Dummy({pvSize, pvSize});
    }

    ImGui::SameLine(0, 12);

    // 时间轴（可点击跳转帧）
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tlStart = ImGui::GetCursorScreenPos();
    float  tlH     = 40.0f;
    float  tlW     = ImGui::GetContentRegionAvail().x - 4.0f;

    // 计算每帧宽度（按 duration_ms 比例）
    int totalMs = 0;
    for (auto &fr : a->frames) totalMs += fr.duration_ms;
    if (totalMs == 0) totalMs = 1;

    float curX = tlStart.x;

    // ── 预计算每帧左边界，用于拖拽插入位置计算 ──────────────────────────
    std::vector<float> frameX(a->frames.size() + 1);
    {
        float cx = tlStart.x;
        for (int fi = 0; fi < (int)a->frames.size(); ++fi)
        {
            frameX[fi] = cx;
            cx += tlW * (float)a->frames[fi].duration_ms / totalMs;
        }
        frameX[a->frames.size()] = cx;
    }

    // ── 鼠标释放时执行帧重排 ──────────────────────────────────────────────
    if (m_tlDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        float mx = ImGui::GetIO().MousePos.x;
        // 找鼠标最近的 gap（0=最前, N=最后）
        int insertIdx = 0;
        float bestDist = std::abs(mx - frameX[0]);
        for (int i = 1; i <= (int)a->frames.size(); ++i)
        {
            float d = std::abs(mx - frameX[i]);
            if (d < bestDist) { bestDist = d; insertIdx = i; }
        }
        // 非 no-op（不是放回原位）
        if (insertIdx != m_tlDragSrcIdx && insertIdx != m_tlDragSrcIdx + 1)
        {
            auto frame = a->frames[m_tlDragSrcIdx];
            a->frames.erase(a->frames.begin() + m_tlDragSrcIdx);
            int dst = insertIdx;
            if (insertIdx > m_tlDragSrcIdx) dst--;
            a->frames.insert(a->frames.begin() + dst, frame);
            m_selFrame    = dst;
            m_previewFrame = std::clamp(m_previewFrame, 0, (int)a->frames.size() - 1);
        }
        m_tlDragging   = false;
        m_tlDragSrcIdx = -1;
        m_tlInsertIdx  = -1;
        // 重排后重新计算 frameX（下方插入指示线不会再用到，但保持一致）
        {
            float cx = tlStart.x;
            for (int fi = 0; fi < (int)a->frames.size(); ++fi)
            {
                frameX[fi] = cx;
                cx += tlW * (float)a->frames[fi].duration_ms / totalMs;
            }
            if (!a->frames.empty()) frameX[a->frames.size()] = cx;
        }
    }

    for (int fi = 0; fi < (int)a->frames.size(); ++fi)
    {
        float fW = tlW * (float)a->frames[fi].duration_ms / totalMs;
        ImVec2 ftl = {curX, tlStart.y};
        ImVec2 fbr = {curX + fW - 1.0f, tlStart.y + tlH};

        bool isSel  = (fi == m_selFrame);
        bool isPrev = (fi == m_previewFrame);
        bool isDrag = (m_tlDragging && fi == m_tlDragSrcIdx);
        ImU32 bg = isDrag ? IM_COL32(200, 120, 50, 140)
                 : isSel  ? IM_COL32(80, 160, 255, 200)
                 : isPrev ? IM_COL32(255, 200, 60, 180)
                          : IM_COL32(60, 70, 90, 200);
        dl->AddRectFilled(ftl, fbr, bg, 2.0f);
        dl->AddRect(ftl, fbr, IM_COL32(120, 130, 160, 150));

        // 帧缩略图
        if (m_glTex && fW > 12.0f)
        {
            const FrameData &fr = a->frames[fi];
            float u0 = fr.sx / m_texW, v0 = fr.sy / m_texH;
            float u1 = (fr.sx+fr.sw)/m_texW, v1 = (fr.sy+fr.sh)/m_texH;
            if (fr.flip_x)
                std::swap(u0, u1);
            float thumbW = std::min(fW - 2.0f, tlH);
            float thumbH = tlH - 2.0f;
            dl->AddImage((ImTextureID)(intptr_t)m_glTex,
                {ftl.x + 1, ftl.y + 1}, {ftl.x + 1 + thumbW, ftl.y + 1 + thumbH},
                {u0, v0}, {u1, v1});
        }

        // 帧序号
        if (fW > 20.0f)
        {
            char idx[8]; snprintf(idx, sizeof(idx), "%d", fi);
            dl->AddText({ftl.x + 2, ftl.y + 2}, IM_COL32(255,255,255,220), idx);
        }

        // 点击/拖拽检测
        ImGui::SetCursorScreenPos(ftl);
        ImGui::InvisibleButton(("##tl" + std::to_string(fi)).c_str(), {fW - 1.0f, tlH});
        if (ImGui::IsItemClicked() && !m_tlDragging)
        {
            m_selFrame    = fi;
            m_previewFrame = fi;
            m_previewTimer = 0.0f;
        }
        // 按住后移动超过 5px 进入拖拽
        if (!m_tlDragging && ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f))
        {
            m_tlDragging   = true;
            m_tlDragSrcIdx = fi;
        }

        curX += fW;
    }

    // ── 拖拽中：绘制插入位置指示线 ────────────────────────────────────────
    if (m_tlDragging)
    {
        float mx = ImGui::GetIO().MousePos.x;
        m_tlInsertIdx = 0;
        float bestDist = std::abs(mx - frameX[0]);
        for (int i = 1; i <= (int)a->frames.size(); ++i)
        {
            float d = std::abs(mx - frameX[i]);
            if (d < bestDist) { bestDist = d; m_tlInsertIdx = i; }
        }
        // 只在有效插入位置（非 no-op）时绘制指示线
        if (m_tlInsertIdx != m_tlDragSrcIdx && m_tlInsertIdx != m_tlDragSrcIdx + 1)
        {
            float ix = frameX[m_tlInsertIdx];
            dl->AddLine({ix, tlStart.y - 3}, {ix, tlStart.y + tlH + 3},
                IM_COL32(255, 200, 50, 255), 2.5f);
            // 顶部小三角提示
            dl->AddTriangleFilled(
                {ix - 5, tlStart.y - 3}, {ix + 5, tlStart.y - 3}, {ix, tlStart.y + 5},
                IM_COL32(255, 200, 50, 255));
        }
    }
    ImGui::SetCursorScreenPos({tlStart.x, tlStart.y + tlH + 2.0f});
}

// ─────────────────────────────────────────────────────────────────────────────
//  状态栏
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderStatusBar()
{
    const auto* cf = selFrame();
    char info[256] = {};
    if (cf)
        snprintf(info, sizeof(info), "帧: sx=%d sy=%d sw=%d sh=%d  锚点:(%d,%d)  反向:%s  盒:%d  |  %s",
            cf->sx, cf->sy, cf->sw, cf->sh, cf->anchor_x, cf->anchor_y,
            cf->flip_x ? "Y" : "N", (int)cf->boxes.size(), m_statusMsg);
    else
        snprintf(info, sizeof(info), "纹理: %.0fx%.0f  |  %s",
            m_texW, m_texH, m_statusMsg);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
    ImGui::BeginChild("##fe_status", ImVec2(0, 20), ImGuiChildFlags_None);
    ImGui::TextUnformatted(info);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ─────────────────────────────────────────────────────────────────────────────
//  纹理加载
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::loadTexture(engine::resource::ResourceManager &resMgr)
{
    m_glTex = resMgr.getGLTexture(m_texturePath);
    glm::vec2 sz = resMgr.getTextureSize(m_texturePath);
    m_texW = sz.x;
    m_texH = sz.y;

    if (m_glTex)
    {
        // 切换到最近邻采样，保持像素画清晰
        glBindTexture(GL_TEXTURE_2D, m_glTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_pixels.clear(); // 像素缓存失效，下次魔棒时重新拉取
        snprintf(m_statusMsg, sizeof(m_statusMsg),
            "纹理加载成功: %.0fx%.0f", m_texW, m_texH);
        spdlog::info("[FrameEditor] 纹理 {} 加载成功 {}x{}",
            m_texturePath, (int)m_texW, (int)m_texH);
    }
    else
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "纹理加载失败: %s", m_texturePath);
        spdlog::warn("[FrameEditor] 纹理加载失败: {}", m_texturePath);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  CPU 像素缓存（用于魔棒 BFS）
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::fetchPixels()
{
    // 直接从磁盘加载图像文件到 CPU 缓冲，避免依赖 glGetTexImage
    if (m_texturePath[0] == '\0') return;

    SDL_Surface *surf = IMG_Load(m_texturePath);
    if (!surf)
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "魔棒：图像加载失败 %s", SDL_GetError());
        spdlog::warn("[FrameEditor] fetchPixels 失败: {}", SDL_GetError());
        return;
    }

    // 转为 RGBA8 格式
    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba) return;

    const int w = rgba->w, h = rgba->h;
    m_pixels.resize((size_t)(w * h * 4));
    SDL_LockSurface(rgba);
    std::memcpy(m_pixels.data(), rgba->pixels, (size_t)(w * h * 4));
    SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);

    spdlog::info("[FrameEditor] 已通过 SDL_image 拉取像素 {}x{}", w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
//  魔棒 BFS
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::magicWandAt(int mx, int my)
{
    if (m_pixels.empty()) return;
    const int W = (int)m_texW, H = (int)m_texH;
    if (mx < 0 || mx >= W || my < 0 || my >= H) return;

    auto* a = selAction();
    if (!a) { snprintf(m_statusMsg, sizeof(m_statusMsg), "魔棒：请先选择一个动作"); return; }

    // 读取点击像素（alpha < 16 视为背景）
    auto getA = [&](int x, int y) -> uint8_t {
        return m_pixels[((size_t)(y * W + x)) * 4 + 3];
    };
    if (getA(mx, my) < 16)
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "魔棒：点击了背景（alpha=0），请点击角色像素");
        return;
    }

    // BFS 找连通非透明像素
    const size_t total = (size_t)(W * H);
    std::vector<bool> visited(total, false);
    std::queue<std::pair<int,int>> q;
    q.push({mx, my});
    visited[(size_t)(my * W + mx)] = true;

    int minX = mx, maxX = mx, minY = my, maxY = my;

    const int dx[] = {1, -1, 0, 0};
    const int dy[] = {0,  0, 1, -1};

    int iters = 0;
    const int kMaxIters = W * H; // 安全上限

    while (!q.empty() && iters++ < kMaxIters)
    {
        auto [cx, cy] = q.front(); q.pop();
        minX = std::min(minX, cx); maxX = std::max(maxX, cx);
        minY = std::min(minY, cy); maxY = std::max(maxY, cy);

        for (int d = 0; d < 4; ++d)
        {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            size_t idx = (size_t)(ny * W + nx);
            if (visited[idx]) continue;
            if (getA(nx, ny) < 16) continue;
            visited[idx] = true;
            q.push({nx, ny});
        }
    }

    // 增加 2px 边距
    minX = std::max(0, minX - 2);
    minY = std::max(0, minY - 2);
    maxX = std::min(W - 1, maxX + 2);
    maxY = std::min(H - 1, maxY + 2);

    int sw = maxX - minX + 1;
    int sh = maxY - minY + 1;
    if (sw <= 0 || sh <= 0) return;

    addFrameFromRect(minX, minY, sw, sh);
    snprintf(m_statusMsg, sizeof(m_statusMsg),
        "魔棒：自动提取帧 (%d,%d)+%dx%d", minX, minY, sw, sh);
}

// ─────────────────────────────────────────────────────────────────────────────
//  帧操作
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::addFrameFromRect(int sx, int sy, int sw, int sh)
{
    auto *a = selAction();
    if (!a)
    {
        // 尝试自动选第一个动作
        if (!m_actions.empty())
        {
            m_selAction = 0;
            a = &m_actions[0];
        }
        else
        {
            snprintf(m_statusMsg, sizeof(m_statusMsg), "请先选择或新建一个动作");
            return;
        }
    }

    FrameData fd;
    fd.sx = sx; fd.sy = sy; fd.sw = sw; fd.sh = sh;
    fd.anchor_x = sw / 2;
    fd.anchor_y = sh;     // 默认锚点在底部中心
    fd.flip_x = false;
    a->frames.push_back(fd);
    m_selFrame = (int)a->frames.size() - 1;
}

void FrameEditor::confirmDragFrame()
{
    int sx = (int)std::min(m_dragStartTex.x, m_dragEndTex.x);
    int sy = (int)std::min(m_dragStartTex.y, m_dragEndTex.y);
    int sw = (int)std::abs(m_dragEndTex.x - m_dragStartTex.x);
    int sh = (int)std::abs(m_dragEndTex.y - m_dragStartTex.y);
    if (sw < 2 || sh < 2) return; // 太小则忽略（点击非拖拽）
    addFrameFromRect(sx, sy, sw, sh);
    snprintf(m_statusMsg, sizeof(m_statusMsg),
        "手动提取帧 (%d,%d)+%dx%d", sx, sy, sw, sh);
}

// ─────────────────────────────────────────────────────────────────────────────
//  坐标转换
// ─────────────────────────────────────────────────────────────────────────────
ImVec2 FrameEditor::texToScreen(float tx, float ty) const
{
    return {
        m_cvPos.x + (tx - m_origin.x) * m_zoom,
        m_cvPos.y + (ty - m_origin.y) * m_zoom
    };
}

ImVec2 FrameEditor::screenToTex(float sx, float sy) const
{
    return {
        m_origin.x + (sx - m_cvPos.x) / m_zoom,
        m_origin.y + (sy - m_cvPos.y) / m_zoom
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON 存储
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::saveJSON()
{
    nlohmann::json root;
    root["texture"] = m_texturePath;
    root["actions"] = nlohmann::json::array();

    for (const auto &act : m_actions)
    {
        nlohmann::json ja;
        ja["name"]    = act.name;
        ja["is_loop"] = act.is_loop;
        ja["frames"]  = nlohmann::json::array();

        for (const auto &fr : act.frames)
        {
            nlohmann::json jf;
            jf["sx"] = fr.sx; jf["sy"] = fr.sy;
            jf["sw"] = fr.sw; jf["sh"] = fr.sh;
            jf["anchor_x"] = fr.anchor_x;
            jf["anchor_y"] = fr.anchor_y;
            jf["duration_ms"] = fr.duration_ms;
            jf["flip_x"] = fr.flip_x;

            jf["boxes"] = nlohmann::json::array();
            for (const auto &bx : fr.boxes)
            {
                nlohmann::json jb;
                jb["type"] = (int)bx.type;
                jb["x"] = bx.x; jb["y"] = bx.y;
                jb["w"] = bx.w; jb["h"] = bx.h;
                jf["boxes"].push_back(jb);
            }

            jf["events"] = nlohmann::json::array();
            for (const auto &ev : fr.events)
                jf["events"].push_back(ev.name);

            ja["frames"].push_back(jf);
        }
        root["actions"].push_back(ja);
    }

    std::ofstream ofs(m_savePath);
    if (ofs)
    {
        ofs << root.dump(2);
        snprintf(m_statusMsg, sizeof(m_statusMsg), "已保存: %s", m_savePath);
        spdlog::info("[FrameEditor] JSON 保存到 {}", m_savePath);
        m_justSaved = true;
    }
    else
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "保存失败！路径: %s", m_savePath);
        spdlog::error("[FrameEditor] JSON 保存失败: {}", m_savePath);
    }
}

void FrameEditor::loadJSON()
{
    std::ifstream ifs(m_savePath);
    if (!ifs)
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "文件不存在: %s", m_savePath);
        return;
    }

    nlohmann::json root;
    try { ifs >> root; }
    catch (...)
    {
        snprintf(m_statusMsg, sizeof(m_statusMsg), "JSON 解析失败");
        return;
    }

    if (root.contains("texture"))
    {
        std::string tp = root["texture"].get<std::string>();
        std::strncpy(m_texturePath, tp.c_str(), sizeof(m_texturePath) - 1);
    }

    m_actions.clear();
    m_selAction = -1; m_selFrame = -1;

    for (const auto &ja : root.value("actions", nlohmann::json::array()))
    {
        ActionData act;
        std::strncpy(act.name, ja.value("name", "Action").c_str(), 63);
        act.is_loop = ja.value("is_loop", true);

        for (const auto &jf : ja.value("frames", nlohmann::json::array()))
        {
            FrameData fd;
            fd.sx = jf.value("sx", 0); fd.sy = jf.value("sy", 0);
            fd.sw = jf.value("sw", 64); fd.sh = jf.value("sh", 64);
            fd.anchor_x   = jf.value("anchor_x", fd.sw / 2);
            fd.anchor_y   = jf.value("anchor_y", fd.sh);
            fd.duration_ms = jf.value("duration_ms", 100);
            fd.flip_x = jf.value("flip_x", false);

            for (const auto &jb : jf.value("boxes", nlohmann::json::array()))
            {
                FrameBox bx;
                bx.type = (BoxType)jb.value("type", 0);
                bx.x = jb.value("x", 0.0f); bx.y = jb.value("y", 0.0f);
                bx.w = jb.value("w", 32.0f); bx.h = jb.value("h", 32.0f);
                fd.boxes.push_back(bx);
            }
            for (const auto &ev : jf.value("events", nlohmann::json::array()))
            {
                FrameEvent fe;
                std::strncpy(fe.name, ev.get<std::string>().c_str(), sizeof(FrameEvent::name)-1);
                fd.events.push_back(fe);
            }
            act.frames.push_back(fd);
        }
        m_actions.push_back(std::move(act));
    }

    if (!m_actions.empty()) m_selAction = 0;
    ensureDefaultActions();
    snprintf(m_statusMsg, sizeof(m_statusMsg), "已载入: %s (%d 个动作)",
        m_savePath, (int)m_actions.size());
}

// ─────────────────────────────────────────────────────────────────────────────
//  从指定路径加载 JSON（不改变 m_savePath）
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::loadJSONFrom(const std::string &path)
{
    std::strncpy(m_savePath, path.c_str(), sizeof(m_savePath) - 1);
    loadJSON();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ensure IDLE / MOVE 动作存在（保护动作）
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::ensureDefaultActions()
{
    auto hasAction = [&](const char* name) {
        for (auto &a : m_actions)
            if (std::strcmp(a.name, name) == 0) return true;
        return false;
    };
    // 插入到最前面（若不存在）
    if (!hasAction("MOVE"))
    {
        ActionData mv;
        std::strncpy(mv.name, "MOVE", 63);
        mv.is_loop = true;
        m_actions.insert(m_actions.begin(), mv);
        if (m_selAction >= 0) m_selAction++;
    }
    if (!hasAction("IDLE"))
    {
        ActionData idle;
        std::strncpy(idle.name, "IDLE", 63);
        idle.is_loop = true;
        m_actions.insert(m_actions.begin(), idle);
        if (m_selAction >= 0) m_selAction++;
    }
    // 确保选中有效
    if (m_selAction < 0 && !m_actions.empty()) m_selAction = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  扫描 Characters 目录下的 JSON 文件
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::scanJsonFiles()
{
    m_jsonFiles.clear();
    namespace fs = std::filesystem;
    const fs::path dir("assets/textures/Characters");
    if (!fs::exists(dir)) return;

    for (const auto &entry : fs::directory_iterator(dir))
    {
        const auto& p = entry.path();
        // 排除 *.sm.json（状态机文件），只显示帧动画 JSON
        if (entry.is_regular_file() &&
            p.extension() == ".json" &&
            p.stem().extension() != ".sm")
        {
            JsonEntry je;
            je.path        = p.string();
            je.displayName = p.stem().string();
            m_jsonFiles.push_back(std::move(je));
        }
    }
    // 按名称排序
    std::sort(m_jsonFiles.begin(), m_jsonFiles.end(),
        [](const JsonEntry &a, const JsonEntry &b){ return a.displayName < b.displayName; });
}

// ─────────────────────────────────────────────────────────────────────────────
//  启动选择页
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderLauncher(engine::resource::ResourceManager &resMgr)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f - 280.0f,
                             io.DisplaySize.y * 0.5f - 220.0f},
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize({560.0f, 440.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);

    if (!ImGui::Begin("帧编辑器 — 选择项目##felauncher", &m_open,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        return;
    }

    drawDevSectionTitle("项目选择");
    ImGui::TextUnformatted("选择已有动画 JSON，或在角色资源目录下新建一个动作文件。");

    // 已有文件列表
    if (m_jsonFiles.empty())
    {
        ImGui::TextDisabled("  (assets/textures/Characters/ 下暂无 .json 文件)");
    }
    else
    {
        ImGui::BeginChild("##launcher_list", ImVec2(0, 300), ImGuiChildFlags_Borders);
        for (const auto &je : m_jsonFiles)
        {
            ImGui::PushID(je.path.c_str());

            // 图标 + 名称按钮
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.25f, 0.45f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.40f, 0.65f, 1.0f));
            std::string label = std::string("[  ] ") + je.displayName + "##fe_je";
            if (ImGui::Button(label.c_str(), ImVec2(-1, 28)))
            {
                loadJSONFrom(je.path);
                // 加载完成：确保纹理也加载
                if (m_texturePath[0] != '\0')
                {
                    m_glTex = 0; m_pixels.clear();
                    loadTexture(resMgr);
                }
                m_showLauncher = false;
                m_firstOpen    = true; // 进入编辑器时重新居中
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", je.path.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    drawDevSectionTitle("操作");

    // 新建按钮
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.18f, 1.0f));
    if (ImGui::Button("+ 新建动画文件", ImVec2(180, 32)))
    {
        // 弹出输入新文件名
        ImGui::OpenPopup("##new_json_name");
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("将在 assets/textures/Characters/ 下创建");

    // 新建弹窗
    static char s_newName[128] = "new_animation";
    if (ImGui::BeginPopup("##new_json_name"))
    {
        ImGui::TextUnformatted("新文件名（不含扩展名）：");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##newnm", s_newName, sizeof(s_newName));
        ImGui::SameLine();
        if (ImGui::Button("创建##nnjb") && s_newName[0] != '\0')
        {
            // 构造路径
            std::string newPath = std::string("assets/textures/Characters/") + s_newName + ".json";
            std::strncpy(m_savePath, newPath.c_str(), sizeof(m_savePath) - 1);
            // 清除旧数据，预置默认动作
            m_actions.clear();
            m_selAction = -1; m_selFrame = -1;
            ensureDefaultActions();
            // 选中 IDLE
            m_selAction = 0;
            m_showLauncher = false;
            m_firstOpen    = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (ImGui::Button("关闭##felclose")) m_open = false;

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  网格切割叠加层
// ─────────────────────────────────────────────────────────────────────────────
void FrameEditor::renderGridCutOverlay()
{
    ImDrawList *dl   = ImGui::GetWindowDrawList();
    const ImVec2 p0  = m_cvPos;
    const ImVec2 p1  = {p0.x + m_cvSize.x, p0.y + m_cvSize.y};
    ImVec2 mousePos  = ImGui::GetIO().MousePos;

    // 步长 = 帧宽 + 间距
    const int stepX = m_gcFrameW + m_gcSpacingX;
    const int stepY = m_gcFrameH + m_gcSpacingY;
    if (stepX <= 0 || stepY <= 0) return;

    // 纹理范围内的列行数
    const int cols = (m_texW > 0 && stepX > 0)
        ? (int)((m_texW  - m_gcOffsetX + stepX - 1) / stepX) : 0;
    const int rows = (m_texH > 0 && stepY > 0)
        ? (int)((m_texH - m_gcOffsetY + stepY - 1) / stepY) : 0;

    // 计算鼠标悬停的格子
    ImVec2 tMouse = screenToTex(mousePos.x, mousePos.y);
    m_gcHoverCol = -1; m_gcHoverRow = -1;
    // 检查鼠标是否在画布范围内
    bool mouseInCanvas = (mousePos.x >= p0.x && mousePos.x < p1.x &&
                          mousePos.y >= p0.y && mousePos.y < p1.y);
    if (mouseInCanvas)
    {
        int col = (int)((tMouse.x - m_gcOffsetX) / stepX);
        int row = (int)((tMouse.y - m_gcOffsetY) / stepY);
        float cellX = m_gcOffsetX + col * stepX;
        float cellY = m_gcOffsetY + row * stepY;
        if (col >= 0 && col < cols && row >= 0 && row < rows
            && tMouse.x >= cellX && tMouse.x < cellX + m_gcFrameW
            && tMouse.y >= cellY && tMouse.y < cellY + m_gcFrameH)
        {
            m_gcHoverCol = col;
            m_gcHoverRow = row;
        }
    }

    dl->PushClipRect(p0, p1, true);

    // 绘制所有网格线
    for (int col = 0; col <= cols; ++col)
    {
        float tx = (float)(m_gcOffsetX + col * stepX);
        ImVec2 sa = texToScreen(tx, (float)m_gcOffsetY);
        ImVec2 sb = texToScreen(tx, (float)(m_gcOffsetY + rows * stepY));
        dl->AddLine(sa, sb, IM_COL32(80, 200, 255, 60));
    }
    for (int row = 0; row <= rows; ++row)
    {
        float ty = (float)(m_gcOffsetY + row * stepY);
        ImVec2 sa = texToScreen((float)m_gcOffsetX, ty);
        ImVec2 sb = texToScreen((float)(m_gcOffsetX + cols * stepX), ty);
        dl->AddLine(sa, sb, IM_COL32(80, 200, 255, 60));
    }

    // 悬停格子高亮
    auto* act = selAction();
    if (m_gcHoverCol >= 0 && m_gcHoverRow >= 0)
    {
        float hx = (float)(m_gcOffsetX + m_gcHoverCol * stepX);
        float hy = (float)(m_gcOffsetY + m_gcHoverRow * stepY);
        ImVec2 hs = texToScreen(hx,              hy);
        ImVec2 he = texToScreen(hx + m_gcFrameW, hy + m_gcFrameH);
        dl->AddRectFilled(hs, he, IM_COL32(80, 220, 255, 50));
        dl->AddRect(hs, he, IM_COL32(80, 220, 255, 220), 0.0f, 0, 2.0f);

        // 提示文字
        char tip[64];
        snprintf(tip, sizeof(tip), "[%d, %d]  (%d,%d)+%dx%d",
            m_gcHoverCol, m_gcHoverRow,
            (int)hx, (int)hy, m_gcFrameW, m_gcFrameH);
        dl->AddText({hs.x + 2, hs.y + 2}, IM_COL32(255, 255, 255, 220), tip);

        // 左键单击 = 添加该格子为帧
        if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::GetIO().KeyAlt && !m_panning)
        {
            addFrameFromRect((int)hx, (int)hy, m_gcFrameW, m_gcFrameH);
            snprintf(m_statusMsg, sizeof(m_statusMsg),
                "网格提取帧 [%d,%d] (%d,%d)+%dx%d",
                m_gcHoverCol, m_gcHoverRow, (int)hx, (int)hy, m_gcFrameW, m_gcFrameH);
        }

        // Shift+左键 = 批量添加整行
        if (act && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && ImGui::GetIO().KeyShift && !m_panning)
        {
            for (int c = 0; c < cols; ++c)
            {
                float fx = (float)(m_gcOffsetX + c * stepX);
                addFrameFromRect((int)fx, (int)hy, m_gcFrameW, m_gcFrameH);
            }
            snprintf(m_statusMsg, sizeof(m_statusMsg),
                "网格批量提取第 %d 行（%d 帧）", m_gcHoverRow, cols);
        }
    }

    dl->PopClipRect();

    // 右侧配置浮动面板
    {
        ImVec2 panelPos = {p1.x - 180.0f, p0.y + 4.0f};
        ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize({175.0f, 190.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::Begin("##gcpanel", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("网格切割设置");
        ImGui::Separator();
        ImGui::SetNextItemWidth(70); ImGui::InputInt("帧宽##gcfw", &m_gcFrameW, 1);
        ImGui::SetNextItemWidth(70); ImGui::InputInt("帧高##gcfh", &m_gcFrameH, 1);
        ImGui::SetNextItemWidth(70); ImGui::InputInt("X间距##gcgx", &m_gcSpacingX, 1);
        ImGui::SetNextItemWidth(70); ImGui::InputInt("Y间距##gcgy", &m_gcSpacingY, 1);
        ImGui::SetNextItemWidth(70); ImGui::InputInt("偏移X##gcpx", &m_gcOffsetX, 1);
        ImGui::SetNextItemWidth(70); ImGui::InputInt("偏移Y##gcpy", &m_gcOffsetY, 1);
        m_gcFrameW   = std::max(1, m_gcFrameW);
        m_gcFrameH   = std::max(1, m_gcFrameH);
        m_gcSpacingX = std::max(0, m_gcSpacingX);
        m_gcSpacingY = std::max(0, m_gcSpacingY);
        ImGui::Separator();
        ImGui::TextDisabled("左键: 添加单格");
        ImGui::TextDisabled("Shift+左键: 添加整行");
        ImGui::End();
    }
}

} // namespace game::scene
