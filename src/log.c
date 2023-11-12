#include <stdio.h>

#include "debug.h"
#include "log.h"

#define LOG_FILE_NAME "log/control_plane.log"

struct log_buf {
	uint8_t buf[LOG_BUF_SIZE];
	size_t sz;
	size_t len;
};

static struct log_buf g_buf[2];
static uint8_t g_idx_cur = 0;
static uint8_t g_idx_prev = 1;
static FILE *g_log_fp = NULL;
static pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;
static bool run_flush_thread = true;

static uint16_t g_nb_tasks = 0;
static pthread_mutex_t mutex_flush = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_flush = PTHREAD_COND_INITIALIZER;

static void *
FlushToFileThread(void *opaque) {

	struct timespec ts_now;
	int ret;

	while(run_flush_thread) {
		off_t b_off = 0;
		size_t toWrite;
		struct log_buf *pbuf;
		pthread_mutex_lock(&mutex_flush);
		clock_gettime(CLOCK_REALTIME, &ts_now);
		ts_now.tv_sec += FLUSH_PERIOD;

		if (g_nb_tasks <= 0)
			pthread_cond_timedwait(&cond_flush, &mutex_flush, &ts_now);

		ret = pthread_mutex_trylock(&mutex_log);
		if (ret < 0 && errno == EBUSY)
			continue;

		pbuf = &g_buf[g_idx_cur];
		toWrite = pbuf->len;

		while (toWrite > 0) {
			ret = fwrite(pbuf->buf + b_off, 1, pbuf->len - b_off, g_log_fp);
			toWrite -= ret;
			b_off += ret;
		}

		if (pbuf->len > 0)
			fflush(g_log_fp);

		pbuf->len = 0;

		pthread_mutex_unlock(&mutex_log);
		if (g_nb_tasks > 0)	
			g_nb_tasks--;
		pthread_mutex_unlock(&mutex_flush);
	}

	return NULL;
}

static void
SignalToThread(void) {
	pthread_mutex_lock(&mutex_flush);
	g_nb_tasks++;
	pthread_cond_signal(&cond_flush);
	pthread_mutex_unlock(&mutex_flush);
}

void
log_setup(void) {
	int ret, i;
	pthread_t flush_thread;
	g_log_fp = fopen(LOG_FILE_NAME, "w");
	if (!g_log_fp) {
		LOG_ERROR("Fail to open %s for control plane logging\n", LOG_FILE_NAME);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < 2; i++) {
		g_buf[i].len = 0;
		g_buf[i].sz = LOG_BUF_SIZE;
	}

	ret = pthread_create(&flush_thread, NULL, FlushToFileThread, NULL);
	if (ret != 0) {
		LOG_ERROR("Fail to create thread for FlushToFileThread");
		exit(EXIT_FAILURE);
	}
}

void
log_write(char *log, size_t log_len) {

	uint8_t temp;
	struct log_buf *pbuf;

	pthread_mutex_lock(&mutex_log);
	pbuf = &g_buf[g_idx_cur];

	if (pbuf->sz - pbuf->len < log_len) {
		temp = g_idx_prev;
		g_idx_prev = (g_idx_cur + 1) % 2;
		g_idx_cur = (temp + 1) % 2;

		SignalToThread();

		pbuf = &g_buf[g_idx_cur];
		memcpy(pbuf->buf + pbuf->len, log, log_len);
	} 

	memcpy(pbuf->buf + pbuf->len, log, log_len);
	pbuf->len += log_len;

	pthread_mutex_unlock(&mutex_log);
}

void
log_teardown(void) {
	run_flush_thread = false;
}
