#pragma once
#include <string>
#include <array>
#include "../inventory/inventory.h"

namespace game::weapon
{
    // 攻击类型
    enum class AttackType
    {
        Melee,       // 近战
        Projectile,  // 子弹/投射物
        Explosive,   // 爆炸
        Raycast,     // 光线（激光）
        Special      // 特殊（挖掘等）
    };

    // 武器定义（静态数据）
    struct WeaponDef
    {
        std::string id;
        std::string name_key;       // locale key
        AttackType  attack_type;
        int         damage;
        float       attack_speed;   // 每秒攻击次数
        float       range;          // 像素；0 = 无限
        int         ammo_capacity;  // 0 = 无限弹药
        std::string icon_label;     // 显示在格子中的短文本
        std::string desc_key;       // locale key for description
    };

    // 查询武器定义（未找到返回 nullptr）
    const WeaponDef* getWeaponDef(const std::string& id);

    // 判断某个物品 id 是否是武器
    bool isWeaponId(const std::string& id);

    // -------------------------------------------------------
    //  武器栏：5 个装备槽，支持滚轮切换
    // -------------------------------------------------------
    class WeaponBar
    {
    public:
        static constexpr int SLOTS = 5;

        inventory::InventorySlot&       getSlot(int idx)       { return _slots[idx]; }
        const inventory::InventorySlot& getSlot(int idx) const { return _slots[idx]; }

        int  getActiveIndex() const { return _active; }
        void setActiveIndex(int idx) { _active = ((idx % SLOTS) + SLOTS) % SLOTS; }

        // 滚轮切换：+1 下一个，-1 上一个
        void scroll(int delta) { setActiveIndex(_active + delta); }

        // 从背包某格装备到武器栏某格（若武器栏已有武器则交换）
        void equipFromInventory(int bar_slot, int inv_slot, inventory::Inventory& inv);

        // 将武器栏某格卸装回背包
        void unequipToInventory(int bar_slot, inventory::Inventory& inv);

        // 获取当前激活武器槽（可能为空）
        const inventory::InventorySlot& getActiveSlot() const { return _slots[_active]; }

    private:
        std::array<inventory::InventorySlot, SLOTS> _slots;
        int _active = 0;
    };

} // namespace game::weapon
