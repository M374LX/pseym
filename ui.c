/*
 * ui.c - User interface
 *
 * Copyright (C) 2020 M-374 LX <wilsalx@gmail.com>
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

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

//Virtual screen size
#define VSCREEN_WIDTH_CHARS 21
#define VSCREEN_HEIGHT_CHARS 16

#define CHAR_SIZE_PIXELS 8
#define VSCREEN_WIDTH_PIXELS (VSCREEN_WIDTH_CHARS * CHAR_SIZE_PIXELS)
#define VSCREEN_HEIGHT_PIXELS (VSCREEN_HEIGHT_CHARS * CHAR_SIZE_PIXELS)

//From charset.c
extern uint32_t charset[];

//From audio.c
void fm_write_reg(uint8_t reg, uint8_t val, uint8_t part);
void fm_key_on(uint8_t oct, uint8_t note, uint8_t chan);
void fm_key_off(uint8_t chan);
void fm_enable_notes();

//Instrument parameters
enum {
	TL  = 0,  //Total level
	SL  = 1,  //Sustain level
	AR  = 2,  //Attack rate
	DR  = 3,  //Decay rate
	SR  = 4,  //Sustain rate
	RR  = 5,  //Release rate
	MUL = 6,  //Multiply
	DT  = 7,  //Detune
	RS  = 8,  //Rate scaling
	FB  = 9,  //Feedback
	ALG = 10, //Algorithm
};

static const char* param_names[] = { 
	"TL", "SL", "AR", "DR", "SR", "RR", "MUL", "DT", "RS", "FB", "ALG"
};

//Offsets within the instr_params array for operator-independent parameters
#define FB_OFFS  36
#define ALG_OFFS 37

//The default sine wave instrument (in Echo's EIF format)
static const uint8_t instr_default[] = {
	0x00,
	0x01, 0x01, 0x01, 0x01,
	0x7F, 0x7F, 0x7F, 0x00,
	0x1F, 0x1F, 0x1F, 0x1F,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF,
};

//Parameters for the current instrument
static uint8_t instr_params[38];

//Keys pressed for the three FM sound channels that are used
static int pressed_keys[3];

//Current octave for note previewing, as shown at the bottom of the screen
static int octave;

//Selected operator and parameter
static int sel_op, sel_param;

//Virtual screen
static char vscreen[VSCREEN_WIDTH_CHARS][VSCREEN_HEIGHT_CHARS];
static SDL_Texture* vscreen_tex;

//Cursor position and width on the screen
static int cursor_x, cursor_y, cursor_w;

static SDL_Window* win = NULL;
static SDL_Renderer* renderer = NULL;

//Draw a character from the character set
static void draw_char(char ch, int x, int y, bool reverse)
{
	int sx, sy;

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

	for (sy = 0; sy < 8; sy++) {
		int row = charset[(int)(ch - ' ') * 8 + sy];

		for (sx = 0; sx < 8; sx++) {
			bool has_pixel = (row & 0xF) != 0;

			if (reverse) {
				has_pixel = !has_pixel;
			}

			if (has_pixel) {
				int px = (x * 8) + (8 - sx);
				int py = (y * 8) + sy;

				SDL_RenderDrawPoint(renderer, px, py);
			}

			row >>= 4;
		}
	}
}

//Write a string on the virtual screen
static void write_str(const char* str, int x, int y)
{
	int i;

	for (i = 0; i < VSCREEN_WIDTH_CHARS; i++) {
		if (str[i] == '\0') {
			return;
		}

		vscreen[x + i][y] = str[i];
	}
}

//Write a hexadecimal number on the virtual screen
static void write_hex(int hex, int num_digits, int x, int y)
{
	int i;

	for (i = 0; i < num_digits; i++) {
		int nybble = (hex & 0xF);
		char c;

		if (nybble < 0xA) {
			c = nybble + '0';
		} else {
			c = nybble - 10 + 'A';
		}

		vscreen[x + (num_digits - 1) - i][y] = c;

		hex >>= 4;
	}
}

//Write an EIF instrument to the YM2612 registers
static void write_eif_regs(const uint8_t* instr, uint8_t chan) {
	uint8_t part = 0;
	int i;

	for (i = 0; i < 6; i++) {
		uint8_t reg = 0x30 + (i << 4) + chan;

		fm_write_reg(reg + 0,  instr[(i << 2) + 1], part);
		fm_write_reg(reg + 4,  instr[(i << 2) + 2], part);
		fm_write_reg(reg + 8,  instr[(i << 2) + 3], part);
		fm_write_reg(reg + 12, instr[(i << 2) + 4], part);
	}

	fm_write_reg(0xB0 + (chan & 0x3), instr[0], part);
	fm_write_reg(0xB4 + (chan & 0x3), 0xC0, part);
}

//Load an EIF instrument from an array
static void load_eif(const uint8_t* instr)
{
	int op;

	for (op = 0; op < 4; op++) {
		int op_screen = op;

		//Internal register ordering for operators (0, 2, 1, 3)
		if (op == 1) {
			op_screen = 2;
		} else if (op == 2) {
			op_screen = 1;
		}

		instr_params[MUL * 4 + op_screen] =  instr[1  + op] & 0x0F;
		instr_params[DT  * 4 + op_screen] = (instr[1  + op] >> 4) & 0x07;
		instr_params[TL  * 4 + op_screen] =  instr[5  + op] & 0x7F;
		instr_params[AR  * 4 + op_screen] =  instr[9  + op] & 0x1F;
		instr_params[RS  * 4 + op_screen] = (instr[9  + op] >> 6) & 0x03;
		instr_params[DR  * 4 + op_screen] =  instr[13 + op] & 0x1F;
		instr_params[SR  * 4 + op_screen] =  instr[17 + op] & 0x1F;
		instr_params[SL  * 4 + op_screen] = (instr[21 + op] >> 4) & 0x0F;
		instr_params[RR  * 4 + op_screen] =  instr[21 + op] & 0x0F;
	}
	instr_params[FB_OFFS]  = (instr[0] >> 3) & 0x07;
	instr_params[ALG_OFFS] = instr[0] & 0x07;

	//Load the instrument on the first three channels
	write_eif_regs(instr, 0);
	write_eif_regs(instr, 1);
	write_eif_regs(instr, 2);
}

//Save the current instrument as EIF into an array
static void save_eif(uint8_t* dest)
{
	int op_screen;

	dest[0] = instr_params[ALG_OFFS] | (instr_params[FB_OFFS] << 3);

	for (op_screen = 0; op_screen < 4; op_screen++) {
		int op = op_screen;

		//Internal register ordering for operators (0, 2, 1, 3)
		if (op_screen == 1) {
			op = 2;
		} else if (op_screen == 2) {
			op = 1;
		}

		dest[1  + op]  = instr_params[MUL * 4 + op_screen];
		dest[1  + op] |= (instr_params[DT * 4 + op_screen] << 4);
		dest[5  + op]  = instr_params[TL  * 4 + op_screen];
		dest[9  + op]  = instr_params[AR  * 4 + op_screen];
		dest[9  + op] |= (instr_params[RS * 4 + op_screen] << 6);
		dest[13 + op]  = instr_params[DR  * 4 + op_screen];
		dest[17 + op]  = instr_params[SR  * 4 + op_screen];
		dest[21 + op]  = instr_params[RR  * 4 + op_screen];
		dest[21 + op] |= (instr_params[SL * 4 + op_screen] << 4);
		dest[25 + op]  = 0; //SSG-EG (currently unsupported)
	}
}

//Change the currently selected instrument parameter
static void change_param(bool inc, bool high_nybble, bool to_max)
{
	uint8_t op = sel_op;
	uint8_t max = 0;
	uint8_t reg;
	int val;
	int offs;

	//Determine maximum value for selected parameter
	switch (sel_param) {
		case AR:  max = 0x1F; break;
		case DR:  max = 0x1F; break;
		case SR:  max = 0x1F; break;
		case SL:  max = 0x0F; break;
		case RR:  max = 0x0F; break;
		case TL:  max = 0x7F; break;
		case MUL: max = 0x0F; break;
		case DT:  max = 0x07; break;
		case RS:  max = 0x03; break;
		case FB:  max = 0x07; break;
		case ALG: max = 0x07; break;
	}

	if (sel_param == FB) {
		offs = FB_OFFS;
	} else if (sel_param == ALG) {
		offs = ALG_OFFS;
	} else {
		offs = sel_param * 4 + op;
	}

	val = instr_params[offs];

	if (inc) {
		val += high_nybble ? 0x10 : 1;

		if (to_max || val > max) {
			val = max;
		}
	} else {
		val -= high_nybble ? 0x10 : 1;

		if (to_max || val < 0) {
			val = 0;
		}
	}

	instr_params[offs] = val;

	//Determine the YM2612 register to write to and the value
	switch (sel_param) {
		case DR:
			reg = 0x60;
			val = instr_params[DR * 4 + op];
			break;

		case SR:
			reg = 0x70;
			val = instr_params[SR * 4 + op];
			break;

		case TL:
			reg = 0x40;
			val = instr_params[TL * 4 + op];
			break;

		case RR:
		case SL:
			reg = 0x80;
			val = instr_params[RR * 4 + op] | (instr_params[SL * 4 + op] << 4);
			break;

		case MUL:
		case DT:
			reg = 0x30;
			val = instr_params[MUL * 4 + op] | (instr_params[DT * 4 + op] << 4);
			break;

		case RS:
		case AR:
			reg = 0x50;
			val = instr_params[AR * 4 + op] | (instr_params[RS * 4 + op] << 6);
			break;

		case FB:
		case ALG:
			reg = 0xB0;
			val = instr_params[ALG_OFFS] | (instr_params[FB_OFFS] << 3);
			break;
	}

	//Adjust the register number to the corresponding operator
	if (sel_param != FB && sel_param != ALG) {
		//Internal YM2612 register numbering for operators (0, 2, 1, 3)
		if (sel_op == 1) {
			op = 2;
		} else if (sel_op == 2) {
			op = 1;
		}

		reg += op << 2;
	}

	//Write the value to the first three channels
	fm_write_reg(reg + 0, val, 0);
	fm_write_reg(reg + 1, val, 0);
	fm_write_reg(reg + 2, val, 0);
}

//Load an EIF instrument from a file
static bool load_eif_file(const char* filename)
{
	uint8_t bytes[29];
	int num_bytes = 0;
	FILE* f = fopen(filename, "r");

	if (f == NULL) {
		return false;
	}

	while (num_bytes < 29) {
		if (feof(f) || fread(&bytes[num_bytes], 1, 1, f) != 1) {
			fclose(f);
			return false;
		}

		num_bytes++;
	}

	load_eif(bytes);

	fclose(f);

	return true;
}

//Save the current instrument as an EIF file
static bool save_eif_file(const char* filename)
{
	uint8_t bytes[29];
	int num_bytes;
	FILE* f = fopen(filename, "w");

	if (f == NULL) {
		return false;
	}

	save_eif(bytes);

	for (num_bytes = 0; num_bytes < 29; num_bytes++) {
		if (fwrite(&bytes[num_bytes], 1, 1, f) != 1) {
			fclose(f);
			return false;
		}
	}

	fclose(f);

	return true;
}

//Instrument key release
static void key_off(int key)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (pressed_keys[i] == key) {
			pressed_keys[i] = -1;
			fm_key_off(i);
			return;
		}
	}
}

//Instrument key press (play a note)
static void key_on(int key, int oct, int note)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (pressed_keys[i] == -1) {
			pressed_keys[i] = key;
			fm_key_on(oct, note, i);
			return;
		}
	}
}

//Handle a key press
static void key_press(int sym, bool shift, bool ctrl, bool repeat)
{
	switch (sym) {
		case SDLK_UP:
			if (sel_param > 0) {
				sel_param--;
			}
			break;

		case SDLK_DOWN:
			if (sel_param < ALG) {
				sel_param++;
			}
			if (sel_op != 0 && (sel_param == FB || sel_param == ALG)) {
				sel_param = RS;
			}
			break;

		case SDLK_LEFT:
			if (sel_op > 0 && sel_param != FB && sel_param != ALG) {
				sel_op--;
			}
			break;

		case SDLK_RIGHT:
			if (sel_op < 3 && sel_param != FB && sel_param != ALG) {
				sel_op++;
			}
			break;

		case SDLK_HOME:
			if (shift) {
				sel_op = 0;
			} else {
				sel_param = 0;
			}
			break;

		case SDLK_END:
			if (shift) {
				if (sel_param != FB && sel_param != ALG) {
					sel_op = 3;
				}
			} else {
				sel_param = ALG;

				if (sel_op != 0) {
					sel_param -= 2;
				}
			}
			break;

		case SDLK_PAGEUP:
			change_param(true, shift, ctrl);
			break;

		case SDLK_PAGEDOWN:
			change_param(false, shift, ctrl);
			break;

		case SDLK_EQUALS:
			if (octave < 7) {
				octave++;
			}
			break;

		case SDLK_MINUS:
			if (octave > 0) {
				octave--;
			}
			break;

		case SDLK_F5:
			save_eif_file("instr.eif");
			break;
	}

	if (!repeat) {
		int oct = octave;
		int note = -1;

		switch (sym) {
			case SDLK_z: note = 0;  oct += 0; break;
			case SDLK_s: note = 1;  oct += 0; break;
			case SDLK_x: note = 2;  oct += 0; break;
			case SDLK_d: note = 3;  oct += 0; break;
			case SDLK_c: note = 4;  oct += 0; break;
			case SDLK_v: note = 5;  oct += 0; break;
			case SDLK_g: note = 6;  oct += 0; break;
			case SDLK_b: note = 7;  oct += 0; break;
			case SDLK_h: note = 8;  oct += 0; break;
			case SDLK_n: note = 9;  oct += 0; break;
			case SDLK_j: note = 10; oct += 0; break;
			case SDLK_m: note = 11; oct += 0; break;
			case SDLK_COMMA: note = 0;  oct += 1; break;
			case SDLK_l: note = 1;  oct += 1; break;
			case SDLK_PERIOD: note = 2;  oct += 1; break;

			case SDLK_q: note = 0;  oct += 1; break;
			case SDLK_2: note = 1;  oct += 1; break;
			case SDLK_w: note = 2;  oct += 1; break;
			case SDLK_3: note = 3;  oct += 1; break;
			case SDLK_e: note = 4;  oct += 1; break;
			case SDLK_r: note = 5;  oct += 1; break;
			case SDLK_5: note = 6;  oct += 1; break;
			case SDLK_t: note = 7;  oct += 1; break;
			case SDLK_6: note = 8;  oct += 1; break;
			case SDLK_y: note = 9;  oct += 1; break;
			case SDLK_7: note = 10; oct += 1; break;
			case SDLK_u: note = 11; oct += 1; break;
			case SDLK_i: note = 0;  oct += 2; break;
			case SDLK_9: note = 1;  oct += 2; break;
			case SDLK_o: note = 2;  oct += 2; break;
			case SDLK_0: note = 3;  oct += 2; break;
			case SDLK_p: note = 4;  oct += 2; break;
		}

		if (note != -1) {
			key_on(sym, oct, note);
		}
	}
}

//Initialize the user interface
bool ui_init()
{
	int i, j;

	win = SDL_CreateWindow("Pseym",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			VSCREEN_WIDTH_PIXELS * 3, VSCREEN_HEIGHT_PIXELS * 3,
			SDL_WINDOW_SHOWN);

	if (win == NULL) {
		printf("Failed to create window.\n");
		return false;
	}

	renderer = SDL_CreateRenderer(win, -1, 0);

	if (renderer == NULL) {
		printf("Failed to create renderer.\n");
		return false;
	}

	vscreen_tex = SDL_CreateTexture(renderer,
			SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET,
			VSCREEN_WIDTH_PIXELS, VSCREEN_HEIGHT_PIXELS);

	if (vscreen_tex == NULL) {
		printf("Unable to create virtual screen.\n");
		return false;
	}

	pressed_keys[0] = -1;
	pressed_keys[1] = -1;
	pressed_keys[2] = -1;

	sel_op = 0;
	sel_param = 0;
	octave = 3;

	//Clear virtual screen with spaces
	for (i = 0; i < VSCREEN_WIDTH_CHARS; i++) {
		for (j = 0; j < VSCREEN_HEIGHT_CHARS; j++) {
			vscreen[i][j] = ' ';
		}
	}

	load_eif(instr_default);
	load_eif_file("instr.eif");
	fm_enable_notes();

	return true;
}

//Free user interface resources at program's end
void ui_shutdown()
{
	SDL_DestroyTexture(vscreen_tex);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(win);
}

//Handle events
bool ui_handle_events()
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev) != 0) {
		if (ev.type == SDL_QUIT) {
			return false;
		} else if (ev.type == SDL_KEYDOWN) {
			int sym = ev.key.keysym.sym;
			bool repeat = (ev.key.repeat != 0);
			bool shift = (ev.key.keysym.mod & KMOD_SHIFT);
			bool ctrl = (ev.key.keysym.mod & KMOD_CTRL);

			if (sym == SDLK_ESCAPE) {
				return false;
			}

			key_press(sym, shift, ctrl, repeat);
		} else if (ev.type == SDL_KEYUP) {
			key_off(ev.key.keysym.sym);
		}
	}

	return true;
}

//Draw the user interface
void ui_draw()
{
	int param, line;

	cursor_x = 6 + (sel_op * 4);
	cursor_y = 2 + sel_param;
	cursor_w = (sel_param == FB || sel_param == ALG) ? 1 : 2;

	//Show top line, with operator numbers
	write_str("OP   1   2   3   4", 1, 1);

	//Show parameter names
	for (param = 0; param < 11; param++) {
		write_str(param_names[param], 1, param + 2);
	}

	//Show values for operator-dependent parameters
	for (param = 0; param < 9; param++) {
		write_hex(instr_params[param * 4 + 0], 2, 6,  param + 2);
		write_hex(instr_params[param * 4 + 1], 2, 10, param + 2);
		write_hex(instr_params[param * 4 + 2], 2, 14, param + 2);
		write_hex(instr_params[param * 4 + 3], 2, 18, param + 2);
	}

	//Show values for operator-independent parameters
	write_hex(instr_params[FB_OFFS], 1, 6, 11);
	write_hex(instr_params[ALG_OFFS], 1, 6, 12);

	//Show octave
	write_str("OCT. ", 1, 14);
	write_hex(octave, 1, 6, 14);

	SDL_SetRenderTarget(renderer, vscreen_tex);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	//Draw characters from virtual screen
	for (line = 0; line < VSCREEN_HEIGHT_CHARS; line++) {
		int cursor_max = cursor_x + cursor_w - 1;
		int i;

		if (cursor_x < 0) {
			cursor_max = -1;
		}

		for (i = 0; i < VSCREEN_WIDTH_CHARS; i++) {
			bool reverse = false;

			if (line == cursor_y) {
				if (i >= cursor_x && i <= cursor_max) {
					reverse = true;
				}
			}

			draw_char(vscreen[i][line], i, line, reverse);
		}
	}

	SDL_SetRenderTarget(renderer, NULL);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, vscreen_tex, NULL, NULL);
	SDL_RenderPresent(renderer);
}

