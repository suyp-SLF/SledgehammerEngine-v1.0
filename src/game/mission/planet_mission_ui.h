#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include "../../engine/world/chunk_manager.h"
#include "../inventory/inventory.h"

namespace game::mission
{
    // ──────────────────────────────────────────────
    // 任务阶段
    // ──────────────────────────────────────────────
    enum class MissionPhase : uint8_t
    {
        Planning   = 0,  // 规划路线
        Active     = 1,  // 执行任务
        Evacuating = 2,  // 撤离倒计时
        Complete   = 3,  // 完成
    };

    // 单个路线节点
    struct Waypoint
    {
        glm::ivec2 tilePos;           // 瓦片坐标
        bool       isEvacPoint = false; // 是否是撤离点
        bool       reached     = false; // 玩家是否已到达
    };

    // 资源扫描结果
    struct ResourceScan
    {
        int  woodCount  = 0;
        int  stoneCount = 0;
        int  dirtCount  = 0;
        bool scanned    = false;
    };

    // ──────────────────────────────────────────────
    // 星球任务规划 UI
    //   - 显示以玩家为中心的迷你地图
    //   - 路线点规划（左键添加、右键撤销）
    //   - 资源扫描与统计
    //   - 三阶段任务流程：规划 → 执行 → 撤离
    // ──────────────────────────────────────────────
    class PlanetMissionUI
    {
    public:
        bool showWindow = false;

        /** 每帧逻辑更新（路线到达检测、撤离计时等） */
        void update(float dt,
                    const glm::vec2 &playerWorldPos,
                    engine::world::ChunkManager &chunkMgr);

        /** 在 ImGui::NewFrame() 之后调用，绘制完整 UI */
        void render(engine::world::ChunkManager &chunkMgr,
                    const glm::vec2 &playerWorldPos);

        MissionPhase getPhase()    const { return m_phase; }
        bool         isComplete()  const { return m_phase == MissionPhase::Complete; }
        bool         isEvacuating()const { return m_phase == MissionPhase::Evacuating; }
        float        evacProgress()const;

    private:
        MissionPhase m_phase = MissionPhase::Planning;
        std::vector<Waypoint> m_waypoints;
        ResourceScan m_scan;
        glm::ivec2   m_playerTile{0, 0};

        // 撤离倒计时
        float m_evacTimer = 0.0f;
        static constexpr float EVAC_DURATION = 20.0f;

        // 迷你地图配置
        static constexpr float TILE_PX  = 3.0f;      // 每格占屏幕像素数
        static constexpr int   HALF_W   = 48;         // 水平半径（格）
        static constexpr int   HALF_H   = 36;         // 垂直半径（格）
        static constexpr float CANVAS_W = (HALF_W * 2 + 1) * TILE_PX;  // ~291 px
        static constexpr float CANVAS_H = (HALF_H * 2 + 1) * TILE_PX;  // ~219 px

        static const char *phaseLabel(MissionPhase p);
    };

} // namespace game::mission
