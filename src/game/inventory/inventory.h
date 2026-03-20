#pragma once
#include <string>
#include <vector>
#include <optional>

namespace game::inventory
{
    struct Item
    {
        std::string id;
        std::string name;
        int max_stack = 99;
    };

    struct InventorySlot
    {
        std::optional<Item> item;
        int count = 0;

        bool isEmpty() const { return !item.has_value() || count == 0; }
    };

    class Inventory
    {
    public:
        static constexpr int COLS = 10;
        static constexpr int ROWS = 10;
        static constexpr int CAPACITY = COLS * ROWS;

        Inventory();

        // 添加物品，优先堆叠同类格子，返回是否完全放入
        bool addItem(const Item &item, int count = 1);

        // 移除指定ID物品，返回是否完全移除
        bool removeItem(const std::string &id, int count = 1);

        // 交换两个格子
        void swapSlots(int a, int b);

        InventorySlot &getSlot(int index) { return _slots[index]; }
        const InventorySlot &getSlot(int index) const { return _slots[index]; }
        int getSlotCount() const { return CAPACITY; }

    private:
        std::vector<InventorySlot> _slots;
    };

} // namespace game::inventory
