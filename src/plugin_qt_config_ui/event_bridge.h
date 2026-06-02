/**
 * @file event_bridge.h
 * @brief 后台数据与前端 UI 之间的跨线程事件桥接器声明。
 *
 * @details
 * 【架构设计】
 * 因为 z3y 插件后台系统是多线程的，而 Qt 的界面刷新强制要求必须在 Main（UI）线程。
 * EventBridge 就是这两界之间的安全通道：
 * 1. 它订阅后台的 ConfigChangedEvent。
 * 2. 收集一定时间内的密集变更（节流 Throttling）。
 * 3. 最终通过 Qt 的信号-槽（QueuedConnection）打包发送给 UI 进行无锁刷新。
 */

#pragma once
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "framework/connection.h"
#include "framework/i_event_bus.h"
#include "interfaces_core/config_types.h"
#include "interfaces_core/i_config_service.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

/** @brief 定义更新包数据结构：键为配置路径，值为 Qt 通用的 QVariant 变量。 */
using ConfigUpdateMap = std::map<QString, QVariant>;

class EventBridge;

/**
 * @class EventBridgeSubscriber
 * @brief 订阅者实体类，专门负责从 EventBus 接收全局事件。
 *
 * @details
 * 将订阅逻辑与 QObject 剥离，并通过 weak/shared_ptr 机制，防止 Qt
 * 控件销毁时引发后台回调访问空指针（生命周期解耦）。
 */
class EventBridgeSubscriber
    : public std::enable_shared_from_this<EventBridgeSubscriber> {
 public:
  /** @brief 构造并绑定关联的 EventBridge 大脑 */
  explicit EventBridgeSubscriber(EventBridge* bridge) : bridge_(bridge) {}
  
  /** @brief 后台数据变更时触发的核心回调函数 */
  void OnConfigChanged(const z3y::interfaces::core::ConfigChangedEvent& evt);
  
  /** @brief 脱离函数，用于在 EventBridge 销毁时主动切断联系 */
  void Detach() {
    std::lock_guard<std::mutex> lock(mtx_);
    bridge_ = nullptr;
  }

 private:
  std::mutex mtx_;          /**< @brief 保护 bridge_ 指针安全的互斥锁 */
  EventBridge* bridge_;     /**< @brief 指向宿主的原始指针 */
};

/**
 * @class EventBridge
 * @brief Qt 侧的事件桥梁本体，负责数据聚合与跨线程转推。
 */
class EventBridge : public QObject {
  Q_OBJECT
 public:
  /** @brief 构造函数，需要依附于 Qt 树（可选） */
  explicit EventBridge(QObject* parent = nullptr);
  ~EventBridge() override;

  /** @brief 初始化操作：向 EventBus 注册订阅，启动节流定时器 */
  void Init();

 signals:
  /**
   * @brief 打包发送给主窗口的数据更新信号。
   * @param updates 汇总了这 33ms 内所有被修改的数据字典。
   */
  void batchedConfigChanged(
      const z3y::plugins::qt_ui::ConfigUpdateMap& updates);

 public slots:
  /** @brief 定时器触发的槽函数，用于将目前收集到的所有脏数据打包推给 UI */
  void FlushDirtyData();

 private:
  /**
   * @brief 在主线程中接收来自后台线程投递的单条数据。
   * @details 宏 Q_INVOKABLE 使得该函数可通过 QMetaObject::invokeMethod 进行跨线程调用。
   * @param path 发生变化的参数路径
   * @param value 最新的数据值，已经转换为 Qt 变体 QVariant
   */
  Q_INVOKABLE void ReceiveDataInMainThread(QString path, QVariant value);

 private:
  /** @brief 管理全局订阅的生命周期锚点 */
  z3y::Connection conn_;
  /** @brief 指向底层订阅回调载体的共享指针 */
  std::shared_ptr<EventBridgeSubscriber> subscriber_;
  /** @brief 30 FPS 的节流定时器，防止 UI 响应过度频繁卡死 */
  QTimer throttle_timer_;

  /** @brief 缓存 ConfigService 句柄，便于快速读取最新的真实数据 */
  std::shared_ptr<z3y::interfaces::core::IConfigService> cached_config_srv_;

  /** @brief 暂存尚未刷给 UI 的最新脏数据池（仅在 UI 线程访问，因此彻底无锁） */
  ConfigUpdateMap dirty_map_;

  // 授权 Subscriber 访问私有变量
  friend class EventBridgeSubscriber;
};

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y

/** @brief 向 Qt 反射系统声明自定义数据结构，供信号跨线程排队传递 */
Q_DECLARE_METATYPE(z3y::plugins::qt_ui::ConfigUpdateMap)
