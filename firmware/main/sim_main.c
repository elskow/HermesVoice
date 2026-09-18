#include "app_main.h"

#include "node_audio.h"
#include "node_ptt.h"

#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Linux-sim harness: replays a press/clip/release script through the real
// flow, then exits. Own file so app_main carries no sim scaffolding.
static void sim_replay(void)
{
    extern void node_ptt_test_inject(bool raw);
    const char *path = getenv("SIM_SCRIPT");
    if (!path || !*path)
    {
        return;
    }
    FILE *f = fopen(path, "r");
    if (!f)
    {
        printf("[sim] no script at %s\n", path);
        return;
    }
    printf("[sim] replaying %s\n", path);
    char line[64];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == 'p')
        {
            node_audio_start();
            for (int i = 0; i < 6; i++)
            {
                node_ptt_test_inject(true);
            }
        }
        else if (line[0] == 'c')
        {
            {
                extern void node_audio_test_push(const int16_t *sm, size_t n);
                static int16_t clip[16000];
                for (int i = 0; i < 16000; i++)
                {
                    clip[i] = (int16_t)(((i % 32) < 16) ? 4000 : -4000);
                }
                node_audio_test_push(clip, 16000);
            }
        }
        else if (line[0] == 'r')
        {
            for (int i = 0; i < 6; i++)
            {
                node_ptt_test_inject(false);
            }
        }
        else if (line[0] == 's')
        {
            int ms = atoi(line + 1);
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
        else if (line[0] == 'q')
        {
            break;
        }
    }
    fclose(f);
}

void app_main(void)
{
    app_main_run();
    sim_replay();
    if (getenv("SIM_SCRIPT"))
    {
        const char *linger = getenv("SIM_LINGER_MS");
        vTaskDelay(pdMS_TO_TICKS(linger ? atoi(linger) : 3000));
        fflush(stdout);
        fflush(stderr);
        exit(0);
    }
}
