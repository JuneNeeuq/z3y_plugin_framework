/**
 * @file plugin_entry.cpp
 * @brief 插件框架入口接入点。
 *
 * @details
 * 这是每一个 z3y 标准化插件模块都必须存在的入口文件。
 * 宏 `Z3Y_DEFINE_PLUGIN_ENTRY` 会自动展开并生成 C 语言链接规范下的 `PluginInit` 与 `PluginDestroy` 等底层符号。
 * 核心引擎 (PluginManager) 加载该 DLL/SO 时，第一眼寻找的就是这个入口的签名，然后完成内部类向系统的注册。
 */
#include "framework/z3y_define_impl.h"

// 暴露出动态链接库标准入口，用于加载与卸载组件元数据
Z3Y_DEFINE_PLUGIN_ENTRY;
