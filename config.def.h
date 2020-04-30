/* Minimum cutoff for a gestures to take effect */
unsigned int threshold = 300;

/* Verbose mode, 1 = enabled, 0 = disabled */
int verbose = 0;

/* Device libinput should read from */
char *device = "/dev/input/event1";

/* Commands to execute upon recieving a swipe gesture */
Gesture gestures[] = {
	/* fingers 	start	end	command */
	{ 1, 	Left,	Right,	  "xdotool key --clearmodifiers Alt+Shift+e" },
	{ 1, 	Right,	Left,	  "xdotool key --clearmodifiers Alt+Shift+r" },

	{ 2, 	Left,	Right,	  "xdotool key --clearmodifiers Alt+e" },
	{ 2, 	Right,	Left,	  "xdotool key --clearmodifiers Alt+r" },

	{ 2, 	Down,	Up,	  "pidof svkbd-sxmo || svkbd-sxmo &" },
	{ 2, 	Up,	Down,	  "pkill -9 svkbd-sxmo" },

	{ 3, 	Down,	Up,	  "sxmo_vol.sh up" },
	{ 3, 	Up,	Down,	  "sxmo_vol.sh down" },

	{ 4, 	Down,	Up,	  "sxmo_brightness.sh up" },
	{ 4, 	Up,	Down,	  "sxmo_brightness.sh down" },
};
