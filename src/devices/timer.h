#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include "lib/kernel/list.h"

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100
#define TIME_SLICE 4   // in ticks

/* Each alarm is characterised by a time (in ticks) and a semaphore 
   to make thread sleep (no-busy). The TIME is used as key. */
struct alarm
{
    int64_t time;
    struct semaphore *sp;
    struct list_elem elem;
};

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);
/*
void alarm_check (void);
*/
#endif /* devices/timer.h */
