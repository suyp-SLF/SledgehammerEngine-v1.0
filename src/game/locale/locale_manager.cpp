#include "locale_manager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace game::locale
{
    const std::vector<std::pair<std::string, std::string>> LocaleManager::SUPPORTED_LANGUAGES = {
        {"zh_CN", "\u7b80\u4f53\u4e2d\u6587"},
        {"en_US", "English"}};

    LocaleManager &LocaleManager::getInstance()
    {
        static LocaleManager instance;
        return instance;
    }

    LocaleManager::LocaleManager()
    {
        loadSettings();
    }

    void LocaleManager::loadLanguage(const std::string &lang_code)
    {
        std::string path = "assets/locale/" + lang_code + ".json";
        std::ifstream file(path);
        if (!file.is_open())
        {
            spdlog::warn("LocaleManager: 语言文件 {} 打开失败", path);
            return;
        }
        try
        {
            nlohmann::json j;
            file >> j;
            _translations.clear();
            for (auto &[key, value] : j.items())
                _translations[key] = value.get<std::string>();
            _current_language = lang_code;
            spdlog::info("LocaleManager: 语言 {} 加载完成，{}条翻译", lang_code, _translations.size());
        }
        catch (const std::exception &e)
        {
            spdlog::error("LocaleManager: 语言文件 {} 解析失败: {}", path, e.what());
        }
    }

    const std::string &LocaleManager::getText(const std::string &key) const
    {
        auto it = _translations.find(key);
        if (it != _translations.end())
            return it->second;
        return key; // fallback：返回键本身
    }

    void LocaleManager::loadSettings()
    {
        std::ifstream file("assets/settings.json");
        if (!file.is_open())
        {
            loadLanguage(_current_language);
            return;
        }
        try
        {
            nlohmann::json j;
            file >> j;
            std::string lang = j.value("language", _current_language);
            loadLanguage(lang);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("LocaleManager: settings.json 读取失败: {}", e.what());
            loadLanguage(_current_language);
        }
    }

    void LocaleManager::saveSettings() const
    {
        std::ofstream file("assets/settings.json");
        if (!file.is_open())
        {
            spdlog::error("LocaleManager: assets/settings.json 写入失败");
            return;
        }
        nlohmann::json j;
        j["language"] = _current_language;
        file << j.dump(4);
        spdlog::info("LocaleManager: settings.json 已保存 (language={})", _current_language);
    }

} // namespace game::locale
