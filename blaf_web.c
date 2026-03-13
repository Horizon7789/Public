#include <stdio.h>
#include <stdlib.h>
#include "blaf_core.h"

// Simple check for Termux/Android
/*
int check_internet_connection(void) {
    // Tries to ping Google's DNS briefly
    return system("ping -c 1 -W 1 8.8.8.8 > /dev/null 2>&1") == 0;
}


int tokenize_with_online_teacher(const char *sentence, Token *tokens) {
    char cmd[1024];
    // This calls the python script we discussed earlier
    snprintf(cmd, sizeof(cmd), "python3 blaf_tagger.py \"%s\"", sentence);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char res[1024];
    int count = 0;
    if (fgets(res, sizeof(res), fp)) {
        char *word_pair = strtok(res, " ");
        while (word_pair && count < 64) {
            char *sep = strchr(word_pair, '|');
            if (sep) {
                *sep = '\0';
                strncpy(tokens[count].word, word_pair, 63);
                tokens[count].class = map_penn_to_blaf(sep + 1);
                tokens[count].trust = 7; // TRUST_VERIFIED
                count++;
            }
            word_pair = strtok(NULL, " ");
        }
    }
    pclose(fp);
    return count;
}

*/
