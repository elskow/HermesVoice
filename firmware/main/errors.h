#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

    // Relay X-Error to OLED user words. Single-page screens; the pager is
    // cleared first so taps leave the words painted instead of paging a dead
    // turn. "cancelled" paints nothing (back to ready, silent).
    void errors_show(const char *xerr);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
