#include <ai-sam.h>
#include <speech.h>

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