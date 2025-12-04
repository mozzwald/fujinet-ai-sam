#ifndef AI_SAM_H
#define AI_SAM_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <conio.h>
#include <unistd.h>  // for sleep()

// FujiNet library includes
#include "fujinet-network.h"
#include "fujinet-fuji.h"
#include "fujinet-clock.h"

// Buffer Sizes
#define RESPONSE_BUFFER_SIZE 3072
#define REQUEST_BUFFER_SIZE 2048
#define SAM_CHUNK_SIZE 100
#define MAX_TEXT_SIZE 960

#define SCREEN_WIDTH 40
#define SCREEN_HEIGHT 20

// App Key Details
#define CREATOR_ID 0x3022
#define APP_ID 0x01
#define TOKEN_KEY_ID 0x01

// Async polling
#define CHECK_INTERVAL 6      // seconds between polls
#define CHECK_TIMEOUT 90      // total timeout in seconds

// Endpoint URLs (relative to PROXY_API_URL in config.h)
#define SUBMIT_URL "submit_request.php"
#define CHECK_URL  "check_request.php"

// Global buffers
char response_buffer[RESPONSE_BUFFER_SIZE];
char devicespec[256];
char json_payload[REQUEST_BUFFER_SIZE];
char user_input[1024];
char escaped_input[1200];
char text_display[MAX_TEXT_SIZE] = "";
char text_sam[MAX_TEXT_SIZE] = "";
bool speak = true;
char message_id[64] = "";
char status[32] = "";

// Function prototypes
bool init_fujinet(void);
bool send_openai_request(char *user_input);
void process_response(const char *json_response);
void display_text(char *text);
void speak_text(const char *sam_text);
void escape_json_string(const char *input, char *output, int output_size);
void get_user_input(char *buffer, int max_length);
void print_help(void);
void process_text(char *text);
bool new_convo(void);

#endif // AI_SAM_H
