#include "weapon.h"
#include <unordered_map>
#include <algorithm>

namespace game::weapon
{
    // ------------------------------------------------------------------
    // 全武器定义表（可随时扩展）
    // 格式：id, name_key, attack_type, damage, attack_speed, range, ammo, icon, desc_key
    // ------------------------------------------------------------------
    static const std::unordered_map<std::string, WeaponDef> s_defs = {
        // 挖掘枪（多功能工具）
        {"dig_gun",
         {"dig_gun", "weapon.dig_gun", AttackType::Special,
          5, 4.0f, 64.f, 0, "[挖]", "weapon.dig_gun.desc"}},

        // 近战武器
        {"iron_sword",
         {"iron_sword", "weapon.iron_sword", AttackType::Melee,
          30, 1.5f, 80.f, 0, "[剑]", "weapon.iron_sword.desc"}},
        {"knife",
         {"knife", "weapon.knife", AttackType::Melee,
          15, 3.0f, 50.f, 0, "[刀]", "weapon.knife.desc"}},
        {"war_axe",
         {"war_axe", "weapon.war_axe", AttackType::Melee,
          45, 0.8f, 90.f, 0, "[斧]", "weapon.war_axe.desc"}},

        // 射击武器
        {"pistol",
         {"pistol", "weapon.pistol", AttackType::Projectile,
          20, 2.5f, 512.f, 16, "[枪]", "weapon.pistol.desc"}},
        {"shotgun",
         {"shotgun", "weapon.shotgun", AttackType::Projectile,
          60, 0.8f, 256.f, 8, "[霰]", "weapon.shotgun.desc"}},
        {"sniper",
         {"sniper", "weapon.sniper", AttackType::Projectile,
          100, 0.5f, 2048.f, 5, "[狙]", "weapon.sniper.desc"}},

        // 爆炸武器
        {"rocket_launcher",
         {"rocket_launcher", "weapon.rocket_launcher", AttackType::Explosive,
          120, 0.4f, 1024.f, 4, "[火]", "weapon.rocket_launcher.desc"}},
        {"grenade_launcher",
         {"grenade_launcher", "weapon.grenade_launcher", AttackType::Explosive,
          90, 0.6f, 512.f, 6, "[弹]", "weapon.grenade_launcher.desc"}},

        // 特殊武器
        {"laser_gun",
         {"laser_gun", "weapon.laser_gun", AttackType::Raycast,
          50, 5.0f, 2048.f, 0, "[激]", "weapon.laser_gun.desc"}},
        {"flamethrower",
         {"flamethrower", "weapon.flamethrower", AttackType::Special,
          10, 10.0f, 128.f, 0, "[焰]", "weapon.flamethrower.desc"}},
    };

    const WeaponDef* getWeaponDef(const std::string& id)
    {
        auto it = s_defs.find(id);
        return it != s_defs.end() ? &it->second : nullptr;
    }

    bool isWeaponId(const std::string& id)
    {
        return s_defs.find(id) != s_defs.end();
    }

    // ------------------------------------------------------------------
    //  WeaponBar 实现
    // ------------------------------------------------------------------
    void WeaponBar::equipFromInventory(int bar_slot, int inv_slot, inventory::Inventory& inv)
    {
        auto& inv_s  = inv.getSlot(inv_slot);
        auto& bar_s  = _slots[bar_slot];

        // 若背包格是空的，什么都不做
        if (inv_s.isEmpty()) return;

        // 只允许武器进武器栏
        if (inv_s.item->category != inventory::ItemCategory::Weapon) return;

        // 武器栏有武器，交换
        std::swap(bar_s, inv_s);
    }

    void WeaponBar::unequipToInventory(int bar_slot, inventory::Inventory& inv)
    {
        auto& bar_s = _slots[bar_slot];
        if (bar_s.isEmpty()) return;

        if (inv.addItem(*bar_s.item, bar_s.count))
            bar_s = {};
    }

} // namespace game::weapon
