#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define SAMPLE_RATE 8000
#define PI 3.1415926535

// Each character has a "Base Frequency" in our model
float get_byte_freq(char c) {
    // Basic mapping: 'a' is lower, 'z' is higher
    // This simulates the vocal cord tension per alphabet
    return 200.0f + (c * 5.0f); 
}

void synthesize_speech(const char *text) {
    FILE *audio_out = fopen("output.raw", "wb");
    if (!audio_out) return;

    for (int i = 0; text[i] != '\0'; i++) {
        float freq = get_byte_freq(text[i]);
        int duration = (text[i] == ' ') ? 400 : 800; // Space is shorter silence

        for (int j = 0; j < duration; j++) {
            // Generate a Sine Wave: $A \cdot \sin(2\pi \cdot f \cdot t)$
            int16_t sample = (int16_t)(32760 * sin(2.0 * PI * freq * j / SAMPLE_RATE));
            
            // Add bit-level "texture" (Destructuring jitter)
            if (j % 10 == 0) sample ^= (text[i] << 4); 

            fwrite(&sample, sizeof(int16_t), 1, audio_out);
        }
    }

    fclose(audio_out);
    printf("[AUDIO] Bit-stream synthesized to output.raw\n");
}
