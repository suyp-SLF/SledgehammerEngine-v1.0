#include "weather_system.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace game::weather
{
    WeatherSystem::WeatherSystem()
        : m_rng(0xDEADBEEF12345678ULL)
    {
        m_autoChangeTimer = autoChangePeriod;
    }

    // ──────────────────────────────────────────────
    // RNG
    // ──────────────────────────────────────────────
    uint64_t WeatherSystem::nextRand()
    {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 7;
        m_rng ^= m_rng << 17;
        return m_rng;
    }

    float WeatherSystem::randFloat()
    {
        return static_cast<float>(nextRand() & 0xFFFFFF) / static_cast<float>(0x1000000);
    }

    // ──────────────────────────────────────────────
    // 天气参数查询
    // ──────────────────────────────────────────────
    int WeatherSystem::targetParticleCount() const
    {
        switch (m_current)
        {
            case WeatherType::Clear:        return 0;
            case WeatherType::LightRain:    return 120;
            case WeatherType::MediumRain:   return 360;
            case WeatherType::HeavyRain:    return 720;
            case WeatherType::Thunderstorm: return 950;
        }
        return 0;
    }

    float WeatherSystem::particleSpeed() const
    {
        switch (m_current)
        {
            case WeatherType::LightRain:    return 340.0f;
            case WeatherType::MediumRain:   return 560.0f;
            case WeatherType::HeavyRain:    return 780.0f;
            case WeatherType::Thunderstorm: return 980.0f;
            default: return 0.0f;
        }
    }

    float WeatherSystem::particleLength() const
    {
        switch (m_current)
        {
            case WeatherType::LightRain:    return 8.0f;
            case WeatherType::MediumRain:   return 14.0f;
            case WeatherType::HeavyRain:    return 20.0f;
            case WeatherType::Thunderstorm: return 25.0f;
            default: return 0.0f;
        }
    }

    float WeatherSystem::particleAlpha() const
    {
        switch (m_current)
        {
            case WeatherType::LightRain:    return 0.35f;
            case WeatherType::MediumRain:   return 0.55f;
            case WeatherType::HeavyRain:    return 0.72f;
            case WeatherType::Thunderstorm: return 0.85f;
            default: return 0.0f;
        }
    }

    // 天空暗化 alpha（阴天效果）
    float WeatherSystem::getDimAlpha() const
    {
        switch (m_current)
        {
            case WeatherType::Clear:        return 0.0f;
            case WeatherType::LightRain:    return 0.12f;
            case WeatherType::MediumRain:   return 0.25f;
            case WeatherType::HeavyRain:    return 0.40f;
            case WeatherType::Thunderstorm: return 0.55f;
        }
        return 0.0f;
    }

    // ──────────────────────────────────────────────
    // 粒子生成
    // ──────────────────────────────────────────────
    void WeatherSystem::spawnParticle(float displayW, float displayH)
    {
        RainParticle p;
        p.x      = randFloat() * (displayW + 120.0f) - 60.0f;
        p.y      = randFloat() * -displayH;  // 从屏幕上方随机位置开始
        p.speed  = particleSpeed()  * (0.8f + randFloat() * 0.4f);
        p.length = particleLength() * (0.7f + randFloat() * 0.6f);
        p.alpha  = particleAlpha()  * (0.5f + randFloat() * 0.5f) * m_intensity;
        m_particles.push_back(p);
    }

    void WeatherSystem::respawnParticle(RainParticle &p, float displayW, float displayH)
    {
        p.x      = randFloat() * (displayW + 120.0f) - 60.0f;
        p.y      = -p.length - randFloat() * 30.0f;
        p.speed  = particleSpeed()  * (0.8f + randFloat() * 0.4f);
        p.length = particleLength() * (0.7f + randFloat() * 0.6f);
        p.alpha  = particleAlpha()  * (0.5f + randFloat() * 0.5f) * m_intensity;
    }

    // ──────────────────────────────────────────────
    // 公共接口
    // ──────────────────────────────────────────────
    void WeatherSystem::setWeather(WeatherType type, float transitionSec)
    {
        if (type == m_current && !m_isTransitioning) return;

        m_current            = type;
        m_transitionDuration = transitionSec;
        m_transitionTimer    = 0.0f;
        m_intensity          = 0.0f;     // 从零开始淡入
        m_isTransitioning    = true;
        m_particles.clear();             // 清除旧粒子，让新天气自然生成

        // 重置雷电计时（切换到雷雨时稍后触发）
        m_lightningFlash    = 0.0f;
        m_lightningNextTime = 3.0f + static_cast<float>(nextRand() % 8);
    }

    const char* WeatherSystem::getWeatherName(WeatherType t)
    {
        switch (t)
        {
            case WeatherType::Clear:        return "晴天";
            case WeatherType::LightRain:    return "小雨";
            case WeatherType::MediumRain:   return "中雨";
            case WeatherType::HeavyRain:    return "大雨";
            case WeatherType::Thunderstorm: return "雷雨";
        }
        return "未知";
    }

    // ──────────────────────────────────────────────
    // update()
    // ──────────────────────────────────────────────
    void WeatherSystem::update(float dt, float displayW, float displayH)
    {
        // ── 过渡淡入 ──
        if (m_isTransitioning)
        {
            m_transitionTimer += dt;
            m_intensity = std::min(1.0f, m_transitionTimer / m_transitionDuration);
            if (m_intensity >= 1.0f)
                m_isTransitioning = false;
        }

        // ── 粒子池管理 ──
        int target = targetParticleCount();
        while (static_cast<int>(m_particles.size()) < target)
            spawnParticle(displayW, displayH);

        // ── 粒子物理 ──
        for (auto &p : m_particles)
        {
            p.y += p.speed * dt;
            p.x += WIND_DX * p.speed * dt;

            // 离开屏幕 → 重新生成
            if (p.y > displayH + 40.0f ||
                p.x < -100.0f          ||
                p.x > displayW + 100.0f)
            {
                respawnParticle(p, displayW, displayH);
            }
        }

        // ── 晴天：淡出并清理粒子 ──
        if (m_current == WeatherType::Clear)
        {
            for (auto &p : m_particles)
                p.alpha -= dt * 0.8f;
            m_particles.erase(
                std::remove_if(m_particles.begin(), m_particles.end(),
                               [](const RainParticle &p){ return p.alpha <= 0.0f; }),
                m_particles.end());
        }

        // ── 雷电（仅雷雨天气）──
        if (m_current == WeatherType::Thunderstorm && m_intensity > 0.5f)
        {
            m_lightningNextTime -= dt;
            if (m_lightningNextTime <= 0.0f)
            {
                m_lightningFlash    = 0.85f + randFloat() * 0.15f;
                m_lightningNextTime = 4.0f + randFloat() * 10.0f;
            }
        }
        if (m_lightningFlash > 0.0f)
        {
            m_lightningFlash -= dt * 3.5f;
            if (m_lightningFlash < 0.0f) m_lightningFlash = 0.0f;
        }

        // ── 自动天气切换 ──
        if (autoChangePeriod > 0.0f)
        {
            m_autoChangeTimer -= dt;
            if (m_autoChangeTimer <= 0.0f)
            {
                // 随机下一种天气（带权重偏向晴天和小雨）
                uint32_t r = static_cast<uint32_t>(nextRand() % 12);
                WeatherType next;
                if      (r < 4)  next = WeatherType::Clear;
                else if (r < 7)  next = WeatherType::LightRain;
                else if (r < 9)  next = WeatherType::MediumRain;
                else if (r < 11) next = WeatherType::HeavyRain;
                else             next = WeatherType::Thunderstorm;

                setWeather(next, 4.0f);
                m_autoChangeTimer = autoChangePeriod * (0.6f + randFloat() * 0.8f);
            }
        }
    }

    // ──────────────────────────────────────────────
    // render()  —  在 ImGui::NewFrame() 后调用
    // ──────────────────────────────────────────────
    void WeatherSystem::render(float displayW, float displayH)
    {
        ImDrawList *bg = ImGui::GetBackgroundDrawList();

        // ── 天空暗化（阴云效果）──
        float dim = getDimAlpha() * m_intensity;
        if (dim > 0.0f)
        {
            bg->AddRectFilled(
                ImVec2(0.0f, 0.0f),
                ImVec2(displayW, displayH),
                IM_COL32(10, 20, 45, static_cast<int>(dim * 220)));
        }

        // ── 雨滴条纹 ──
        for (const auto &p : m_particles)
        {
            float alpha = p.alpha * m_intensity;
            if (alpha <= 0.01f) continue;

            uint8_t a  = static_cast<uint8_t>(std::min(alpha * 255.0f, 255.0f));
            ImU32  col = IM_COL32(170, 210, 255, a);

            // 斜向线段
            float dx = WIND_DX * p.length;
            float dy = p.length;
            bg->AddLine(
                ImVec2(p.x,      p.y),
                ImVec2(p.x + dx, p.y + dy),
                col, 1.0f);
        }

        // ── 闪电全屏白光 ──
        if (m_lightningFlash > 0.0f)
        {
            uint8_t fa = static_cast<uint8_t>(m_lightningFlash * 190.0f);
            bg->AddRectFilled(
                ImVec2(0.0f, 0.0f),
                ImVec2(displayW, displayH),
                IM_COL32(255, 255, 240, fa));
        }
    }

} // namespace game::weather
