// Start-stop timer implementation by Alexis Engelke, 2015.
// Licensed under the MIT license. See the LICENSE file.

#include <time.h>
#include "timer.h"

void JTimerInit(JTimer* timer) {
  timer->timetaken_time.tv_sec = 0;
  timer->timetaken_time.tv_nsec = 0;
}

void JTimerCont(JTimer* timer) {
  clock_gettime(CLOCK_REALTIME, &(timer->current_time));
}

void JTimerStop(JTimer* timer) {
  struct timespec comp;
  clock_gettime(CLOCK_REALTIME, &comp);

  timer->timetaken_time.tv_sec += comp.tv_sec - timer->current_time.tv_sec;
  timer->timetaken_time.tv_nsec += comp.tv_nsec - timer->current_time.tv_nsec;
  if (timer->timetaken_time.tv_nsec < 0) {
    timer->timetaken_time.tv_nsec += 1000000000;
    timer->timetaken_time.tv_sec--;
  }
  else if (timer->timetaken_time.tv_nsec > 1000000000) {
    timer->timetaken_time.tv_nsec -= 1000000000;
    timer->timetaken_time.tv_sec++;
  }
}

double JTimerRead(JTimer* timer) {
  return timer->timetaken_time.tv_sec + timer->timetaken_time.tv_nsec * 1e-9;
}
