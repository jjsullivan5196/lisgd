#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <unistd.h>

/* Defines */
#define MAXSLOTS 20
#define INVALID -999999

/* Types */
enum { Left, Right, Down, Up };
typedef int direction;
typedef struct {
	int nfingers;
	direction start;
	direction end;
	char *command;
} Gesture;

/* Config */
#include "config.h"

/* Globals */
Gesture *gestsarr;
int gestsarrlen;
direction pendingstart, pendingend;
double xstart[MAXSLOTS], xend[MAXSLOTS], ystart[MAXSLOTS], yend[MAXSLOTS];
unsigned fingsdown = 0, fingspending = 0;

void
die(char * msg)
{
	fprintf(stderr, msg);
	exit(1);
}

void
execcommand(char *c)
{
	system(c);
}

static int 
libinputopenrestricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
 
static void
libinputcloserestricted(int fd, void *user_data)
{
	close(fd);
}

void
touchdown(struct libinput_event *e)
{
	struct libinput_event_touch *tevent;
	int slot;
	
	tevent = libinput_event_get_touch_event(e);
	slot = libinput_event_touch_get_slot(tevent);

	xstart[slot] = libinput_event_touch_get_x(tevent);
	ystart[slot] = libinput_event_touch_get_y(tevent);
	fingsdown++;
}

void
touchmotion(struct libinput_event *e)
{
	struct libinput_event_touch *tevent;
	int slot;

	tevent = libinput_event_get_touch_event(e);
	slot = libinput_event_touch_get_slot(tevent);
	xend[slot] = libinput_event_touch_get_x(tevent);
	yend[slot] = libinput_event_touch_get_y(tevent);
}

void
touchup(struct libinput_event *e)
{
	struct libinput_event_touch *tevent;
	direction start;
	direction end;
	int i;
	int slot;

	tevent = libinput_event_get_touch_event(e);
	slot = libinput_event_touch_get_slot(tevent);

	fingsdown--;
	if (xend[slot] == INVALID || xstart[slot] == INVALID) return;

	if (verbose) {
		fprintf(
			stderr, 
			"(%d down fingers) (%d pending fingers) [start: x %lf y %lf] to [end x %lf y %lf]\n", 
			fingsdown, fingspending, xstart[slot], ystart[slot], xend[slot], yend[slot]
		);
	}

	if (xend[slot] > xstart[slot] && fabs(xend[slot] - xstart[slot]) > threshold) {
		start = Left;
		end = Right;
	} else if (xend[slot] < xstart[slot] && fabs(xend[slot] - xstart[slot]) > threshold) {
		start = Right;
		end = Left;
	} else if (yend[slot] > ystart[slot] && fabs(yend[slot] - ystart[slot]) > threshold) {
		start = Up;
		end = Down;
	} else if (yend[slot] < ystart[slot] && fabs(yend[slot] - ystart[slot]) > threshold) {
		start = Down;
		end = Up;
	} else {
		if (verbose) {
			fprintf(stderr, "Input didn't match a known gesture\n");
		}
		start = INVALID;
		end = INVALID;
	}

	if (fingspending == 0) {
		pendingstart = start;
		pendingend = end;
	}
	if (pendingstart == start && pendingend == end)	{
		fingspending++;
	}

	xend[slot] = INVALID;
	yend[slot] = INVALID;
	xstart[slot] = INVALID;
	ystart[slot] = INVALID;
	
	if (fingsdown == 0) {
		for (i = 0; i < gestsarrlen; i++) {
			if (verbose) {
				fprintf(stderr, 
					"[Fingers/Start/End]: Cfg (%d/%d/%d) <=> Evt (%d/%d/%d)\n", 
					gestsarr[i].nfingers, gestsarr[i].start, gestsarr[i].end, 
					fingspending, pendingstart, pendingend
				);
			}
			if (
				gestsarr[i].nfingers == fingspending && 
				gestsarr[i].start == pendingstart &&
				gestsarr[i].end == pendingend
			) {
				if (verbose) {
					fprintf(stderr, "Execute %s\n", gestsarr[i].command);
				}
				execcommand(gestsarr[i].command);
			}
		}
		fingspending = 0;
	}
}

void
run()
{
	struct libinput *li;
	struct libinput_event *event;
	struct libinput_event_touch *tevent;
	struct libinput_device *d;
	int selectresult;
	fd_set fdset;

	const static struct libinput_interface interface = {
		.open_restricted = libinputopenrestricted,
		.close_restricted = libinputcloserestricted,
	};

	li = libinput_path_create_context(&interface, NULL);

	if ((d = libinput_path_add_device(li, device)) == NULL) {
		die("Couldn't bind event from dev filesystem\n");
	} else if (LIBINPUT_CONFIG_STATUS_SUCCESS != libinput_device_config_send_events_set_mode(
		d, LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
	)) {
		die("Couldn't set mode to capture events\n");
	}

	FD_ZERO(&fdset);
	FD_SET(libinput_get_fd(li), &fdset);
	for (;;) {
		selectresult = select(FD_SETSIZE, &fdset, NULL, NULL, NULL);
		if (selectresult == -1) {
			die("Can't select on device node?\n");
		} else {
			libinput_dispatch(li);
			while ((event = libinput_get_event(li)) != NULL) {
				switch(libinput_event_get_type(event)) {
					case LIBINPUT_EVENT_TOUCH_DOWN: touchdown(event); break;
					case LIBINPUT_EVENT_TOUCH_UP: touchup(event); break;
					case LIBINPUT_EVENT_TOUCH_MOTION: touchmotion(event); break;
				}
				libinput_event_destroy(event);
			}
		}
	}
	libinput_unref(li);
}
 
int
main(int argc, char *argv[])
{
	int i, j;
	char *gestpt;

	gestsarr = malloc(0);
	gestsarrlen = 0;

	prctl(PR_SET_PDEATHSIG, SIGTERM);
	prctl(PR_SET_PDEATHSIG, SIGKILL);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "-d")) {
			device = argv[++i];
		} else if (!strcmp(argv[i], "-t")) {
			threshold = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-g")) {
			gestsarrlen++;
			realloc(gestsarr, (gestsarrlen * sizeof(Gesture)));
			gestpt = strtok(argv[++i], ",");
			for (j = 0; gestpt != NULL && j < 4;	gestpt = strtok(NULL, ","), j++) {
				switch(j) {
					case 0: gestsarr[gestsarrlen - 1].nfingers = atoi(gestpt); break;
					case 1: 
						if (!strcmp(gestpt, "l")) gestsarr[gestsarrlen-1].start = Left;
						if (!strcmp(gestpt, "r")) gestsarr[gestsarrlen-1].start = Right;
						if (!strcmp(gestpt, "d")) gestsarr[gestsarrlen-1].start = Down;
						if (!strcmp(gestpt, "u")) gestsarr[gestsarrlen-1].start = Up;
						break;
					case 2: 
						if (!strcmp(gestpt, "l")) gestsarr[gestsarrlen-1].end = Left;
						if (!strcmp(gestpt, "r")) gestsarr[gestsarrlen-1].end = Right;
						if (!strcmp(gestpt, "d")) gestsarr[gestsarrlen-1].end = Down;
						if (!strcmp(gestpt, "u")) gestsarr[gestsarrlen-1].end = Up;
						break;
					case 3: gestsarr[gestsarrlen - 1].command = gestpt; break;
				}
			}
		}
	}

	// E.g. no gestures passed on CLI - used gestures defined in config.def.h
	if (gestsarrlen == 0) {
		gestsarr = malloc(sizeof(gestures));
		gestsarrlen = sizeof(gestures) / sizeof(Gesture);
		memcpy(gestsarr, gestures, sizeof(gestures));
	}

	run();
	return 0;
}
