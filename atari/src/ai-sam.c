#include "ai-sam.h"
#include "config.h"  // PROXY_API_URL and DEFAULT_TOKEN definitions

static char app_token[65] = {0};

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

    // Extract message_id and status
    network_json_query(devicespec, "/message_id", message_id);
    network_json_query(devicespec, "/status", status);
    network_close(devicespec);

    if (strlen(message_id) == 0)
    {
        printf("Error: Invalid response from server.\n");
        return false;
    }

    printf("Thinking", message_id);

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

        network_json_query(devicespec, "/status", status);

        if (strcmp(status, "complete") == 0)
        {
            // Retrieve the completed JSON response
            network_json_query(devicespec, "/text_display", response_buffer);
            strncpy(text_display, response_buffer, sizeof(text_display) - 1);

            network_json_query(devicespec, "/text_sam", response_buffer);
            strncpy(text_sam, response_buffer, sizeof(text_sam) - 1);

            network_close(devicespec);

            // Compose minimal JSON string for process_response()
            snprintf(response_buffer, sizeof(response_buffer),
                     "{\"text_display\":\"%s\",\"text_sam\":\"%s\"}",
                     text_display, text_sam);
            printf("\n"); // new line
            process_response(response_buffer);
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
void process_response(const char *json_response)
{
    char *start, *end;

    // Extract text_display
    start = strstr(json_response, "\"text_display\":\"");
    if (start)
    {
        start += strlen("\"text_display\":\"");
        end = strchr(start, '\"');
        if (end && (end - start) < sizeof(text_display))
        {
            strncpy(text_display, start, end - start);
            text_display[end - start] = '\0';
        }
    }

    // Extract text_sam
    start = strstr(json_response, "\"text_sam\":\"");
    if (start)
    {
        start += strlen("\"text_sam\":\"");
        end = strchr(start, '\"');
        if (end && (end - start) < sizeof(text_sam))
        {
            strncpy(text_sam, start, end - start);
            text_sam[end - start] = '\0';
        }
    }

    if (strlen(text_display) > 0)
        display_text(text_display);
    else
        printf("Error: No text to display\n");

    if (speak && strlen(text_sam) > 0)
        speak_text(text_sam);
}

// Convert UTF-8 characters to ASCII equivalents and replace them in text
void process_text(char *text)
{
    char *src = text, *dst = text;
    unsigned int unicode_char;

    while (*src) {
        if ((unsigned char)*src == '\\' && *(src + 1) == 'n')
        {
            *dst++ = 0x9B; // Convert "\n" to ATASCII newline
            src += 2;
        }
        else if ((unsigned char)*src >= 0xC0 && (unsigned char)*src <= 0xDF && *(src + 1))
        {
            unicode_char = (((unsigned char)*(src) & 0x1F) << 6) | ((unsigned char)*(src + 1) & 0x3F);
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
            putchar(0x9B); // Move to next line if word doesn't fit
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
            putchar(*text == '\n' ? 0x9B : *text); // Convert '\n' to ATASCII newline
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
    putchar(0x9B);
}

// Speak text using FujiNet SAM
void speak_text(const char *sam_text)
{
    int len;
    int start;
    int max_end;
    int end;
    int chunk_len;
    int i;
    char chunk[SAM_CHUNK_SIZE + 1];
    char c;
    FILE *printer;

    len   = strlen(sam_text);
    start = 0;

    printer = fopen("P4:", "w");
    if (!printer) {
        printf("Unable to access FujiNet SAM printer device (P4)\nTurning off speech.\n");
        speak = false;
        return;
    }

    while (start < len) {
        /* determine the farthest we can go in this chunk */
        max_end = start + SAM_CHUNK_SIZE;
        if (max_end > len) {
            max_end = len;
        }
        end = max_end;

        /* Try to break on a sentence boundary */
        for (i = max_end - 1; i > start; --i) {
            c = sam_text[i];
            if (c == '.' || c == '?' || c == '!') {
                end = i + 1;
                break;
            }
        }

        /* If no sentence end found, break on last whitespace */
        if (end == max_end) {
            for (i = max_end; i > start; --i) {
                if (isspace((unsigned char)sam_text[i])) {
                    end = i;
                    break;
                }
            }
            /* If still no break point, force at max_end */
            if (end == start) {
                end = max_end;
            }
        }

        /* Copy and NULL-terminate */
        chunk_len = end - start;
        if (chunk_len > SAM_CHUNK_SIZE) {
            chunk_len = SAM_CHUNK_SIZE;
        }
        memcpy(chunk, sam_text + start, chunk_len);
        chunk[chunk_len] = '\0';

        /* Send chunk to SAM */
        fprintf(printer, "%s\n", chunk);

        /* move past any whitespace for next chunk */
        start = end;
        while (start < len && isspace((unsigned char)sam_text[start])) {
            start++;
        }
    }

    fclose(printer);
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
}

void print_help()
{
    //      -------------------40-------------------
    printf("-------- FujiNet AI SAM v3 HELP --------\n");
    printf("AI SAM is an interface with OpenAI\n");
    printf("ChatGPT. You can talk with it about\n");
    printf("anything you like. It has a modest\n");
    printf("context window to remember some of your\n");
    printf("conversation. Messages are stored on a\n");
    printf("server by token id for up to 7 days\n");
    printf("after which they are deleted. You can\n");
    printf("delete your chat history at any time by\n");
    printf("using the 'NEW' command which tells the\n");
    printf("server to delete all messages for your\n");
    printf("token id and provides a new token.\n");
    printf("\n");
    printf("\n");
    printf("\n");
    printf(" HELP       Prints this message\n");
    printf(" EXIT       Exit the program\n");
    printf(" SPEAKOFF   Turn OFF SAM audio\n");
    printf(" SPEAKON    Turn ON SAM audio\n");
    printf(" CLS        Clear the screen\n");
    printf(" NEW        Start new conversation\n");
}

int main()
{
    bool res = false;

    printf("         Welcome to AI SAM!\n");
    speak_text("I AEM SAEM, YOR FOO-JEE-NET UH-SIS-TUHNT");

    if (!init_fujinet())
    {
        printf("Press any key to quit\n");
        getchar();
        return 1;  // Exit if FujiNet is not available
    }

    printf("   Type HELP for a list of commands\n");
    printf("           Ask me anything...\n");

    while (1)
    {
        user_input[0] = '\0';
        text_display[0] = '\0';
        text_sam[0] = '\0';
        json_payload[0] = '\0';
        response_buffer[0] = '\0';

        printf("\n> ");
        get_user_input(user_input, sizeof(user_input)-1);
    
        if (strcmp(user_input, "help") == 0 || strcmp(user_input, "HELP") == 0)
        {
            print_help();
        }
        else if (strcmp(user_input, "exit") == 0 || strcmp(user_input, "EXIT") == 0)
        {
            printf("Goodbye!\n");
            break;
        }
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
        else if(strcmp(user_input, "cls") == 0 || strcmp(user_input, "CLS") == 0)
        {
            clrscr();
        }
        else if(strcmp(user_input, "new") == 0 || strcmp(user_input, "NEW") == 0)
        {
            new_convo();
        }
        else
        {
            res = send_openai_request(user_input);
        }
    }

    return 0;
}