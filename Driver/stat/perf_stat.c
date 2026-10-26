/********************************** (C) COPYRIGHT  *******************************
* File Name          : perf_stat.c
* Author             : P1nkDog
* Version            : V1.0.0
* Date               : 2026/10/26
* Description        : 代码段耗时统计实现（PERF_STAT_ENABLE=0 时本文件为空）
*********************************************************************************/

#include "perf_stat.h"

#if PERF_STAT_ENABLE

#include "debug.h"          //printf
#include "us_timer.h"       //micros

//--------------------------------------------------------------------+
// 静态配置
//--------------------------------------------------------------------+
#define STAT_WINDOW_US      1000000u        //1 秒输出一次
#define STAT_MIN_INVALID    0xFFFFFFFFu     //min 的"尚无样本"标记

//--------------------------------------------------------------------+
// 统计状态（当前窗口）
//--------------------------------------------------------------------+
static uint32_t s_t0;               //本段起点
static uint32_t s_max;              //窗口内最长
static uint32_t s_min;              //窗口内最短
static uint32_t s_sum;              //窗口内累计
static uint32_t s_cnt;              //窗口内次数
static uint32_t s_win_start;        //窗口起点
static uint32_t s_aux_last;         //上次输出的附属计数

//--------------------------------------------------------------------+
// 公共接口
//--------------------------------------------------------------------+

void perf_stat_reset(void)
{
    uint32_t now = micros();

    s_t0        = now;
    s_max       = 0;
    s_min       = STAT_MIN_INVALID;
    s_sum       = 0;
    s_cnt       = 0;
    s_win_start = now;
    s_aux_last  = 0;
}

void perf_stat_begin(void)
{
    s_t0 = micros();
}

void perf_stat_end(void)
{
    uint32_t dt = micros() - s_t0;      //uint32 无符号相减，跨回绕仍正确

    if (dt > s_max) s_max = dt;
    if (dt < s_min) s_min = dt;
    s_sum += dt;
    s_cnt++;
}

void perf_stat_dump_if(bool cond, uint32_t aux_counter)
{
    //cond 由调用方给出（例如"未挂载"），本模块不判断任何外部状态
    if (!cond) return;

    uint32_t now = micros();
    if ((now - s_win_start) < STAT_WINDOW_US) return;

    //赋 now 而不是累加窗口，避免打印耗时被计入下一周期后连续补打
    s_win_start = now;

    uint32_t avg = (s_cnt != 0u) ? (s_sum / s_cnt) : 0u;
    uint32_t min = (s_min != STAT_MIN_INVALID) ? s_min : 0u;
    uint32_t aux = aux_counter - s_aux_last;
    s_aux_last = aux_counter;

    printf("block max/avg/min = %lu/%lu/%luus  n=%lu  aux=+%lu\r\n",
           (unsigned long)s_max,
           (unsigned long)avg,
           (unsigned long)min,
           (unsigned long)s_cnt,
           (unsigned long)aux);

    //重置窗口
    s_max = 0;
    s_min = STAT_MIN_INVALID;
    s_sum = 0;
    s_cnt = 0;
}

#else

//关闭时保持翻译单元非空，避免 -Wpedantic 下的空文件告警
typedef int perf_stat_disabled_t;

#endif // PERF_STAT_ENABLE