#include "ai-sam.h"
#include "config.h"  // PROXY_API_URL and DEFAULT_TOKEN definitions
#include "speech.h"

static char app_token[65] = {0};

// Global buffers
char response_buffer[RESPONSE_BUFFER_SIZE];
char devicespec[256];
char json_payload[REQUEST_BUFFER_SIZE];
char user_input[512];
char escaped_input[768];
char text_display[MAX_TEXT_SIZE] = "";
#ifdef BUILD_ATARI
char text_sam[MAX_TEXT_SIZE] = "";
#endif
bool speak = true;
char message_id[64] = "";
char status[32] = "";

// ---------------------------------------------------------------------------
// FujiNet initialization and session handling
// ---------------------------------------------------------------------------
bool init_fujinet(void)
{
    AdapterConfig config;
    uint16_t count = 0;
    uint8_t buffer[66] = {0};

    if (!fuji_get_adapter_config(&config))
    {
        printf("Error: FujiNet not detected or failed to retrieve configuration!\n");
        return false;
    }

    fuji_set_appkey_details(CREATOR_ID, APP_ID, DEFAULT);

    if (fuji_read_appkey(TOKEN_KEY_ID, &count, buffer))
    {
        if (count > 64) count = 64;
        buffer[count] = '\0';
        strncpy(app_token, (char*)buffer, sizeof(app_token) - 1);
        return true;
    }
    else
    {
        strncpy(app_token, DEFAULT_TOKEN, sizeof(app_token));
        app_token[sizeof(app_token) - 1] = '\0';
        return new_convo();
    }
}

bool new_convo(void)
{
    int err, len;

    printf("Starting new session...");
    snprintf(devicespec, sizeof(devicespec), "N1:%s%s", PROXY_API_URL, SUBMIT_URL);

    snprintf(json_payload, REQUEST_BUFFER_SIZE,
        "{"
        "\"token_id\":\"%s\","
        "\"new\":\"%s\""
        "}",
        app_token, DEFAULT_TOKEN);

    err = network_open(devicespec, OPEN_MODE_HTTP_POST, OPEN_TRANS_NONE);
    if (err != 0)
    {
        printf("\nErr: Unable to open network channel.\n");
        return false;
    }

    network_http_start_add_headers(devicespec);
    network_http_add_header(devicespec, "Content-Type: application/json");
    network_http_end_add_headers(devicespec);

    err = network_http_post(devicespec, json_payload);
    if (err != 0)
    {
        printf("\nErr: Failed to send data.\n");
        network_close(devicespec);
        return false;
    }

    err = network_json_parse(devicespec);
    if (err != 0)
    {
        printf("\nErr: Failed to parse JSON.\n");
        network_close(devicespec);
        return false;
    }

    err = network_json_query(devicespec, "/token_id", response_buffer);
    if (err > 0)
    {
        strncpy(app_token, response_buffer, sizeof(app_token) - 1);
    }
    else
    {
        printf("\nError: Token not received.\n");
        network_close(devicespec);
        return false;
    }

    network_close(devicespec);

    // printf("TokResp (%d): %s\n", sizeof(response_buffer), response_buffer); // Print response for debug
    len = strlen(response_buffer);
    if (len > 64) {
        len = 64;
    }

    // Copy token from response_buffer into app_token
    strncpy(app_token, response_buffer, len);
    app_token[len+1] = '\0';
    // Write it to AppKey and update in-memory
    if (!fuji_write_appkey(TOKEN_KEY_ID, (uint16_t)len, (uint8_t*)app_token))
    {
        printf("\nErr: Failed to write token to AppKey.\n");
        return false;
    }
    else
    {
        //printf("Token saved: \n %s\n", app_token);
        printf("done!\n");
        return true;
    }
}

// ---------------------------------------------------------------------------
// Send request asynchronously to submit_request.php
// ---------------------------------------------------------------------------
bool send_openai_request(char *user_input)
{
    int err;
    int elapsed = 0;
    bool retried = false;
    char error_msg[64] = "";

retry_submit:
    escape_json_string(user_input, escaped_input, sizeof(escaped_input));

    // Step 1: POST user input to submit_request.php
    snprintf(devicespec, sizeof(devicespec), "N1:%s%s", PROXY_API_URL, SUBMIT_URL);

    snprintf(json_payload, REQUEST_BUFFER_SIZE,
        "{"
        "\"token_id\":\"%s\","
        "\"message\":\"%s\""
        "}",
        app_token, escaped_input);

    err = network_open(devicespec, OPEN_MODE_HTTP_POST, OPEN_TRANS_NONE);
    if (err != 0)
    {
        printf("Error: Unable to open network channel.\n");
        return false;
    }

    network_http_start_add_headers(devicespec);
    network_http_add_header(devicespec, "Content-Type: application/json");
    network_http_end_add_headers(devicespec);

    err = network_http_post(devicespec, json_payload);
    if (err != 0)
    {
        printf("Error: Failed to send data.\n");
        network_close(devicespec);
        return false;
    }

    err = network_json_parse(devicespec);
    if (err != 0)
    {
        printf("Error: Failed to parse JSON response.\n");
        network_close(devicespec);
        return false;
    }

    error_msg[0] = '\0';
    err = network_json_query(devicespec, "/error", error_msg);
    if (err > 0 && strcmp(error_msg, "Invalid token") == 0)
    {
        network_close(devicespec);
        printf("Token expired. Requesting new token...\n");
        if (!new_convo())
        {
            printf("Error: Failed to renew token.\n");
            return false;
        }
        if (!retried)
        {
            retried = true;
            goto retry_submit;
        }
        printf("Error: Token renewed but request failed.\n");
        return false;
    }

    // Extract message_id and status
    network_json_query(devicespec, "/message_id", message_id);
    network_json_query(devicespec, "/status", status);
    network_close(devicespec);

    if (strlen(message_id) == 0)
    {
        printf("Error: Invalid response from server.\n");
        return false;
    }

    printf("Thinking...");

    // Step 2: Poll check_request.php until complete or timeout
    for (elapsed = 0; elapsed < CHECK_TIMEOUT; elapsed += CHECK_INTERVAL)
    {
        snprintf(devicespec, sizeof(devicespec),
                 "N1:%s%s?token_id=%s&message_id=%s",
                 PROXY_API_URL, CHECK_URL, app_token, message_id);

        err = network_open(devicespec, OPEN_MODE_HTTP_GET, OPEN_TRANS_NONE);
        if (err != 0)
        {
            printf("Error: Network open failed.\n");
            sleep(CHECK_INTERVAL);
            continue;
        }

        err = network_json_parse(devicespec);
        if (err != 0)
        {
            printf("Error: JSON parse failed.\n");
            network_close(devicespec);
            sleep(CHECK_INTERVAL);
            continue;
        }

        error_msg[0] = '\0';
        err = network_json_query(devicespec, "/error", error_msg);
        if (err > 0 && strcmp(error_msg, "Token expired") == 0)
        {
            printf("\nToken expired. Requesting new token...\n");
            network_close(devicespec);
            new_convo();
            return false;
        }

        network_json_query(devicespec, "/status", status);

        if (strcmp(status, "complete") == 0)
        {
            // Retrieve the completed JSON response
            network_json_query(devicespec, "/text_display", response_buffer);
            strncpy(text_display, response_buffer, sizeof(text_display) - 1);
#ifdef BUILD_ATARI
            network_json_query(devicespec, "/text_sam", response_buffer);
            strncpy(text_sam, response_buffer, sizeof(text_sam) - 1);
#endif
            network_close(devicespec);

#ifdef BUILD_ATARI
            process_response(text_display, text_sam);
#else
            process_response(text_display, NULL);
#endif

            return true;
        }

        network_close(devicespec);
        printf(".");
        fflush(stdout);
        sleep(CHECK_INTERVAL);
    }

    // Timeout after 90 seconds
    printf("\nError: Request timed out after %d seconds.\n", CHECK_TIMEOUT);
    return false;
}

// ---------------------------------------------------------------------------
// Process JSON and forward to display/speech routines
// ---------------------------------------------------------------------------
void process_response(const char *text_display, const char *text_sam)
{
    if (strlen(text_display) > 0)
        display_text(text_display);
    else
        printf("Error: No text to display\n");

#ifdef BUILD_ATARI
    if (speak && strlen(text_sam) > 0)
        speak_text(text_sam);
#endif
}

// Convert UTF-8 characters to ASCII equivalents and replace them in text
void process_text(char *text)
{
    char *src = text, *dst = text;
    unsigned int unicode_char;

    while (*src) {
        if ((unsigned char)*src == '\\' && *(src + 1) == 'n')
        {
#ifdef BUILD_MSDOS
            *dst++ = CR; // Add carriage return before newline for MS-DOS
#endif

            *dst++ = NEWLINE; // Convert "\n" to ATASCII newline
            src += 2;
        }
        else if ((unsigned char)*src >= 0xC0 && (unsigned char)*src <= 0xDF && *(src + 1))
        {
            unicode_char = (((unsigned char)*(src) & 0x1F) << 6) | ((unsigned char)*(src + 1) & 0x3F);

            // Hires font on CoCo can display Latin-1 (ISO-8859-1) 
            // characters, so use those.
#ifdef _CMOC_VERSION_
            // ISO-8859-1 covers Unicode code points 0x00-0xFF directly
            if (unicode_char >= 0xA0 && unicode_char <= 0xFF)
            {
                // Characters in the Latin-1 Supplement range (0xA0-0xFF)
                // can be directly mapped to ISO-8859-1
                *dst++ = (char)unicode_char;
            }
            else
            {
                // Characters outside ISO-8859-1 range
                switch (unicode_char)
                {
                // Polish characters not in Latin-1
                case 0x0105:
                    *dst++ = 'a';
                    break; // ą
                case 0x0107:
                    *dst++ = 'c';
                    break; // ć
                case 0x0119:
                    *dst++ = 'e';
                    break; // ę
                case 0x0142:
                    *dst++ = 'l';
                    break; // ł
                case 0x0144:
                    *dst++ = 'n';
                    break; // ń
                case 0x015B:
                    *dst++ = 's';
                    break; // ś
                case 0x017A:
                    *dst++ = 'z';
                    break; // ź
                case 0x017C:
                    *dst++ = 'z';
                    break; // ż
                default:
                    *dst++ = '_';
                    break; // Replace unsupported characters
                }
            }
#else            
            switch (unicode_char) {
                // Polish characters
                case 0x0105: *dst++ = 'a'; break; // ą
                case 0x0107: *dst++ = 'c'; break; //  ć
                case 0x0119: *dst++ = 'e'; break; //  ę
                case 0x0142: *dst++ = 'l'; break; //  ł
                case 0x0144: *dst++ = 'n'; break; //  ń
                case 0x00F3: *dst++ = 'o'; break; //  ó
                case 0x015B: *dst++ = 's'; break; //  ś
                case 0x017A: *dst++ = 'z'; break; //  ź
                case 0x017C: *dst++ = 'z'; break; //  ż
        
                // German characters
                case 0x00E4: *dst++ = 'a'; break; // ä
                case 0x00F6: *dst++ = 'o'; break; // ö
                case 0x00FC: *dst++ = 'u'; break; // ü
                case 0x00DF: *dst++ = 's'; break; // ß
        
                // French characters 
                case 0x00E0: *dst++ = 'a'; break; // à
                case 0x00E2: *dst++ = 'a'; break; // â
                case 0x00E7: *dst++ = 'c'; break; // ç
                case 0x00E9: *dst++ = 'e'; break; // é
                case 0x00E8: *dst++ = 'e'; break; // è
                case 0x00EA: *dst++ = 'e'; break; // ê
                case 0x00EB: *dst++ = 'e'; break; // ë
                case 0x00EE: *dst++ = 'i'; break; // î
                case 0x00EF: *dst++ = 'i'; break; // ï
                case 0x00F4: *dst++ = 'o'; break; // ô
                case 0x00F9: *dst++ = 'u'; break; // ù
                case 0x00FB: *dst++ = 'u'; break; // û
        
                // Spanish characters
                case 0x00E1: *dst++ = 'a'; break; // á
                case 0x00ED: *dst++ = 'i'; break; // í
                case 0x00F1: *dst++ = 'n'; break; // ñ
                case 0x00FA: *dst++ = 'u'; break; // ú
        
                // Italian characters
                case 0x00EC: *dst++ = 'i'; break; // ì
                case 0x00F2: *dst++ = 'o'; break; // ò
        
                default: *dst++ = '_'; break; // Replace unsupported characters
            }
#endif            
            src += 2; // Skip UTF-8 second byte
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0'; // Null terminate
}

// Display text on Atari screen with word wrap
void display_text(char *text)
{
    int line_length = 0, word_length = 0, i = 0, w = 0, lines = 0;
    char *word_start;
    char *pos;

#ifdef BUILD_MSDOS
    putchar(CR);
#endif

    putchar(NEWLINE);

    process_text(text); // Convert UTF-8 and prepare text
    pos = text;

    while (*text)
    {
        word_start = text;
        while (*text && !isspace(*text))
        {
            text++; // Move to end of word
        }
        word_length = text - word_start;

        if (line_length + word_length >= SCREEN_WIDTH - 1)
        {
#ifdef BUILD_MSDOS
            putchar(CR);
#endif

            putchar(NEWLINE); // Move to next line if word doesn't fit
            line_length = 0;
            lines++;
        }

        // Print each character from the processed text
        for (i = 0; i < word_length; i++)
        {
            putchar(word_start[i]);
        }
        line_length += word_length;

        while (*text && isspace(*text))
        {
            if (*text == '\n')
            {
#ifdef BUILD_MSDOS
                putchar(CR);
#endif
            }
            putchar(*text == '\n' ? NEWLINE : *text); // Convert '\n' to ATASCII newline
            if (*text == '\n')
            {
                line_length = 0;
                lines++;
            }
            else
            {
                line_length++;
            }
            text++;
        }

        // Pause for reading if screen full
        if (lines >= SCREEN_HEIGHT)
        {
            printf("\nPress RETURN to continue...\n");
            getchar();  // Wait for key press before continuing
            lines = 0;
        }
    }
#ifdef BUILD_MSDOS
    putchar(CR);
#endif

    putchar(NEWLINE);
}

// Escape special characters in user input for JSON compatibility
void escape_json_string(const char *input, char *output, int output_size)
{
    int i = 0, j = 0;
    while (input[i] != '\0') {
        char c = input[i];

        // Check if we need to escape this char
        if (c == '"' || c == '\\' || c == '/') {
            // We need two bytes: '\' plus the character itself
            if (j + 2 >= output_size) break;  // not enough room
            output[j++] = '\\';
            output[j++] = c;
        }
        else {
            // Just a normal character
            if (j + 1 >= output_size) break;
            output[j++] = c;
        }

        i++;
    }

    // NULL‐terminate
    if (j < output_size) {
        output[j] = '\0';
    } else {
        output[output_size - 1] = '\0';
    }
}

void get_user_input(char *buffer, int max_length)
{
    int index = 0;
    int x, y;
    char ch;

#ifdef _CMOC_VERSION_
    get_line(buffer, max_length);
#else
    while (1)
    {
        ch = cgetc(); // Read keypress
        //printf("%02X", ch);
        if (ch == '\r' || ch == '\n') // Enter key
        {
            buffer[index] = '\0'; // Null-terminate string
            printf("\n"); // Move to new line
            break;
        }
        else if ((ch == 0x08 || ch == 0x7F || ch == 0x7E) && index > 0) // Handle Backspace
        {
            index--;
            buffer[index] = '\0';       // Remove last char from buffer

            // Remove the last char on screen and move position
            x = wherex();
            y = wherey();
            // If we’re not in the first column, just move left one
            if (x > 1)
            {
                gotoxy(x - 1, y);
            }
            else // Otherwise we’re in column 1
            {
                // If not on the very first line, wrap to end of previous line
                if (y > 1) {
                    gotoxy(SCREEN_WIDTH, y - 1);
                }
                // Otherwise we're at 1,1 and there’s nothing to delete
            }            
            cputc(' '); // Overwrite that character with a space
            // if we moved left, x-1; if we wrapped, SCREEN_WIDTH, y-1
            if (x > 1)
            {
                gotoxy(x - 1, y);
            }
            else
            {
                gotoxy(SCREEN_WIDTH, y - 1);
            }
        }
        else if (index < max_length - 1 && ch >= 32 && ch <= 126 && ch != '~') // Normal character
        {
            buffer[index++] = ch;
            putchar(ch); // Echo character
        }
    }
#endif
}

static void print_centered(const char *s)
{
    int len = (int)strlen(s);
    int pad = (SCREEN_WIDTH - len) / 2;
    int i;
    if (pad < 0) pad = 0;
    for (i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", s);
}

static void print_wrapped(const char *text)
{
    int w = SCREEN_WIDTH;
    int line_len = 0;
    const char *p = text;

    while (*p)
    {
        const char *wstart = p;
        int wl;
        while (*p && !isspace((unsigned char)*p)) p++;
        wl = (int)(p - wstart);
        if (line_len > 0 && line_len + 1 + wl > w)
        {
            putchar('\n');
            line_len = 0;
        }
        if (line_len > 0)
        {
            putchar(' ');
            line_len++;
        }
        while (wstart < p)
        {
            putchar(*wstart++);
            line_len++;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n')
        {
            putchar('\n');
            line_len = 0;
            p++;
        }
    }
    if (line_len > 0) putchar('\n');
}

void print_help()
{
    const char *title = "FujiNet AI SAM v3 HELP";
    int w = SCREEN_WIDTH;
    int tlen = (int)strlen(title);
    int dashes = w - tlen - 2;
    int left, right, i;

    if (dashes < 0) dashes = 0;
    left = dashes / 2;
    right = dashes - left;
    for (i = 0; i < left; i++) putchar('-');
    printf(" %s ", title);
    for (i = 0; i < right; i++) putchar('-');
    putchar('\n');

    print_wrapped(
        "AI SAM is an interface with OpenAI ChatGPT. "
        "You can talk with it about anything you like. "
        "It has a modest context window to remember some "
        "of your conversation. Messages are stored on a "
        "server by token id for up to 7 days after which "
        "they are deleted. You can delete your chat "
        "history at any time by using the 'NEW' command "
        "which tells the server to delete all messages "
        "for your token id and provides a new token."
    );
    printf("\n");
    printf(" HELP       Prints this message\n");
    printf(" EXIT       Exit the program\n");
#ifdef BUILD_ATARI
    printf(" SPEAKOFF   Turn OFF SAM audio\n");
    printf(" SPEAKON    Turn ON SAM audio\n");
#endif
    printf(" CLS        Clear the screen\n");
    printf(" NEW        Start new conversation\n");
}

int main()
{
    bool res = false;

#ifdef _CMOC_VERSION_
    hirestxt_init();
#endif
#ifdef BUILD_MSDOS
    setbuf(stdout, NULL);
    msdos_init_screen();
#endif

    print_centered("Welcome to AI SAM!");
    speak_text("I AEM SAEM, YOR FOO-JEE-NET UH-SIS-TUHNT");

    if (!init_fujinet())
    {
        printf("Press any key to quit\n");
        getchar();
        return 1;  // Exit if FujiNet is not available
    }

    print_centered("Type HELP for a list of commands");
    print_centered("Ask me anything...");

    while (1)
    {
        user_input[0] = '\0';
        text_display[0] = '\0';
#ifdef BUILD_ATARI        
        text_sam[0] = '\0';
#endif
        json_payload[0] = '\0';
        response_buffer[0] = '\0';

        printf("\n> ");
        get_user_input(user_input, sizeof(user_input)-1);
    
        if (!stricmp(user_input, "HELP"))
        {
            print_help();
        }
        else if (!stricmp(user_input, "EXIT"))
        {
            printf("Goodbye!\n");
            break;
        }
#ifdef BUILD_ATARI
        else if (strcmp(user_input, "speakon") == 0 || strcmp(user_input, "SPEAKON") == 0)
        {
            speak = true;
            printf("Turned ON SAM audio output\n");
        }
        else if (strcmp(user_input, "speakoff") == 0 || strcmp(user_input, "SPEAKOFF") == 0)
        {
            speak = false;
            printf("Turned OFF SAM audio output\n");
        }
#endif
        else if (!stricmp(user_input, "CLS"))
        {
            clrscr();
        }
        else if (!stricmp(user_input, "NEW"))
        {
            new_convo();
        }
        else
        {
            res = send_openai_request(user_input);
        }
    }

#ifdef _CMOC_VERSIION_
    hiresstxt_close();
#endif  

    return 0;
}
