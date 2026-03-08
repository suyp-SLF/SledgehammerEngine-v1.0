// 世界配置（种子、参数）
#include "world_config.h"
#include "world_config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

bool engine::world::WorldConfig::loadFromFile(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::error("无法打开世界配置文件: {}", path);
        return false;
    }
    try {
        nlohmann::json config_json;
        file >> config_json;

        // 使用 at 确保字段存在
        seed = config_json.at("seed").get<uint64_t>();
        seaLevel = config_json.at("seaLevel").get<int>();
        noiseScale = config_json.at("noiseScale").get<float>();
        amplitude = config_json.at("amplitude").get<float>();
        grassDepth = config_json.at("grassDepth").get<int>();
        dirtDepth = config_json.at("dirtDepth").get<int>();
        stoneStart = config_json.at("stoneStart").get<int>();

        return true;
    } catch (const nlohmann::json::parse_error &e) {
        spdlog::error("解析世界配置文件 JSON 失败: {}，错误: {}", path, e.what());
    } catch (const nlohmann::json::out_of_range &e) {
        spdlog::error("世界配置文件缺少必要字段: {}，错误: {}", path, e.what());
    } catch (const std::exception &e) {
        spdlog::error("读取世界配置文件异常: {}，错误: {}", path, e.what());
    }
    return false;
}