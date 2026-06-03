/**
 * @file config_ui_manager_service.cpp
 * @brief Qt 配置UI管理器服务的具体实现代码。
 *
 * @details
 * 本文件包含了 ConfigUIManagerService 的函数实现。
 * 主要完成了类型向 Qt 元对象系统 (Meta-Object System) 的注册，
 * 以及对 ConfigMainWindow 和 EventBridge 的实例化和组合。
 */

#include "config_ui_manager_service.h"

#include <QMetaType>
#include <QApplication>

#include "config_main_window.h"
#include "event_bridge.h"
#include "framework/z3y_framework.h"

// 将服务自动注册到框架中，以便其他模块可以通过 GetService 获取
Z3Y_AUTO_REGISTER_SERVICE(z3y::plugins::qt_ui::ConfigUIManagerService,
                          "Config.UIManager.Qt", true);

namespace z3y {
namespace plugins {
namespace qt_ui {

void ConfigUIManagerService::Initialize() {
  // 【关键原理】：必须使用信号签名中实际出现的完整别名路径注册 MetaType。
  // 因为跨线程使用 Qt 的 QueuedConnection 发送自定义类型时，Qt 必须能序列化该类型。
  qRegisterMetaType<z3y::plugins::qt_ui::ConfigUpdateMap>(
      "z3y::plugins::qt_ui::ConfigUpdateMap");

  // 初始化翻译系统
  if (qApp) {
    translator_ = std::make_unique<QTranslator>();
    
    // 方案B落地：混合兜底策略
    // 1. 先尝试加载外部的翻译文件 (支持热更新、免编译添加多国语言)
    QString target_lang = QLocale().name();
    if (translator_->load("z3y_config_ui_" + target_lang + ".qm", qApp->applicationDirPath() + "/translations")) {
      qApp->installTranslator(translator_.get());
    } 
    // 2. 如果外部没有，则启用编译在 DLL 里的中文作为保底
    else if (target_lang.startsWith("zh") && translator_->load(":/z3y_i18n/z3y_config_ui_zh_CN.qm")) {
      qApp->installTranslator(translator_.get());
    }
  }
}

void ConfigUIManagerService::Shutdown() {
  // 安全释放主窗口，QPointer 会保证就算之前已经被释放了，这里判断也会为 false
  if (main_window_) {
    delete main_window_.data();
  }
  // 安全重置事件桥接器，断开与后台的数据监听
  event_bridge_.reset();
}

void ConfigUIManagerService::ShowConfigWindow(void* parent) {
  // 触发 UI 即将打开的全局事件，让各业务模块能在这里动态刷新字典 (UpdateEnumSchema)
  z3y::interfaces::core::ConfigUIAboutToOpenEvent evt;
  evt.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  z3y::FireGlobalEvent<z3y::interfaces::core::ConfigUIAboutToOpenEvent>(evt);
  // 懒加载：只有当用户第一次要求显示窗口时，才去创建事件桥接器和主窗口
  if (!event_bridge_) {
    event_bridge_ = std::make_unique<EventBridge>();
    event_bridge_->Init();
  }

  if (!main_window_) {
    QWidget* qt_parent = static_cast<QWidget*>(parent);
    main_window_ = new ConfigMainWindow(event_bridge_.get(), qt_parent);
    
    // 把收集到的配置数据全部喂给主窗口
    main_window_->SetCustomPanels(custom_panels_);
    main_window_->SetRole(current_role_);
  }

  // 常规的 Qt 窗口显示三连击：显示、置顶、激活焦点
  main_window_->showNormal();
  main_window_->raise();
  main_window_->activateWindow();
}

void ConfigUIManagerService::SetCurrentUserRole(const std::string& role) {
  current_role_ = role;
  // 如果窗口已经存在，直接透传下去触发 UI 重新排版
  if (main_window_) {
    main_window_->SetRole(role);
  }
}

void ConfigUIManagerService::RegisterCustomPanel(const std::string& key,
                                                 CustomPanelCreator creator) {
  // 利用 std::move 高效地将闭包转移进字典中
  custom_panels_[key] = std::move(creator);
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y



