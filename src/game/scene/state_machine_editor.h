#pragma once
#include "../../engine/statemachine/sm_types.h"
#include "../statemachine/sm_loader.h"
#include <string>
#include <vector>

// Forward-declare ResourceManager to avoid heavy include chain
namespace engine::resource { class ResourceManager; }

namespace game::scene {

// ─────────────────────────────────────────────────────────────────────────────
//  状态机编辑器
//  使用方式：
//    1. game_scene.cpp 在 renderUI() 中调用 m_smEditor.render()
//    2. 其他地方调用 m_smEditor.open() / m_smEditor.toggle() 打开
// ─────────────────────────────────────────────────────────────────────────────
class StateMachineEditor
{
public:
    StateMachineEditor()  = default;
    ~StateMachineEditor() = default;

    // ── 控制窗口 ──────────────────────────────────────────────────────────────
    void open();          // 打开（若已在 launcher 则保留 launcher）
    void toggle();
    /** suggestedSavePath：路径为空时设定合理的默认保存名 */
    void openWithJson(const std::string& path, const std::string& suggestedSavePath = "");
    void renderInline();  // 在当前 ImGui 上下文中内联渲染（嵌入 Tab 使用）
    bool isOpen() const { return m_open; }

    // ── 主渲染入口（每帧调用）────────────────────────────────────────────────
    void render();

    // ── 数据访问 ──────────────────────────────────────────────────────────────
    const engine::statemachine::StateMachineData& getData()      const { return m_data; }
    const std::string&                    getSavePath()  const { return m_savePath; }

    /** 若上次 render() 期间用户刚选了新文件，返回 true 并清除标志（每帧最多返回一次 true） */
    bool takeJustLoaded() { bool v = m_justLoaded; m_justLoaded = false; return v; }

    /** 若上次 renderInline 期间用户执行了保存，返回 true 并清除标志（一次性消费） */
    bool popJustSaved() { bool v = m_justSaved; m_justSaved = false; return v; }

private:
    // ── 状态数据 ──────────────────────────────────────────────────────────────
    bool m_open          = false;
    bool m_showLauncher  = true;

    engine::statemachine::StateMachineData m_data;
    std::string                    m_savePath;

    // ── 选中状态 ──────────────────────────────────────────────────────────────
    std::string m_selState;          // 当前选中的状态名
    int         m_selTransIdx = -1;  // 选中的转换条件索引
    int         m_selWinIdx   = -1;  // 选中的帧区间索引
    int         m_selRmIdx    = -1;  // 选中的根位移索引
    int         m_selEvtIdx   = -1;  // 选中的帧事件索引

    // ── 文件扫描列表 ──────────────────────────────────────────────────────────
    struct SmEntry { std::string path; std::string displayName; };
    std::vector<SmEntry> m_smFiles;

    // ── 子界面 ────────────────────────────────────────────────────────────────
    void renderLauncher();
    void renderMenuBar();
    void renderStateList(float listH);
    void renderStateDetails();
    void renderTimeline();

    // ── 辅助 ──────────────────────────────────────────────────────────────────
    void scanSmFiles();
    void newFile(const std::string& characterId);
    void loadJsonFrom(const std::string& path);
    void saveJson();

    bool m_justLoaded = false;  // 用户刚从启动页选了新文件
    bool m_justSaved  = false;  // 本帧执行了保存
    bool m_showHelp   = false;  // 是否展开文档面板

    // ── 新建状态弹窗临时缓冲区 ────────────────────────────────────────────────
    char m_newStateNameBuf[64]  = {};
    char m_newFileIdBuf[64]     = {};
    char m_customTriggerBuf[64] = {};
};

} // namespace game::scene
