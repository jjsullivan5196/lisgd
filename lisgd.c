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
#include <time.h>
#include <unistd.h>

/* Defines */
#define MAXSLOTS 20
#define NOMOTION -999999

/* Types */
enum {
  SwipeDU,
  SwipeUD,
  SwipeLR,
  SwipeRL,
  SwipeDLUR,
  SwipeDRUL,
  SwipeURDL,
  SwipeULDR
};

typedef int Swipe;
typedef struct {
	int nfswipe;
	Swipe swipe;
	char *command;
} Gesture;

/* Config */
#include "config.h"

/* Globals */
Gesture *gestsarr;
int gestsarrlen;
Swipe pendingswipe;
double xstart[MAXSLOTS], xend[MAXSLOTS], ystart[MAXSLOTS], yend[MAXSLOTS];
unsigned nfdown = 0, nfpendingswipe = 0;
struct timespec timedown;

void
die(char * msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

void
execcommand(char *c)
{
	system(c);
}

int
gesturecalculateswipewithindegrees(double gestdegrees, double wantdegrees) {
	return (
		gestdegrees >= wantdegrees - degreesleniency &&
		gestdegrees <= wantdegrees + degreesleniency
	);
}

Swipe
gesturecalculateswipe(double x0, double y0, double x1, double y1) {
	double t, degrees, dist;

	t = atan2(x1 - x0, y0 - y1);
	degrees = 57.2957795130823209 * (t < 0 ? t + 6.2831853071795865 : t);
	dist = sqrt(pow(x1 - x0, 2) + pow(y1 - y0, 2));

	if (verbose)
		fprintf(stderr, "Swipe distance=[%.2f]; degrees=[%.2f]\n", dist, degrees);

	if (dist < distancethreshold) return -1;
	else if (gesturecalculateswipewithindegrees(degrees, 0))   return SwipeDU;
	else if (gesturecalculateswipewithindegrees(degrees, 45))  return SwipeDLUR;
	else if (gesturecalculateswipewithindegrees(degrees, 90))  return SwipeLR;
	else if (gesturecalculateswipewithindegrees(degrees, 135)) return SwipeULDR;
	else if (gesturecalculateswipewithindegrees(degrees, 180)) return SwipeUD;
	else if (gesturecalculateswipewithindegrees(degrees, 225)) return SwipeURDL;
	else if (gesturecalculateswipewithindegrees(degrees, 270)) return SwipeRL;
	else if (gesturecalculateswipewithindegrees(degrees, 315)) return SwipeDRUL;
	else if (gesturecalculateswipewithindegrees(degrees, 360)) return SwipeDU;

	return -1;
}

void
gestureexecute(Swipe swipe, int nfingers) {
	int i;

	for (i = 0; i < gestsarrlen; i++) {
		if (verbose) {
			fprintf(stderr, 
				"[Nfswipe/SwipeId]: Cfg (%d/%d) <=> Evt (%d/%d)\n", 
				gestsarr[i].nfswipe, gestsarr[i].swipe, nfingers, swipe
			);
		}
		if (gestsarr[i].nfswipe == nfingers && gestsarr[i].swipe == swipe) {
			if (verbose) fprintf(stderr, "Execute %s\n", gestsarr[i].command);
			execcommand(gestsarr[i].command);
		}
	}
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

Swipe
swipereorient(Swipe swipe, int orientation) {
	while (orientation > 0) {
		switch(swipe) {
			// 90deg per turn so: L->U, R->D, U->R, D->L
			case SwipeDU:   swipe = SwipeLR; break;
			case SwipeDLUR: swipe = SwipeULDR; break;
			case SwipeLR:   swipe = SwipeUD; break;
			case SwipeULDR: swipe = SwipeURDL; break;
			case SwipeUD:   swipe = SwipeRL; break;
			case SwipeURDL: swipe = SwipeDRUL; break;
			case SwipeRL:   swipe = SwipeDU; break;
			case SwipeDRUL: swipe = SwipeDLUR; break;
		}
		orientation--;
	}
	return swipe;
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
	if (nfdown == 0) clock_gettime(CLOCK_MONOTONIC_RAW, &timedown);
	nfdown++;
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
resetslot(int slot) {
	xend[slot] = NOMOTION;
	yend[slot] = NOMOTION;
	xstart[slot] = NOMOTION;
	ystart[slot] = NOMOTION;
}

void
touchup(struct libinput_event *e)
{
	int i;
	int slot;
	struct libinput_event_touch *tevent;
	struct timespec now;

	tevent = libinput_event_get_touch_event(e);
	slot = libinput_event_touch_get_slot(tevent);
	nfdown--;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);

	// E.g. invalid motion, it didn't begin/end from anywhere
	if (
		xstart[slot] == NOMOTION || ystart[slot] == NOMOTION ||
		xend[slot] == NOMOTION || yend[slot] == NOMOTION
	) return;

	Swipe swipe = gesturecalculateswipe(
		xstart[slot], ystart[slot], xend[slot], yend[slot]
	);
	if (nfpendingswipe == 0) pendingswipe = swipe;
	if (pendingswipe == swipe) nfpendingswipe++;
	resetslot(slot);

	// All fingers up - check if within milisecond limit, exec, & reset
	if (nfdown == 0) {
		if (
			timeoutms > 
			((now.tv_sec - timedown.tv_sec) * 1000000 + (now.tv_nsec - timedown.tv_nsec) / 1000) / 1000
		) gestureexecute(swipe, nfpendingswipe);
		
		nfpendingswipe = 0;
	}
}

void
run()
{
	int i;
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
		die("Couldn't bind event from dev filesystem");
	} else if (LIBINPUT_CONFIG_STATUS_SUCCESS != libinput_device_config_send_events_set_mode(
		d, LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
	)) {
		die("Couldn't set mode to capture events");
	}

	// E.g. initially invalidate every slot 
	for (i = 0; i < MAXSLOTS; i++) {
		xend[i] = NOMOTION;
		yend[i] = NOMOTION;
		xstart[i] = NOMOTION;
		ystart[i] = NOMOTION;
	}

	FD_ZERO(&fdset);
	FD_SET(libinput_get_fd(li), &fdset);
	for (;;) {
		selectresult = select(FD_SETSIZE, &fdset, NULL, NULL, NULL);
		if (selectresult == -1) {
			die("Can't select on device node?");
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
			distancethreshold = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-r")) {
			degreesleniency = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-m")) {
			timeoutms = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-o")) {
			orientation = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "-g")) {
			gestsarrlen++;
			realloc(gestsarr, (gestsarrlen * sizeof(Gesture)));
			gestpt = strtok(argv[++i], ",");
			for (j = 0; gestpt != NULL && j < 3;	gestpt = strtok(NULL, ","), j++) {
				switch(j) {
					case 0: gestsarr[gestsarrlen - 1].nfswipe = atoi(gestpt); break;
					case 1: 
						if (!strcmp(gestpt, "LR")) gestsarr[gestsarrlen-1].swipe = SwipeLR;
						if (!strcmp(gestpt, "RL")) gestsarr[gestsarrlen-1].swipe = SwipeRL;
						if (!strcmp(gestpt, "DU")) gestsarr[gestsarrlen-1].swipe = SwipeDU;
						if (!strcmp(gestpt, "UD")) gestsarr[gestsarrlen-1].swipe = SwipeUD;
						if (!strcmp(gestpt, "DLUR")) gestsarr[gestsarrlen-1].swipe = SwipeDLUR;
						if (!strcmp(gestpt, "URDL")) gestsarr[gestsarrlen-1].swipe = SwipeURDL;
						if (!strcmp(gestpt, "ULDR")) gestsarr[gestsarrlen-1].swipe = SwipeULDR;
						if (!strcmp(gestpt, "DRUL")) gestsarr[gestsarrlen-1].swipe = SwipeDRUL;
						break;
					case 2: gestsarr[gestsarrlen - 1].command = gestpt; break;
				}
			}
		} else {
			fprintf(stderr, "lisgd [-v] [-d /dev/input/0] [-o 0] [-t 200] [-r 20] [-m 400] [-g '1,LR,notify-send swiped left to right']\n");
			exit(1);
		}
	}

	// E.g. no gestures passed on CLI - used gestures defined in config.def.h
	if (gestsarrlen == 0) {
		gestsarr = malloc(sizeof(gestures));
		gestsarrlen = sizeof(gestures) / sizeof(Gesture);
		memcpy(gestsarr, gestures, sizeof(gestures));
	}

  // Modify gestures swipes based on orientation provided
	for (i = 0; i < gestsarrlen; i++)
		gestsarr[i].swipe = swipereorient(gestsarr[i].swipe, orientation);

	run();
	return 0;
}
