/*
 * main.c - Main program's entry point
 *
 * Copyright (C) 2020-2022 M-374 LX <wilsalx@gmail.com>
 *
 * This file is part of Pseym.
 *
 * Pseym is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Pseym is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Pseym; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#define VERSION "0.2"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

//From ui.c
bool ui_init();
void ui_shutdown();
bool ui_handle_events();
void ui_draw();

//From audio.c
bool audio_init();
void audio_shutdown();
bool audio_buffer_full();
void audio_update();

void help(const char* progname)
{
	const char* str =
	"Options:\n"
	"-h      Show this usage information and exit.\n"
	"-v      Show version and license information and exit.\n"
	"\n"
	"By not using any options, the program runs in normal UI mode. The list of\n"
	"keys can be found in the file README.md.\n\n";
	
#ifdef _WIN32
	MessageBox(NULL, str, "Pseym", 0);
#else
	printf("Usage: %s [options]\n\n", progname);
	printf(str);
#endif
}

void version()
{
	const char* str = 
	"Pseym " VERSION "\n"
	"\n"
	"Copyright (C) 2020-2022 M-374 LX <wilsalx@gmail.com>\n"
	"\n"
	"For a more complete list of authors, see the file AUTHORS.\n"
	"\n"
	"Pseym is free software; you can redistribute it and/or modify\n"
	"it under the terms of the GNU General Public License as published by\n"
	"the Free Software Foundation; either version 2 of the License, or\n"
	"(at your option) any later version.\n"
	"\n"
	"Pseym is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU General Public License along\n"
	"with Pseym; if not, write to the Free Software Foundation, Inc.,\n"
	"51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.\n"
	"\n";
#ifdef _WIN32
	MessageBox(NULL, str, "Pseym", 0);
#else
	printf(str);
#endif
}

int main(int argc, char* argv[])
{
	bool quit = false;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case 'h':
					help(argv[0]);
					return 0;

				case 'v':
					version();
					return 0;
			}
		}
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		printf("SDL initialization failed.\n");
		return 1;
	}

	if (!audio_init()) {
		printf("Failed to initialize audio.\n");
		return 1;
	}

	if (!ui_init()) {
		return 1;
	}

	while (!quit) {
		quit = !ui_handle_events();

		if (!audio_buffer_full()) {
			ui_draw();
			audio_update();
		}
	}

	audio_shutdown();
	ui_shutdown();

	SDL_Quit();

	return 0;
}

