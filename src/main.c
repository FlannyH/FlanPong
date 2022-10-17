#include <gb/gb.h> 			// Game Boy hardware functions
#include <stdint.h>			// Standard library integers (uint8_t beloved)
#include <gbdk/incbin.h>	// To be able to include binary files into our ROM

#define PADDLE_HEIGHT 10
#define SCORE_MAX 5

// Include graphics binary
INCBIN(graphics, "res/graphics.2bpp")
INCBIN_EXTERN(graphics)

// Global variables
static uint8_t player_y = 72;
static uint8_t opponent_y = 72;
static uint8_t opponent_timer = 1;
static uint8_t opponent_direction = 0;
static uint8_t score_player = 0;
static uint8_t score_opponent = 0;
static uint8_t wait_timer = 0;
static uint8_t waiting_for_start = 0;
static uint8_t fade_in_timer = 0;
const uint8_t opponent_timer_length = 20;
static uint8_t curr_sprite = 0;
static int16_t ball_vel_x = -0x0100; // Fixed point x velocity of the ball
static int16_t ball_vel_y = 0x0000;
static uint16_t ball_pos_x = 0x5400; // Fixed point x position of the ball
static uint16_t ball_pos_y = 0x5400;

	const uint8_t null_array[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Sprite helper function
inline void draw_sprite(uint8_t x, uint8_t y, uint8_t tile, uint8_t prop) {
	move_sprite(curr_sprite, x, y);
	set_sprite_tile(curr_sprite, tile);
	set_sprite_prop(curr_sprite, prop);
	++curr_sprite;
}

// Empty the other sprite slots
inline void flush_sprites() {
	while (curr_sprite < 40) {
		move_sprite(curr_sprite++, 0, 0);
	}
	curr_sprite = 0;
}

// Play sound at certain frequency
inline void play_beep(uint16_t freq, uint8_t vol, uint8_t env, uint8_t duty, uint8_t len) {
	NR11_REG = (duty << 6) | len;
	NR12_REG = (vol << 4) | env;
	NR13_REG = freq & 0xFF;
	NR14_REG = (freq >> 8) | 0xC0;
}

// Player update function
inline void update_player() {
	// If up is held down, move the paddle upwards
	if (joypad() & J_UP) {
		player_y -= 1 +  + (sys_time % 2);
		// Clamp top
		if (player_y < 28) {
			player_y = 28;
		}
	}
	// If down is held down, move the paddle downwards
	if (joypad() & J_DOWN) {
		player_y += 1 + (sys_time % 2);
		// Clamp bottom
		if (player_y > 140) {
			player_y = 140;
		}
	}
}

inline void update_opponent() {
	// We wait on a timer so it doesn't just lock on perfectly, but the enemy can make mistakes.
	// If the timer triggers
	if (--opponent_timer == 0) {
		// Reset the timer
		opponent_timer = opponent_timer_length;

		// Pick a direction based on where the ball is currently
		uint8_t ball_pos_y_8 = ball_pos_y >> 8;
		if (ball_pos_y_8 > opponent_y + 8) {
			opponent_direction = 2;
		}
		else if (ball_pos_y_8 < opponent_y - 8) {
			opponent_direction = 1;
		}
		else {
			opponent_timer >>= 2;
			opponent_direction = 0;
		}
	}

	if (opponent_direction == 1) {
		opponent_y -= 1 + (sys_time % 2);
		// Clamp top
		if (opponent_y < 28) {
			opponent_y = 28;
		}
	}
	else if (opponent_direction == 2){
		opponent_y += 1 + (sys_time % 2);
		// Clamp bottom
		if (opponent_y > 140) {
			opponent_y = 140;
		}
	}
}

inline void update_ball() {
	// Move
	ball_pos_x += ball_vel_x;
	ball_pos_y += ball_vel_y;

	// Bounce against top and bottom
	if (ball_pos_y >> 8 < 28) {
		ball_pos_y = 28 << 8;
		// Make sure it moves away from the wall
		if (ball_vel_y < 0) {
			ball_vel_y = -ball_vel_y;
		}
		ball_pos_y += ball_vel_y;

		// And play a beep
		play_beep(0x6E0, 15, 0, 3, 54);
	}
	else if (ball_pos_y >> 8 > 140) {
		ball_pos_y = 140U << 8;
		// Make sure it moves away from the wall
		if (ball_vel_y > 0) {
			ball_vel_y = -ball_vel_y;
		}
		ball_pos_y += ball_vel_y;

		// And play a beep
		play_beep(0x6E0, 15, 0, 3, 54);
	}

	// Handle collision with the player
	const int16_t bounce_lut[22] = { 0x0160, 0x0150, 0x0140, 0x0130, 0x0120, 0x00D0, 0x00A0, 0x0080, 0x0060, 0x0040, 0x0020, -0x0020, -0x0040, -0x0060, -0x0080, -0x00A0, -0x00D0, -0x0120, -0x0130, -0x0140, -0x0150, -0x0160 };
	const uint8_t ball_pos_x_8 = ball_pos_x >> 8;
	const uint8_t ball_pos_y_8 = ball_pos_y >> 8;
	if (ball_vel_x < 0) {
		// If the ball is colliding on the X-axis
		if (ball_pos_x_8 < 0x16 && ball_pos_x_8 > (0x10)) {
			// And the ball is colliding on the Y-axis
			if (ball_pos_y_8 < (player_y + PADDLE_HEIGHT) && ball_pos_y_8 > (player_y - PADDLE_HEIGHT)) {
				// Make the ball bounce to the right
				if (ball_vel_x < 0) {
					ball_vel_x = -ball_vel_x;
				}

				// And make the ball bounce upwards or downwards based on where it hit the paddle, using this look-up table
				ball_vel_y = bounce_lut[(11 + player_y - ball_pos_y_8)];

				// And play a beep
				play_beep(0x5C0, 15, 0, 3, 54);
			}
		}
	}

	// Handle collision with the opponent
	else {
		// If the ball is colliding on the X-axis
		if (ball_pos_x_8 < (0x16 + 128) && ball_pos_x_8 > (0x10 + 128)) {
			// And the ball is colliding on the Y-axis
			if (ball_pos_y_8 < (opponent_y + PADDLE_HEIGHT) && ball_pos_y_8 > (opponent_y - PADDLE_HEIGHT)) {
				// Make the ball bounce to the left
				if (ball_vel_x > 0) {
					ball_vel_x = -ball_vel_x;
				}

				// And make the ball bounce upwards or downwards based on where it hit the paddle, using this look-up table
				ball_vel_y = bounce_lut[(11 + opponent_y - ball_pos_y_8)];

				// And play a beep
				play_beep(0x5C0, 15, 0, 3, 54);
			}
		}
	}

	// If the player misses the ball
	if (ball_pos_x_8 < 16) {
		// Increase the score of the opponent
		++score_opponent;

		// Reset the ball to the center
		ball_pos_x = 0x5400;
		ball_pos_y = 0x5400;
		ball_vel_y >>= 2;

		// Wait 120 frames to give the player some breathing room (this is very scuffed, but hey it works)
		wait_timer = 120;

		// And play a beep
		play_beep(0x6EF, 15, 0, 3, 1);
	}
	// If the enemy misses the ball
	else if (ball_pos_x_8 > 152) {
		// Increase the score of the player
		++score_player;

		// Reset the ball to the center
		ball_pos_x = 0x5400;
		ball_pos_y = 0x5400;
		ball_vel_y >>= 2;

		// Wait 120 frames to give the player some breathing room (this is very scuffed, but hey it works)
		wait_timer = 120;

		// And play a beep
		play_beep(0x6EF, 15, 0, 3, 1);
	}
}

inline void handle_win() {
	const uint8_t you_array[] =  { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };
	const uint8_t win_array[] =  { 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D };
	const uint8_t lose_array[] = { 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25 };
	if (score_player == SCORE_MAX) {
		// Display text
		set_bkg_tiles(5, 7, 3, 2, you_array);
		set_bkg_tiles(11, 7, 4, 2, win_array);

		// Delete all sprites
		curr_sprite = 0;
		flush_sprites();

		// Wait until the player presses start
		while ((joypad() & J_START) == 0) {
			draw_sprite(56, 92, 0x32, 0);
			draw_sprite(64, 92, 0x33, 0);
			draw_sprite(72, 92, 0x34, 0);
			draw_sprite(96, 92, 0x35, 0);
			draw_sprite(104, 92, 0x36, 0);
			draw_sprite(112, 92, 0x37, 0);
			flush_sprites();
			wait_vbl_done();
		}

		while (joypad() & J_START) {
			wait_vbl_done();
		}
		
		// Remove the text
		set_bkg_tiles(5, 7, 3, 2, null_array);
		set_bkg_tiles(11, 7, 4, 2, null_array);

		// Reset variables
		player_y = 72;
		opponent_y = 72;
		score_player = 0;
		score_opponent = 0;
		ball_vel_y = 0;
		waiting_for_start = 1;
	}
	if (score_opponent == SCORE_MAX) {
		// Display text
		set_bkg_tiles(5, 7, 3, 2, you_array);
		set_bkg_tiles(11, 7, 4, 2, lose_array);

		// Delete all sprites
		curr_sprite = 0;
		flush_sprites();

		// Wait until the player presses start
		while ((joypad() & J_START) == 0) {
			draw_sprite(56, 92, 0x32, 0);
			draw_sprite(64, 92, 0x33, 0);
			draw_sprite(72, 92, 0x34, 0);
			draw_sprite(96, 92, 0x35, 0);
			draw_sprite(104, 92, 0x36, 0);
			draw_sprite(112, 92, 0x37, 0);
			flush_sprites();
			wait_vbl_done();
		}

		while (joypad() & J_START) {
			wait_vbl_done();
		}
		
		// Remove the text
		set_bkg_tiles(5, 7, 3, 2, null_array);
		set_bkg_tiles(11, 7, 4, 2, null_array);

		// Reset variables
		player_y = 72;
		opponent_y = 72;
		score_player = 0;
		score_opponent = 0;
		ball_vel_y = 0;
		waiting_for_start = 1;
	}
}

inline void load_graphics() {
	// We turn off the display before copying data.
	disable_interrupts();
	LCDC_REG = LCDCF_OFF;
	
	// Let's copy the graphics data to VRAM
	set_sprite_data(0, 0x40, graphics);
}

inline void prepare_gameplay() {
	// And set the palette to go from 0=white to 3=black
	LCDC_REG = LCDCF_OFF;
	BGP_REG = 0b11100100;
	OBP0_REG = 0b11100100;

	// Let's prepare the playing field background
	for (int x = 0; x < 20; x++) {
		_SCRN0[x + (0 * DEVICE_SCREEN_BUFFER_WIDTH)] = 0x05;
		_SCRN0[x + (17 * DEVICE_SCREEN_BUFFER_WIDTH)] = 0x05;
	}
	for (int y = 1; y < 17; y++) {
		_SCRN0[9 + (y * DEVICE_SCREEN_BUFFER_WIDTH)] = 0x03;
		_SCRN0[10 + (y * DEVICE_SCREEN_BUFFER_WIDTH)] = 0x04;
	}
	
	// Now it's time to set some LCD flags 
	LCDC_REG = 	LCDCF_ON | 		// We want the screen to be off for now
				LCDCF_BG8000 | 	// We use the shared tile area $8000 for sprites and tiles alike
				LCDCF_BGON; 	// We want to enable the tilemap layer

	// Enable interrupts so we can use the vblank interrupt
	enable_interrupts();

	// Enable sound chip
	NR52_REG = 0x80;
	NR51_REG = 0xFF;
	NR50_REG = 0xFF;

	// We want to use sprites, and this macro enables all the necessary systems to show sprites
    SHOW_SPRITES;
}

void wait_n_frames(int n_frames) {
	while (n_frames--) {
		wait_vbl_done();
	}
}

inline void splash_screen()
{
	// Load the graphics
	load_graphics();

	// Let's clear the screen
	{
		for (int x = 0; x < 20 * 18; x++) {
			_SCRN0[x] = 0x00;
		}
	}

	// Copy the Flan splash screen to the screen
	const uint8_t splash_screen[] = { 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31};
	set_bkg_tiles(8, 8, 4, 3, splash_screen);

	// Now it's time to set some LCD flags 
	LCDC_REG = 	LCDCF_ON | 		// We want the screen to be off for now
				LCDCF_BG8000 | 	// We use the shared tile area $8000 for sprites and tiles alike
				LCDCF_BGON; 	// We want to enable the tilemap layer
				
	enable_interrupts();

	// Fade in
	BGP_REG = 0b00000000;
	wait_n_frames(4);
	BGP_REG = 0b01000000;
	wait_n_frames(4);
	BGP_REG = 0b10010000;
	wait_n_frames(4);
	BGP_REG = 0b11100100;

	// Wait a short while
	wait_n_frames(60);

	// Fade out
	BGP_REG = 0b10010000;
	wait_n_frames(4);
	BGP_REG = 0b01000000;
	wait_n_frames(4);
	BGP_REG = 0b00000000;
	wait_n_frames(4);

	// Turn off the screen
	LCDC_REG = LCDCF_OFF;

	// Let's clear the screen again
	for (int x = 0; x < 20 * 18; x++) {
		_SCRN0[x] = 0x00;
	}
}

void main() {
	splash_screen();
	prepare_gameplay();

	// Make the player press start to continue
	waiting_for_start = 1;
	fade_in_timer = 15;

	// This is our main loop
	while (1) {
		// Update player
		if (wait_timer == 0 && waiting_for_start == 0 && fade_in_timer == 0) {
			update_player();
			update_opponent();
			update_ball();
			handle_win();
		} else {
			--wait_timer;
		}

		// If we're still fading in from the splash screen
		if (fade_in_timer) {
			// Shift in the colour palette 2 bits at a time for a smooth fade in
			const int amount_to_shift = (fade_in_timer >> 2) << 1;
			BGP_REG = 0b11100100 << amount_to_shift;
			OBP0_REG = 0b11100100 << amount_to_shift;
			--fade_in_timer;
		}

		// Handle waiting for start
		if (waiting_for_start) {
			if (joypad() & J_START)
			{
				waiting_for_start = 0;
				wait_timer = 0;
			}
			draw_sprite(56, 84, 0x32, 0);
			draw_sprite(64, 84, 0x33, 0);
			draw_sprite(72, 84, 0x34, 0);
			draw_sprite(96, 84, 0x35, 0);
			draw_sprite(104, 84, 0x36, 0);
			draw_sprite(112, 84, 0x37, 0);
		}

		// Update sprites
		draw_sprite(16, player_y - 4, 1, 0);
		draw_sprite(16, player_y + 4, 1, 0);
		draw_sprite(152, opponent_y - 4, 1, 0);
		draw_sprite(152, opponent_y + 4, 1, 0);
		draw_sprite(0x54 - 32, 64, 6 + score_player, 0);
		draw_sprite(0x54 + 32, 64, 6 + score_opponent, 0);

		// Draw ball
		draw_sprite((uint8_t)(ball_pos_x >> 8), (uint8_t)(ball_pos_y >> 8), 2, 0);

		// Wait for next frame
		flush_sprites();
		wait_vbl_done();
	}
}