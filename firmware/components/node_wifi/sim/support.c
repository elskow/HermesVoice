#if defined(__APPLE__) || defined(__linux__)
#include <sys/time.h>
#include <stdint.h>

// Linux-sim wall clock for esp_timer_get_time (IDF linux target leaves it
// unimplemented; health telemetry + press timing need it).
int64_t esp_timer_get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}
#endif // if defined(__APPLE__) || defined(__linux__)
