#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

    // Reply pager: owns the last completed reply text and the tap-to-page
    // cursor. One writer of the text (report path), one writer of the cursor
    // (tap path). Screen reads only through page_show / page_count.
#define PAGE_CHARS 57

    // Store a completed reply; resets cursor to page 0.
    void page_store(const char *reply);

    // Advance to next page, wrapping to 0. No-op unless pages > 1.
    void page_next(void);

    // Render current page to the display (with n/m tag when pages > 1).
    void page_show(void);

    // Number of pages (0 = no reply stored yet).
    int page_count(void);

    // Drop stored reply (errors paint their own single-page words).
    void page_clear(void);

    // Mark one painted screen with no stored text (error words): taps preserve.
    void page_mark_single(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
