#include "debug.h"
#include "log.h"

void
debug_setup(void) {
	log_setup();
}

void
dump_log(char *filename, int line, char *log, ...) {
	char tmpbuf[512];
	va_list ap;
	int ret;
	size_t log_len;

	ret = sprintf(tmpbuf, "(%10s:%4d) ", filename, line);
	if (unlikely(ret < 0)) return;

	log_len = ret;

	va_start(ap, log);
	ret = vsprintf(tmpbuf + ret, log, ap);
	va_end(ap);

	if (unlikely(ret < 0)) return;

	log_len += ret;

	log_write(log, log_len);
}

void
debug_teardown(void) {
	/* TODO */
}
