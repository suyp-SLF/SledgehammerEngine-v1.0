#pragma once
#include <vector>
#include <cstdint>

namespace game::weather
{
    // ──────────────────────────────────────────────
    // 天气类型
    // ──────────────────────────────────────────────
    enum class WeatherType : uint8_t
    {
        Clear        = 0,  // 晴天
        LightRain    = 1,  // 小雨
        MediumRain   = 2,  // 中雨
        HeavyRain    = 3,  // 大雨
        Thunderstorm = 4,  // 雷雨
    };

    // ──────────────────────────────────────────────
    // 单个雨滴粒子（屏幕坐标）
    // ──────────────────────────────────────────────
    struct RainParticle
    {
        float x, y;      // 屏幕坐标（像素）
        float speed;     // 垂直速度（像素/秒）
        float length;    // 条纹长度（像素）
        float alpha;     // 不透明度 0..1
    };

    // ──────────────────────────────────────────────
    // 天气系统
    //   update()  — 每帧调用（粒子物理、闪电计时等）
    //   render()  — 在 ImGui::NewFrame() 之后调用
    //               使用 GetBackgroundDrawList() 绘制在ImGui窗口之下
    // ──────────────────────────────────────────────
    class WeatherSystem
    {
    public:
        WeatherSystem();

        /** 每帧更新粒子位置、闪电计时、自动切换天气 */
        void update(float dt, float displayW, float displayH);

        /** 在 ImGui 帧内调用，通过背景 DrawList 绘制天气效果 */
        void render(float displayW, float displayH);

        /**
         * @brief 切换天气
         * @param type          目标天气类型
         * @param transitionSec 过渡时间（秒）
         */
        void setWeather(WeatherType type, float transitionSec = 3.0f);

        WeatherType getCurrentWeather() const { return m_current; }

        /** 返回天气中文名 */
        static const char* getWeatherName(WeatherType t);
        const char* getCurrentWeatherName() const { return getWeatherName(m_current); }

        /** 自动天气切换周期（秒）；设为 0 则禁用自动切换 */
        float autoChangePeriod = 50.0f;

    private:
        WeatherType m_current  = WeatherType::Clear;

        // 过渡：从 0 淡入到 1.0
        float m_intensity        = 1.0f;  // 当前显示强度
        float m_transitionTimer  = 0.0f;
        float m_transitionDuration = 3.0f;
        bool  m_isTransitioning  = false;

        std::vector<RainParticle> m_particles;
        uint64_t m_rng;

        // 雷电
        float m_lightningFlash    = 0.0f;   // 0..1 闪光强度
        float m_lightningNextTime = 8.0f;   // 到下次雷电的倒计时

        // 自动切换计时
        float m_autoChangeTimer = 0.0f;

        // 雨斜度（水平移动 / 垂直移动 比值，负值向左倾斜）
        static constexpr float WIND_DX = -0.22f;

        // ── 内部查询 ──
        int   targetParticleCount() const;
        float particleSpeed()  const;
        float particleLength() const;
        float particleAlpha()  const;
        float getDimAlpha()    const;

        // ── RNG ──
        uint64_t nextRand();
        float    randFloat();  // [0, 1)

        // ── 粒子生命周期 ──
        void spawnParticle(float displayW, float displayH);
        void respawnParticle(RainParticle &p, float displayW, float displayH);
    };

} // namespace game::weather
