#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace game::locale
{
    class LocaleManager
    {
    public:
        // 支持的语言列表：{ 语言代码, 显示名称 }
        static const std::vector<std::pair<std::string, std::string>> SUPPORTED_LANGUAGES;

        // 获取单例实例
        static LocaleManager &getInstance();

        // 加载指定语言（例如 "zh_CN", "en_US"）
        void loadLanguage(const std::string &lang_code);

        // 获取翻译文本；若键不存在则返回键本身作为 fallback
        const std::string &getText(const std::string &key) const;

        const std::string &getCurrentLanguage() const { return _current_language; }

        // 从 assets/settings.json 读取/保存语言偏好
        void loadSettings();
        void saveSettings() const;

    private:
        LocaleManager();
        std::string _current_language = "zh_CN";
        std::unordered_map<std::string, std::string> _translations;
    };

    // 便捷缩写：locale::T("key")
    inline const std::string &T(const std::string &key)
    {
        return LocaleManager::getInstance().getText(key);
    }

} // namespace game::locale
