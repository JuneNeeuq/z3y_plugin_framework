/**
 * @file config_ui_manager_service.h
 * @brief Qt 配置UI管理器服务的具体实现声明。
 *
 * @details
 * 本文件负责实现 interfaces_ui/i_config_ui_manager.h 中定义的纯虚接口。
 * 它作为一个标准的 z3y 插件服务存在，负责管理 Qt 主窗口 (ConfigMainWindow) 以及
 * 负责数据层与 UI 层通信的事件桥接器 (EventBridge) 的生命周期。
 */

#pragma once
#include <QPointer>
#include <map>
#include <memory>
#include <string>

#include "framework/z3y_define_impl.h"
#include "interfaces_ui/i_config_ui_manager.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

class ConfigMainWindow;
class EventBridge;

/**
 * @class ConfigUIManagerService
 * @brief 配置UI服务的具体实现类，基于 Qt 框架。
 *
 * @details
 * 继承自 z3y::PluginImpl 和 IConfigUIManager。
 * 它是整个 UI 插件模块的入口大脑，负责：
 * 1. 接收来自业务层的命令（显示窗口、设置角色）。
 * 2. 托管主窗口和通信桥的内存。
 * 3. 收集并下发自定义面板的工厂函数。
 */
class ConfigUIManagerService
    : public z3y::PluginImpl<ConfigUIManagerService,
                             z3y::interfaces::ui::IConfigUIManager> {
 public:
  /** @brief 插件服务的唯一实现 ID，用于服务定位器获取此实例。 */
  Z3Y_DEFINE_COMPONENT_ID("z3y-qt-ConfigUIManagerService-P001");

  /** @brief 默认构造函数 */
  ConfigUIManagerService() = default;

  /**
   * @brief 服务初始化钩子。
   * @details 插件系统在加载此服务时自动调用。在此处向 Qt 注册跨线程所需的元类型。
   */
  void Initialize() override;

  /**
   * @brief 服务销毁钩子。
   * @details 插件系统在卸载此服务时自动调用。负责安全销毁窗口和事件桥接器。
   */
  void Shutdown() override;

  /**
   * @brief 显示配置窗口的实现。
   * @details 如果窗口未创建，则初始化 EventBridge 并 `new` 出 ConfigMainWindow。随后将其弹出并置顶。
   * @param parent_window_handle 外部传入的父窗口句柄，将被强转为 QWidget*。
   */
  void ShowConfigWindow(void* parent_window_handle) override;

  /**
   * @brief 存储并下发角色权限信息。
   * @details 如果主窗口已经开启，会立刻通知主窗口进行角色刷新（隐藏无权限项）。
   * @param role 角色名称，如 "Admin"。
   */
  void SetCurrentUserRole(const std::string& role) override;

  /**
   * @brief 暂存外部注入的自定义面板工厂。
   * @details 存入 `custom_panels_` 字典中，当主窗口创建时会将此字典传递给主窗口以便实例化。
   * @param custom_key 对应的绑定键值。
   * @param creator 创建面板的闭包函数。
   */
  void RegisterCustomPanel(const std::string& custom_key,
                           CustomPanelCreator creator) override;

 private:
  /** @brief 事件桥接器，负责将后台配置系统的更新异步投递到 Qt UI 线程。使用 unique_ptr 独占管理。 */
  std::unique_ptr<EventBridge> event_bridge_;

  /** 
   * @brief 配置主窗口的指针。
   * @details 采用 QPointer (Qt 的弱引用指针)。如果窗口被用户强行释放，此指针会自动安全地置空，从而避免双重释放 (Double Free Bug) 导致的崩溃。
   */
  QPointer<ConfigMainWindow> main_window_;

  /** @brief 当前生效的用户角色字符串。 */
  std::string current_role_;

  /** @brief 缓存外部业务模块注册过来的所有自定义面板工厂函数。 */
  std::map<std::string, CustomPanelCreator> custom_panels_;
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
