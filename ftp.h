#include "ff.h"

#define ftplock()   _ftplock(__FUNC__,__LINE__)
#define ftpunlock() _ftpunlock(__FUNC__,__LINE__)
#define ftptrylock() _ftptrylock(__FUNC__,__LINE__)

int ftpcd(char*dir);
bool _ftptrylock(char*f, int line);
char *ftppwd(void);
void _ftplock(char*func, int line);
void _ftpunlock(char*func, int line);
