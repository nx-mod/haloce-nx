/* the game's errors.h, as far as network_extensions.c uses it */
#ifndef __ERRORS_H
#define __ERRORS_H
enum { _error_silent };
void error(int priority, char const *format, ...);
#endif
