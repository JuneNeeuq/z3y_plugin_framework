/**
 * @file event_bridge.cpp
 * @brief 跨线程事件桥接器的实现代码。
 *
 * @details
 * 主要实现了基于 QMetaObject::invokeMethod 的无锁化数据跨线程投递逻辑，
 * 极大地保证了在高频并发参数更新下，UI 界面的流畅度与系统的绝对稳定。
 */

#include "event_bridge.h"

#include <QString>
#include <QVariantList>

#include "framework/plugin_exceptions.h"
#include "framework/z3y_service_locator.h"
#include "qt_utils.h"

namespace z3y {
namespace plugins {
namespace qt_ui {

void EventBridgeSubscriber::OnConfigChanged(
    const z3y::interfaces::core::ConfigChangedEvent& evt) {
  std::lock_guard<std::mutex> lock(mtx_);
  // 确保 bridge_ 还在，且服务没被卸载
  if (bridge_ && bridge_->cached_config_srv_) {
    QString qt_path = QString::fromUtf8(evt.path.data(), evt.path.size());

    try {
      // 重新向底层请求一次获取强类型的真实数据
      auto current_val = bridge_->cached_config_srv_->GetValue(evt.path);
      // 转化为 Qt 可以理解的数据结构
      QVariant qt_val = ConvertToQVariant(current_val);
      
      // 【核心技术点】：强行将其投递回 Main(UI) 线程执行 ReceiveDataInMainThread。
      // 这个动作本身是非阻塞的，它会把调用任务塞进 UI 线程的事件队列中。
      QMetaObject::invokeMethod(bridge_, "ReceiveDataInMainThread",
                                Qt::QueuedConnection, Q_ARG(QString, qt_path),
                                Q_ARG(QVariant, qt_val));
    } catch (...) {
      // 忽略因路径被瞬间移除导致无法获取值的情况
    }
  }
}

EventBridge::EventBridge(QObject* parent) : QObject(parent) {}

EventBridge::~EventBridge() {
  // 析构时先切断 EventBus
  conn_.Disconnect();
  // 并且主动清理 Subscriber 对自身的指引，避免野指针调用
  if (subscriber_) subscriber_->Detach();
}

void EventBridge::Init() {
  try {
    cached_config_srv_ =
        z3y::GetDefaultService<z3y::interfaces::core::IConfigService>();
    auto event_bus = z3y::GetService<z3y::IEventBus>(z3y::clsid::kEventBus);
    if (event_bus) {
      subscriber_ = std::make_shared<EventBridgeSubscriber>(this);
      // 订阅全局级别的 ConfigChangedEvent
      conn_ =
          event_bus->SubscribeGlobal<z3y::interfaces::core::ConfigChangedEvent>(
              subscriber_, &EventBridgeSubscriber::OnConfigChanged,
              z3y::ConnectionType::kDirect);
    }
  } catch (...) {
  }

  // 设置定时器：每 33ms 收割一次全部积累的更新包发送给 UI
  throttle_timer_.setInterval(33);
  connect(&throttle_timer_, &QTimer::timeout, this,
          &EventBridge::FlushDirtyData);
  throttle_timer_.start();
}

void EventBridge::ReceiveDataInMainThread(QString path, QVariant value) {
  // 【无锁化设计原理】
  // 这个函数由于被 invokeMethod 丢到了主线程执行，所以此时环境 100% 是单线程的 UI 线程。
  // 此时对 dirty_map_ 进行读写是绝对线程安全的，不需要任何 std::mutex 互斥锁。
  // 若同一个参数在 33ms 内突发变化几十次，map 还会天然地通过 key 覆盖去重，只有最后一次值留存。
  dirty_map_[path] = value;
}

void EventBridge::FlushDirtyData() {
  if (dirty_map_.empty()) return;

  // 使用 std::map 的 swap 机制，进行 O(1) 的零拷贝交换
  // 将旧的数据瞬间抽离并清空旧池，准备接待后续请求
  ConfigUpdateMap batch;
  batch.swap(dirty_map_);

  // 直接将聚合打包好的数据发出去（Main Window 会连接这个信号）
  emit batchedConfigChanged(batch);
}

}  // namespace qt_ui
}  // namespace plugins
}  // namespace z3y
