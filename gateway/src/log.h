#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

void log_init(int verbose);
void log_msg(log_level_t level, const char *fmt, ...);

#define LOG_DEBUG(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_msg(LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
