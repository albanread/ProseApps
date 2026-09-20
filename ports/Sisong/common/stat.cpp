
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <Locker.h>

#include "basics.h"

void stat(const char *str, ...);
void staterr(const char *str, ...);
void lstat(const char *str, ...);


// A message is cut at this length. It was 40000 with an unbounded vsprintf;
// these run on any thread, stat() calls lstat(), and a thread's stack here
// is 256 KB.
#define STAT_BUFFER_SIZE	8192

BLocker statlock;
static char log_file_name[MAXPATHLEN] = { 0 };
static FILE *logfp = NULL;

void SetLogfileName(const char *nn)
{
	strlcpy(log_file_name, nn, sizeof(log_file_name));

	logfp = fopen(log_file_name, "wb");
	if (logfp)
	{
		stat("using debug log file %s", log_file_name);
	}
	else
	{
		stat("couldn't open debug log file %s; disabling log", log_file_name);
		log_file_name[0] = 0;
	}
}

class onShutdown
{
public:
	~onShutdown()
	{
		if (logfp)
		{
			fclose(logfp);
			logfp = NULL;
		}
	}
} onShutdown1;


void stat(const char *str, ...)
{
va_list ar;
char buf[STAT_BUFFER_SIZE];

	va_start(ar, str);
	vsnprintf(buf, sizeof(buf), str, ar);
	va_end(ar);

	statlock.Lock();
	{
		fprintf(stdout, "%s\n", buf);
		fflush(stdout);

		if (logfp)
			lstat("%s", buf);
	}
	statlock.Unlock();
}

void staterr(const char *str, ...)
{
va_list ar;
char buf[STAT_BUFFER_SIZE];

	va_start(ar, str);
	vsnprintf(buf, sizeof(buf), str, ar);
	va_end(ar);

	statlock.Lock();
	{
		fprintf(stderr, "%s\n", buf);
		fflush(stderr);

		if (logfp)
			lstat("<< error: '%s' >>", buf);
	}
	statlock.Unlock();
}


void lstat(const char *str, ...)
{
va_list ar;
char buf[STAT_BUFFER_SIZE];

	if (!logfp)
		return;

	statlock.Lock();
	{
		va_start(ar, str);
		vsnprintf(buf, sizeof(buf), str, ar);
		va_end(ar);

		fprintf(logfp, "%d: %s\n", find_thread(0), buf);
		fflush(logfp);
	}
	statlock.Unlock();
}



/*
char *stprintf(const char *fmt, ...)
{
va_list ar;
char *str = GetStaticStr();

	va_start(ar, fmt);
	vsprintf(str, fmt, ar);
	va_end(ar);

	return str;
}
*/
