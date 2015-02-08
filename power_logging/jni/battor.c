#include <jni.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>

#include <android/sensor.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#define REGULATOR_PATH "/sys/class/regulator"
#define SLEEP_US 10000
#define TIMESYNC_INT_US 70000000 
//#define TIMESYNC_TORCH_INT_NS 4000000
#define TIMESYNC_TORCH_INT_S 2
#define TIMESYNC_TORCH_INT_NS 0
//#define TIMESYNC_TORCH_FLASHES 64
#define TIMESYNC_TORCH_FLASHES 32 

char timesync_pn[] = {1,0,0,1,0,1,1,0,1,1,0,1,1,0,0,0,1,1,0,0,1,1,1,1,1,0,1,0,1,1,0,1,0,0,0,1,0,0,1,0,0,1,1,0,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,1,1,0,0};

#define APP_NAME "BattOr"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, APP_NAME, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, APP_NAME, __VA_ARGS__))

/*#define SYSFS_FILENAME "/sys/devices/qpnp-iadc-f856a200/internal_rsense" */
// #define SYSFS_FILENAME "/sys/devices/qpnp-iadc-f858a000/internal_rsense"
/* #define SYSFS_FILENAME "/sys/devices/qpnp-iadc-f856a200/external_rsense" */
#define SYSFS_FILENAME "/sys/class/power_supply/battery/current_now"

static int keep_running = 1;

/**
 * Process the next input event.
 */
int32_t handle_input(struct android_app* app, AInputEvent* event) 
{
	// TODO app->userData;
	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) 
	{
		LOGI("hello\n");
		return 1;
	}
	return 0;
}

/**
 * Process the next main command.
 */
void handle_cmd(struct android_app* app, int32_t cmd) {
	// TODO app->userData;
	switch (cmd) 
	{
		case APP_CMD_SAVE_STATE:
			break;
		case APP_CMD_INIT_WINDOW:
			// The window is being shown, get it ready.
			break;
		case APP_CMD_TERM_WINDOW:
			// The window is being hidden or closed, clean it up.
			break;
	}
}

void inline timespec_to_timeval(struct timespec* ts, struct timeval* tv)
{
	tv->tv_sec = ts->tv_sec;
	tv->tv_usec = ts->tv_nsec / 1000; 
}

struct timeval tv_start;
inline uint32_t get_time_us() //{{{
{
	struct timespec ts;
	struct timeval tv, tv_sub;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	timespec_to_timeval(&ts, &tv);
	timersub(&tv, &tv_start, &tv_sub);
	return (tv_sub.tv_sec * 1000000) + tv_sub.tv_usec;
} //}}}

typedef struct regulator_
{
	char name[100];
	char state;
	int microvolts;
} regulator;

int list_regulators(char regfs_name[][100], char regfs_state[][100], char regfs_microvolts[][100]) //{{{
{	
	int len = 0;
	DIR* dir = opendir(REGULATOR_PATH);
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (ent->d_name[0] != '.')
		{
			sprintf(regfs_name[len], "%s/%s/name", REGULATOR_PATH, ent->d_name);
			sprintf(regfs_state[len], "%s/%s/state", REGULATOR_PATH, ent->d_name);
			sprintf(regfs_microvolts[len], "%s/%s/microvolts", REGULATOR_PATH, ent->d_name);
			len++;
		}
	}
	return len;
} //}}}

volatile sig_atomic_t flash_timer_fired = 0;
void flash_handler(int signo) //{{{
{
	// no the right signal
	if (signo != SIGALRM)
		return;

	flash_timer_fired++;
} //}}}

/**
 * Main entry point, handles events
 */
void android_main(struct android_app* app) 
{
	int i;
	char regfs_name[100][100], regfs_state[100][100], regfs_microvolts[100][100];
	regulator regs[100];
	int regs_len = 0, regs_idx = 0;
	struct timespec ts_start;
	uint32_t time = 0, prev_time_torch = get_time_us();
	timer_t flash_timer;
	int flash_idx = 0;
	int sync_idx = 0;
	int flash_times[TIMESYNC_TORCH_FLASHES];
	int audio_fd = -1;
	FILE* log_file = NULL;
	
	char buf[500] = "";

	LOGI("Starting power sampling...\n");

	// for signal handling
	pid_t thread_id = gettid();
	sigset_t sigset;
	sigemptyset(&sigset);

	// start time
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	timespec_to_timeval(&ts_start, &tv_start);

	audio_fd = open(SYSFS_FILENAME, O_RDONLY);
	if (audio_fd == -1)
	{
		LOGI("OPEN FAILED: %s\n", strerror(errno));
		return;
	}
	
	log_file = fopen("/sdcard/current", "w");
	if (NULL == log_file) {
		LOGW("Failed opening output file (%s)\n", strerror(errno));
		close(audio_fd);
		return;
	}

	/* make sure glue isn't stripped */
	app_dummy();

	app->userData = NULL;
	app->onAppCmd = handle_cmd;
	app->onInputEvent = handle_input;

	// flash timer
	struct sigevent event = {.sigev_notify=SIGEV_THREAD_ID, .sigev_signo=SIGALRM, .sigev_notify_thread_id=thread_id};
	struct sigaction action = {.sa_handler=flash_handler, .sa_mask=0, .sa_flags=0};
	struct itimerspec its = {
		.it_interval = {.tv_sec = TIMESYNC_TORCH_INT_S, .tv_nsec = TIMESYNC_TORCH_INT_NS},
		.it_value = {.tv_sec = 0, .tv_nsec = 1}
	};
	struct itimerspec its_zero;

	memset(&its_zero, 0, sizeof(its_zero));
	timer_create(CLOCK_MONOTONIC, &event, &flash_timer);
	timer_settime(flash_timer, 0, &its_zero, NULL);
	sigaction(SIGALRM, &action, NULL);

	// Read all pending events.
	while (keep_running) 
	{
		int len, len1;
		int ident;
		int events;
		struct android_poll_source* source;

		fprintf(log_file, "start: %u\n", get_time_us());
		lseek(audio_fd, 0, SEEK_SET);
		fprintf(log_file, "lseek: %u\n", get_time_us());
		len = read(audio_fd, buf, 20);
		fprintf(log_file, "read: %u %d\n", get_time_us(), len);
		if (len >= 0)
		{
			fwrite(buf, len, 1, log_file);
			fprintf(log_file, "fwrite: %u\n", get_time_us());
		}

		while ((ident = ALooper_pollAll(0, NULL, &events, (void**)&source)) >= 0) {
			if (source != NULL) {
				source->process(app, source);
			}
			
			if (app->destroyRequested != 0) {
				/* requested to exit application */
				LOGW("Requested to quit app");
				keep_running = 0;
			}

		}
	} /* while keep_running */

cleanup:
	close(audio_fd);
	fclose(log_file);
}
