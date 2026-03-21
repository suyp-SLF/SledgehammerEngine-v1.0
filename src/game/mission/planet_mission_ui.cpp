#include "planet_mission_ui.h"
#include "../../engine/world/tile_info.h"
#include "../../engine/world/chunk.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace game::mission
{
    using namespace engine::world;

    // ──────────────────────────────────────────────
    // 辅助：瓦片颜色映射
    // ──────────────────────────────────────────────
    static ImU32 tileColor(TileType t)
    {
        switch (t)
        {
        case TileType::Air:    return IM_COL32(30,  100, 200,  70);
        case TileType::Stone:  return IM_COL32(110, 110, 120, 255);
        case TileType::Dirt:   return IM_COL32(150, 105,  55, 255);
        case TileType::Grass:  return IM_COL32( 60, 185,  55, 255);
        case TileType::Wood:   return IM_COL32(130,  85,  35, 255);
        case TileType::Leaves: return IM_COL32( 35, 150,  40, 255);
        default:               return IM_COL32(  0,   0,   0,   0);
        }
    }

    // ──────────────────────────────────────────────
    // 坐标转换
    // ──────────────────────────────────────────────
    static ImVec2 tileToCanvas(ImVec2 origin, glm::ivec2 tile,
                                glm::ivec2 center, int hw, int hh, float px)
    {
        return {origin.x + (tile.x - center.x + hw) * px,
                origin.y + (tile.y - center.y + hh) * px};
    }

    static glm::ivec2 canvasToTile(ImVec2 origin, ImVec2 click,
                                    glm::ivec2 center, int hw, int hh, float px)
    {
        return {center.x - hw + static_cast<int>((click.x - origin.x) / px),
                center.y - hh + static_cast<int>((click.y - origin.y) / px)};
    }

    // ──────────────────────────────────────────────
    // phaseLabel
    // ──────────────────────────────────────────────
    const char *PlanetMissionUI::phaseLabel(MissionPhase p)
    {
        switch (p)
        {
        case MissionPhase::Planning:   return "规划路线";
        case MissionPhase::Active:     return "执行任务";
        case MissionPhase::Evacuating: return "撤离中...";
        case MissionPhase::Complete:   return "任务完成";
        }
        return "";
    }

    float PlanetMissionUI::evacProgress() const
    {
        return (EVAC_DURATION > 0.0f) ? std::min(1.0f, m_evacTimer / EVAC_DURATION) : 1.0f;
    }

    // ──────────────────────────────────────────────
    // update()
    // ──────────────────────────────────────────────
    void PlanetMissionUI::update(float dt,
                                  const glm::vec2 &playerWorldPos,
                                  ChunkManager &chunkMgr)
    {
        m_playerTile = chunkMgr.worldToTile(playerWorldPos);

        // 检测到达路线节点
        if (m_phase == MissionPhase::Active)
        {
            for (auto &wp : m_waypoints)
            {
                if (!wp.reached)
                {
                    float dx = static_cast<float>(m_playerTile.x - wp.tilePos.x);
                    float dy = static_cast<float>(m_playerTile.y - wp.tilePos.y);
                    if (dx * dx + dy * dy < 9.0f)  // 距离 < 3 格
                        wp.reached = true;
                }
            }
        }

        // 撤离倒计时
        if (m_phase == MissionPhase::Evacuating)
        {
            m_evacTimer += dt;
            if (m_evacTimer >= EVAC_DURATION)
                m_phase = MissionPhase::Complete;
        }
    }

    // ──────────────────────────────────────────────
    // render()
    // ──────────────────────────────────────────────
    void PlanetMissionUI::render(ChunkManager &chunkMgr,
                                  const glm::vec2 &playerWorldPos)
    {
        if (!showWindow) return;

        ImGuiIO &io = ImGui::GetIO();
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(CANVAS_W + 260.0f, CANVAS_H + 60.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);

        char titleBuf[64];
        std::snprintf(titleBuf, sizeof(titleBuf), " 星球任务规划  |  %s", phaseLabel(m_phase));
        int wflags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (!ImGui::Begin(titleBuf, &showWindow, wflags))
        {
            ImGui::End();
            return;
        }

        // ────────────────────────────────────────────
        // 左侧：迷你地图画布
        // ────────────────────────────────────────────
        ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        // — 背景 —
        dl->AddRectFilled(canvasOrigin,
            ImVec2(canvasOrigin.x + CANVAS_W, canvasOrigin.y + CANVAS_H),
            IM_COL32(12, 35, 70, 255));

        // — 瓦片绘制 —
        for (int dy = -HALF_H; dy <= HALF_H; ++dy)
        {
            for (int dx = -HALF_W; dx <= HALF_W; ++dx)
            {
                TileType t = chunkMgr.tileAt(m_playerTile.x + dx,
                                              m_playerTile.y + dy).type;
                if (t == TileType::Air)
                    continue;
                ImU32 col = tileColor(t);
                float px = canvasOrigin.x + (dx + HALF_W) * TILE_PX;
                float py = canvasOrigin.y + (dy + HALF_H) * TILE_PX;
                dl->AddRectFilled(ImVec2(px, py),
                                  ImVec2(px + TILE_PX, py + TILE_PX),
                                  col);
            }
        }

        // — 区块网格线（每 16 格一条）—
        {
            ImU32 gc = IM_COL32(55, 90, 135, 50);
            int coff = m_playerTile.x % Chunk::SIZE;
            for (int dx = -HALF_W - coff; dx <= HALF_W; dx += Chunk::SIZE)
            {
                float px = canvasOrigin.x + (dx + HALF_W) * TILE_PX;
                dl->AddLine(ImVec2(px, canvasOrigin.y),
                            ImVec2(px, canvasOrigin.y + CANVAS_H), gc, 1.0f);
            }
            int roff = m_playerTile.y % Chunk::SIZE;
            for (int dy = -HALF_H - roff; dy <= HALF_H; dy += Chunk::SIZE)
            {
                float py = canvasOrigin.y + (dy + HALF_H) * TILE_PX;
                dl->AddLine(ImVec2(canvasOrigin.x, py),
                            ImVec2(canvasOrigin.x + CANVAS_W, py), gc, 1.0f);
            }
        }

        // — 路线 —
        if (m_waypoints.size() >= 2)
        {
            std::vector<ImVec2> pts;
            pts.reserve(m_waypoints.size());
            for (const auto &wp : m_waypoints)
                pts.push_back(tileToCanvas(canvasOrigin, wp.tilePos,
                                            m_playerTile, HALF_W, HALF_H, TILE_PX));

            // 发光底层
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()),
                            IM_COL32(255, 200, 50, 50), 0, 8.0f);
            // 主线
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()),
                            IM_COL32(255, 225, 60, 230), 0, 2.0f);

            // 每段方向箭头
            for (size_t i = 0; i + 1 < pts.size(); ++i)
            {
                const ImVec2 &a = pts[i], &b = pts[i + 1];
                float ddx = b.x - a.x, ddy = b.y - a.y;
                float len = std::sqrt(ddx * ddx + ddy * ddy);
                if (len < 8.0f) continue;
                ddx /= len; ddy /= len;
                float nx = -ddy, ny = ddx;
                float as = 5.5f;
                ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                ImVec2 tip(mid.x + ddx * as, mid.y + ddy * as);
                ImVec2 bl(mid.x - ddx * as + nx * as * 0.6f,
                          mid.y - ddy * as + ny * as * 0.6f);
                ImVec2 br(mid.x - ddx * as - nx * as * 0.6f,
                          mid.y - ddy * as - ny * as * 0.6f);
                dl->AddTriangleFilled(tip, bl, br, IM_COL32(255, 240, 80, 220));
            }
        }

        // — 路线节点 —
        for (size_t i = 0; i < m_waypoints.size(); ++i)
        {
            const auto &wp = m_waypoints[i];
            ImVec2 pt = tileToCanvas(canvasOrigin, wp.tilePos,
                                      m_playerTile, HALF_W, HALF_H, TILE_PX);

            if (wp.isEvacPoint)
            {
                dl->AddCircleFilled(pt, 6.0f, IM_COL32(50, 230, 50, 255));
                dl->AddCircle(pt, 9.0f, IM_COL32(100, 255, 120, 200), 8, 2.0f);
                dl->AddText(ImVec2(pt.x - 4, pt.y - 18),
                            IM_COL32(60, 255, 80, 255), "[E]");
            }
            else
            {
                ImU32 col = wp.reached
                    ? IM_COL32(80, 80, 220, 160)
                    : IM_COL32(60, 200, 255, 255);
                dl->AddCircleFilled(pt, 4.5f, col);
                dl->AddCircle(pt, 6.5f, IM_COL32(200, 240, 255, 160), 12, 1.5f);
                char num[4];
                std::snprintf(num, sizeof(num), "%d", static_cast<int>(i + 1));
                dl->AddText(ImVec2(pt.x + 6.0f, pt.y - 8.0f),
                            IM_COL32(255, 255, 255, 200), num);
            }
        }

        // — 玩家标记（橙色菱形）—
        {
            ImVec2 pc = tileToCanvas(canvasOrigin, m_playerTile,
                                      m_playerTile, HALF_W, HALF_H, TILE_PX);
            float s = 5.0f;
            dl->AddQuadFilled(
                ImVec2(pc.x,     pc.y - s),
                ImVec2(pc.x + s, pc.y),
                ImVec2(pc.x,     pc.y + s),
                ImVec2(pc.x - s, pc.y),
                IM_COL32(255, 150, 30, 255));
            dl->AddCircle(pc, s + 3.0f, IM_COL32(255, 210, 80, 180), 8, 1.5f);
        }

        // — 指南针 —
        {
            float cx = canvasOrigin.x + CANVAS_W - 22.0f;
            float cy = canvasOrigin.y + 22.0f;
            dl->AddText(ImVec2(cx - 4, cy - 22), IM_COL32(255, 70, 70, 220), "N");
            dl->AddLine(ImVec2(cx, cy - 14), ImVec2(cx, cy + 14),
                        IM_COL32(180, 210, 255, 160), 1.0f);
            dl->AddLine(ImVec2(cx - 14, cy), ImVec2(cx + 14, cy),
                        IM_COL32(180, 210, 255, 160), 1.0f);
        }

        // — 边框 —
        dl->AddRect(canvasOrigin,
            ImVec2(canvasOrigin.x + CANVAS_W, canvasOrigin.y + CANVAS_H),
            IM_COL32(80, 170, 230, 180), 2.0f, 0, 2.0f);

        // ─ InvisibleButton 捕获画布点击 ─
        ImGui::InvisibleButton("##mapcanvas", ImVec2(CANVAS_W, CANVAS_H));
        bool hovered = ImGui::IsItemHovered();
        bool lclicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rclicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        if (m_phase == MissionPhase::Planning || m_phase == MissionPhase::Active)
        {
            if (lclicked)
            {
                ImVec2 mp = ImGui::GetMousePos();
                glm::ivec2 tp = canvasToTile(canvasOrigin, mp,
                                              m_playerTile, HALF_W, HALF_H, TILE_PX);
                m_waypoints.push_back({tp, false, false});
            }
            if (rclicked && !m_waypoints.empty())
                m_waypoints.pop_back();
        }
        if (hovered && (m_phase == MissionPhase::Planning || m_phase == MissionPhase::Active))
            ImGui::SetTooltip("左键 添加节点  右键 撤销最后节点");

        // ────────────────────────────────────────────
        // 右侧控制面板
        // ────────────────────────────────────────────
        float panelX = CANVAS_W + 16.0f;
        ImGui::SetCursorPos(ImVec2(panelX, 28.0f));
        ImGui::BeginGroup();

        // ── 资源情报 ──
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "资源情报");
        ImGui::Separator();
        if (!m_scan.scanned)
        {
            ImGui::TextDisabled("(尚未扫描)");
        }
        else
        {
            ImGui::Text("木材: %d 格", m_scan.woodCount);
            ImGui::Text("石材: %d 格", m_scan.stoneCount);
            ImGui::Text("泥土: %d 格", m_scan.dirtCount);
        }
        ImGui::Spacing();
        if (ImGui::Button("扫描附近资源", ImVec2(220, 0)))
        {
            m_scan = {};
            for (int dy = -HALF_H; dy <= HALF_H; ++dy)
                for (int dx = -HALF_W; dx <= HALF_W; ++dx)
                {
                    TileType t = chunkMgr.tileAt(m_playerTile.x + dx,
                                                  m_playerTile.y + dy).type;
                    if (t == TileType::Wood)  ++m_scan.woodCount;
                    if (t == TileType::Stone) ++m_scan.stoneCount;
                    if (t == TileType::Dirt)  ++m_scan.dirtCount;
                }
            m_scan.scanned = true;
        }

        // ── 路线规划 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "路线规划");
        ImGui::Text("节点数: %d", static_cast<int>(m_waypoints.size()));

        if (!m_waypoints.empty())
        {
            bool hasEvac = false;
            for (const auto &w : m_waypoints) if (w.isEvacPoint) { hasEvac = true; break; }
            if (!hasEvac)
            {
                if (ImGui::Button("标记末尾为撤离点", ImVec2(220, 0)))
                    m_waypoints.back().isEvacPoint = true;
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("清空路线", ImVec2(220, 0)))
            m_waypoints.clear();

        // ── 任务控制 ──
        ImGui::Spacing();
        ImGui::Separator();

        if (m_phase == MissionPhase::Planning)
        {
            if (ImGui::Button("开始执行任务", ImVec2(220, 36)))
                m_phase = MissionPhase::Active;
        }
        else if (m_phase == MissionPhase::Active)
        {
            int reached = 0;
            for (const auto &w : m_waypoints) if (w.reached) ++reached;
            ImGui::Text("已到达: %d / %d", reached,
                        static_cast<int>(m_waypoints.size()));
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
            if (ImGui::Button("撤离星球", ImVec2(220, 36)))
            {
                m_evacTimer = 0.0f;
                m_phase = MissionPhase::Evacuating;
            }
            ImGui::PopStyleColor();
        }
        else if (m_phase == MissionPhase::Evacuating)
        {
            float prog = evacProgress();
            char buf[32];
            std::snprintf(buf, sizeof(buf), "撤离 %.0f%%", prog * 100.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
            ImGui::ProgressBar(prog, ImVec2(220, 22), buf);
            ImGui::PopStyleColor();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                               "倒计时: %.1f 秒",
                               EVAC_DURATION - m_evacTimer);
        }
        else if (m_phase == MissionPhase::Complete)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "撤离完成！任务终结");
            ImGui::Spacing();
            if (m_scan.scanned)
                ImGui::Text("扫描数据: 木 %d  石 %d", m_scan.woodCount, m_scan.stoneCount);
            ImGui::Spacing();
            if (ImGui::Button("重新规划", ImVec2(220, 30)))
            {
                m_phase = MissionPhase::Planning;
                m_waypoints.clear();
                m_scan = {};
                m_evacTimer = 0.0f;
            }
        }

        // ── 图例 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "图例");

        struct LegendItem { ImU32 col; const char *name; };
        static const LegendItem kLegend[] = {
            { IM_COL32( 60, 185,  55, 255), "草地" },
            { IM_COL32(150, 105,  55, 255), "泥土" },
            { IM_COL32(110, 110, 120, 255), "石头" },
            { IM_COL32(130,  85,  35, 255), "木材" },
            { IM_COL32( 35, 150,  40, 255), "树叶" },
        };
        ImDrawList *wdl = ImGui::GetWindowDrawList();
        for (const auto &li : kLegend)
        {
            ImVec2 sp = ImGui::GetCursorScreenPos();
            wdl->AddRectFilled(sp, ImVec2(sp.x + 12, sp.y + 12), li.col);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
            ImGui::Text("  %s", li.name);
        }

        ImGui::EndGroup();

        // ── 底部提示 ──
        ImGui::SetCursorPos(ImVec2(8.0f, CANVAS_H + 10.0f));
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 0.6f),
            "M 键打开/关闭 | 左键添加节点 | 右键撤销 | 共 %d 个节点",
            static_cast<int>(m_waypoints.size()));

        ImGui::End();

        // ── 撤离中全屏脉冲绿光 ──
        if (m_phase == MissionPhase::Evacuating)
        {
            float pulse = std::sin(m_evacTimer * 5.0f) * 0.5f + 0.5f;
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                              IM_COL32(20, 200, 50, static_cast<int>(pulse * 35)));
        }
        // ── 完成时短暂白光 ──
        if (m_phase == MissionPhase::Complete && m_evacTimer < EVAC_DURATION + 0.5f)
        {
            float fade = 1.0f - (m_evacTimer - EVAC_DURATION) / 0.5f;
            if (fade > 0.0f)
            {
                ImDrawList *bg = ImGui::GetBackgroundDrawList();
                bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                  IM_COL32(255, 255, 255,
                                           static_cast<int>(fade * 120)));
            }
        }
    }

} // namespace game::mission
