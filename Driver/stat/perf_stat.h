/********************************** (C) COPYRIGHT  *******************************
* File Name          : perf_stat.h
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/10/26
* Description        : 代码段耗时统计（基础设施模块，零业务依赖）
*********************************************************************************/

#ifndef PERF_STAT_H
#define PERF_STAT_H

#include <stdint.h>
#include <stdbool.h>

//--------------------------------------------------------------------+
// 开关
//   1 = 开启：统计 BEGIN/END 之间的耗时，按窗口周期经串口输出
//   0 = 关闭：所有调用点被预处理器消除，参数不求值，零代码零数据
//
// 本模块只依赖 us_timer(micros) 与 debug(printf)，
// 不依赖 USB 或任何业务模块——是否输出的条件由调用方决定。
//--------------------------------------------------------------------+
#ifndef PERF_STAT_ENABLE
#define PERF_STAT_ENABLE 0
#endif

//--------------------------------------------------------------------+
// 开关 2：输出条件（只作为集中配置存在，本模块实现不引用它）
//   0 = 只在未挂载时输出（默认）：printf 一次约 4.3ms，此时没有 8kHz 流要干扰
//   1 = 只在已挂载时输出：能看到插线后的真实数字，但每秒缺约 35 帧（8kHz 档）
//
// 这个宏由调用方在 #if 里解释（见 main.c），perf_stat.c 完全不使用它——
// 所以"知道 USB 挂载状态"的仍然是调用方，模块保持零业务依赖。
//--------------------------------------------------------------------+
#ifndef PERF_STAT_DUMP_WHEN_MOUNTED
#define PERF_STAT_DUMP_WHEN_MOUNTED 1
#endif

#if PERF_STAT_ENABLE

//清空统计窗口（初始化后调用一次）
void perf_stat_reset(void);

//计时：BEGIN 与 END 必须成对，且不可嵌套（内部只有一个起点）
void perf_stat_begin(void);
void perf_stat_end(void);

//窗口到期则输出一次。cond 为假时直接返回（由调用方决定何时允许打印）。
//aux_counter 为调用方希望一并打印的附属计数（如丢帧数），模块不解释其含义。
void perf_stat_dump_if(bool cond, uint32_t aux_counter);

#define PERF_STAT_RESET()               perf_stat_reset()
#define PERF_STAT_BEGIN()               perf_stat_begin()
#define PERF_STAT_END()                 perf_stat_end()
#define PERF_STAT_DUMP(cond, aux)       perf_stat_dump_if((cond), (aux))

#else

//关闭时展开为空语句：参数不会被求值，连 cond 里的函数调用也不会发生
#define PERF_STAT_RESET()               ((void)0)
#define PERF_STAT_BEGIN()               ((void)0)
#define PERF_STAT_END()                 ((void)0)
#define PERF_STAT_DUMP(cond, aux)       ((void)0)

#endif // PERF_STAT_ENABLE

#endif // PERF_STAT_H