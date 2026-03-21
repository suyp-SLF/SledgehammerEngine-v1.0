#include "inventory.h"
#include <algorithm>

namespace game::inventory
{
    Inventory::Inventory() : _slots(CAPACITY) {}

    bool Inventory::addItem(const Item &item, int count)
    {
        // 优先堆叠到已有同类物品的格子
        for (auto &slot : _slots)
        {
            if (!slot.isEmpty() && slot.item->id == item.id)
            {
                int space = slot.item->max_stack - slot.count;
                if (space > 0)
                {
                    int add = std::min(count, space);
                    slot.count += add;
                    count -= add;
                    if (count <= 0) return true;
                }
            }
        }

        // 再找空格子
        for (auto &slot : _slots)
        {
            if (slot.isEmpty())
            {
                slot.item = item;
                slot.count = std::min(count, item.max_stack);
                count -= slot.count;
                if (count <= 0) return true;
            }
        }

        return count <= 0;
    }

    bool Inventory::removeItem(const std::string &id, int count)
    {
        for (auto &slot : _slots)
        {
            if (!slot.isEmpty() && slot.item->id == id)
            {
                int remove = std::min(count, slot.count);
                slot.count -= remove;
                if (slot.count == 0) slot.item.reset();
                count -= remove;
                if (count <= 0) return true;
            }
        }
        return count <= 0;
    }

    void Inventory::swapSlots(int a, int b)
    {
        std::swap(_slots[a], _slots[b]);
    }

    int Inventory::countItem(const std::string &id) const
    {
        int total = 0;
        for (const auto &slot : _slots)
            if (!slot.isEmpty() && slot.item->id == id)
                total += slot.count;
        return total;
    }

} // namespace game::inventory
