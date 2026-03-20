#pragma once
#include "../component/component.h"
#include <string>
#include <unordered_map>
#include <typeindex>
#include <spdlog/spdlog.h>

namespace engine::core
{
    class Context;
}
namespace engine::object
{
    class GameObject final
    {
    private:
        std::string _name;
        std::string _tag;
        engine::core::Context* _context = nullptr;
        bool _need_remove = false;
        std::unordered_map<std::type_index, std::unique_ptr<engine::component::Component>> _components;

    public:
        GameObject(engine::core::Context &context,
            const std::string &name = "未定义的名字", 
            const std::string &tag = "未定义的标签");

        // 禁止拷贝和移动
        GameObject(const GameObject &) = delete;
        GameObject &operator=(const GameObject &) = delete;
        GameObject(GameObject &&) = delete;
        GameObject &operator=(GameObject &&) = delete;

        // GETTER
        std::string getName() const { return _name; };
        std::string getTag() const { return _tag; };
        bool isNeedRemove() const { return _need_remove; };

        // SETTER
        void setName(const std::string &name) { _name = name; };
        void setTag(const std::string &tag) { _tag = tag; };
        void setNeedRemove(bool need_remove) { _need_remove = need_remove; };

        // 组件相关
        template <typename T, typename... Args>
        T *addComponent(Args &&...args)
        {
            // 检测组件是否合法
            static_assert(std::is_base_of<engine::component::Component, T>::value, "T 必须继承 Component");
            auto type_index = std::type_index(typeid(T));
            // 检测组件是否已经存在，如果存在则返回组件指针
            if (hasComponent<T>())
            {
                return getComponent<T>();
            }
            // 如果不存在则创建组件
            auto new_component = std::make_unique<T>(std::forward<Args>(args)...);
            T *ptr = new_component.get();
            // 这会自动设置 _owner, _context 并触发 ptr->init()
            // 这里的 _context 是 GameObject 构造时存入的成员变量
            ptr->attach(this, this->_context);
            _components[type_index] = std::move(new_component);
            spdlog::debug(" GameObject {} 添加组件: {}", _name, typeid(T).name());
            return ptr;
        }

        template <typename T>
        T *getComponent() const
        {
            static_assert(std::is_base_of<engine::component::Component, T>::value, "T 必须继承 Component");
            auto type_index = std::type_index(typeid(T));
            if (_components.find(type_index) == _components.end())
            {
                return nullptr;
            }
            return static_cast<T *>(_components.at(type_index).get());
        }

        template <typename T>
        bool hasComponent() const
        {
            static_assert(std::is_base_of<engine::component::Component, T>::value, "T 必须继承 Component");
            return _components.contains(std::type_index(typeid(T)));
        }

        template <typename T>
        void removeComponent()
        {
            static_assert(std::is_base_of<engine::component::Component, T>::value, "T 必须继承 Component");
            auto type_index = std::type_index(typeid(T));
            auto it = _components.find(type_index);
            if (it != _components.end())
            {
                it->second->clean();
                _components.erase(it);
            }
        }

        void update(float delta_time);
        void render();
        void clean();
        void handleInput();
    };
}; // namespace GameObject
