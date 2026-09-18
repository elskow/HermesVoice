#include "page.h"

#include "node_display.h"
#include "node_voice.h"

#include <string.h>
#include <stdio.h>

static char s_text[NODE_VOICE_MAX_REPLY];
static int s_page;
static int s_pages;

void page_store(const char *reply)
{
    strncpy(s_text, reply ? reply : "", sizeof(s_text) - 1);
    s_text[sizeof(s_text) - 1] = '\0';
    s_pages = (strlen(s_text) + PAGE_CHARS - 1) / PAGE_CHARS;
    s_page = 0;
    page_show();
}

void page_next(void)
{
    if (s_pages <= 1)
    {
        return;
    }
    s_page = (s_page + 1) % s_pages;
    page_show();
}

void page_show(void)
{
    // Caller guarantees s_pages > 0 and 0 <= s_page < s_pages, so off
    // lands inside the string: pages tile s_text contiguously.
    size_t off = (size_t)s_page * PAGE_CHARS;
    char win[PAGE_CHARS + 1];

    snprintf(win, sizeof(win), "%s", s_text + off);

    if (s_pages > 1)
    {
        // Tag lives INSIDE the visible 57: overwrite the tail so short
        // final pages keep it (a tag past the NUL would never render).
        // s_pages <= 18 (1024/57 ceiling): two hand-capped digits, no
        // format-truncation exposure under -Werror.
        char tag[8];
        tag[0] = '0' + (char)((s_page + 1) / 10 % 10);
        tag[1] = '0' + (char)((s_page + 1) % 10);
        tag[2] = '/';
        tag[3] = '0' + (char)(s_pages / 10 % 10);
        tag[4] = '0' + (char)(s_pages % 10);
        tag[5] = '\0';
        size_t wl = strlen(win);
        size_t tl = strlen(tag);
        if (wl + 1 + tl > PAGE_CHARS)
        {
            wl = PAGE_CHARS - 1 - tl;
        }
        win[wl] = ' ';
        memcpy(win + wl + 1, tag, tl + 1);
    }
    node_display_reply(win);
}

int page_count(void)
{
    return s_pages;
}

void page_clear(void)
{
    s_text[0] = '\0';
    s_page = 0;
    s_pages = 0;
}

void page_mark_single(void)
{
    s_text[0] = '\0';
    s_page = 0;
    s_pages = 1;
}
