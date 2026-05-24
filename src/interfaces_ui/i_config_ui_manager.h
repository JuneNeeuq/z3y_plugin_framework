/**
 * @file i_config_ui_manager.h
 * @brief 系统配置中心界面的核心接口定义。
 *
 * @details
 * 【架构设计】
 * 本文件定义了面向外界调用者的配置UI管理接口。采用面向接口编程（Interface-Oriented Programming）的设计，
 * 将底层的Qt界面实现与高层的业务逻辑解耦。业务模块在需要弹出配置窗口或注入自定义页面时，
 * 只需要依赖此接口即可，完全不需要引入Qt相关的头文件。
 *
 * 【核心功能】
 * 1. 弹出全局的系统配置窗口。
 * 2. 设置当前用户的角色权限（用于过滤高级选项或隐藏参数）。
 * 3. 允许外部业务插件向配置窗口中注入自定义的UI面板（Custom Panel）。
 *
 * 【给维护者的建议】
 * 作为基类接口，这里的每一个纯虚函数（ = 0）都必须在子类（ConfigUIManagerService）中被实现。
 * 如果你需要增加新的跨插件UI交互功能，请优先考虑在此处添加纯虚函数，并在子类中完成具体逻辑。
 */

#pragma once
#include <functional>
#include <string>

#include "framework/i_component.h"

namespace z3y {
namespace interfaces {
namespace ui {

/**
 * @class IConfigUIManager
 * @brief 配置UI管理器接口类。
 *
 * @details
 * 继承自底层组件系统 IComponent，通过 public virtual 继承以解决未来可能的菱形继承问题。
 * 这是整个插件的门面（Facade），所有的UI操作调用均通过此接口发起。
 */
class IConfigUIManager : public virtual z3y::IComponent {
 public:
  /**
   * @brief 组件的唯一身份标识符声明。框架通过该 ID 和版本号来定位和加载具体的实现。
   */
  Z3Y_DEFINE_INTERFACE(IConfigUIManager, "z3y-config-ui-manager", 1, 0);

  /**
   * @brief 呼出（显示）系统的配置主界面窗口。
   *
   * @details
   * 这是最常用的接口，通常绑定在主界面的“系统设置”按钮上。
   * 如果窗口尚未创建，内部会自动完成窗口的实例化、数据加载和渲染。
   * 如果窗口已经存在，则会将其置于顶层并激活（Raise and Activate）。
   *
   * @param parent_window_handle 父窗口的系统句柄（指针）。
   *        如果是在 Qt 环境下调用，可以传入主窗口的 QWidget 指针（需要外部做类型转换）；
   *        如果不需要依附父窗口（独立弹窗），则传入 nullptr 即可。
   */
  virtual void ShowConfigWindow(void* parent_window_handle = nullptr) = 0;

  /**
   * @brief 设置当前登录用户的角色。
   *
   * @details
   * 在配置系统中，不同的用户角色（如 "Admin", "Operator", "Guest"）拥有不同的可见性和修改权限。
   * 设置角色后，UI内部会根据配置字典（Schema）中的要求，自动隐藏没有权限看到的参数。
   *
   * @param role 角色名称字符串，如 "Admin"。
   */
  virtual void SetCurrentUserRole(const std::string& role) = 0;

  /**
   * @brief 自定义UI面板创建器的函数指针别名。
   *
   * @details
   * 这是一个 C++11 的 function 对象，用于接收外部传入的一个工厂函数（闭包）。
   * 该工厂函数接收一个父窗口句柄，返回一个创建好的自定义UI面板句柄（void*，底层实则为 QWidget*）。
   */
  using CustomPanelCreator = std::function<void*(void* parent_handle)>;

  /**
   * @brief 向配置系统中注册第三方的自定义UI面板。
   *
   * @details
   * 【为什么需要这个？】
   * 有时候通用的数值输入框、下拉框无法满足复杂的配置需求（例如需要一张图表、一个复杂的坐标拾取器）。
   * 业务插件可以自己用 Qt 写一个特定的界面（Widget），然后通过此接口，把这个界面的创建函数告诉配置系统。
   * 配置系统在渲染特定参数时，会调用这里的 creator，生成第三方界面并嵌进去。
   *
   * @param custom_key 唯一识别码，在 SchemaMetadata 中配置为 `custom_ui_key` 以产生绑定。
   * @param creator 用于实例化该第三方界面的工厂回调函数。
   */
  virtual void RegisterCustomPanel(const std::string& custom_key,
                                   CustomPanelCreator creator) = 0;
};

}  // namespace ui
}  // namespace interfaces
}  // namespace z3y
