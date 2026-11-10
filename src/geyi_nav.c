#include "geyi_nav.h"

void gy_nav_reset(GyNav *nav)
{
    if(!nav) return;
    nav->tilt=0;
    nav->button=0;
}
int gy_nav_tilt_allowed(const GyNav *nav)
{
    return nav && !nav->button;
}
int gy_nav_tilt_selected(GyNav *nav)
{
    if(!nav||nav->button) return 0;
    nav->tilt=1;
    return 1;
}
int gy_nav_button_selected(GyNav *nav)
{
    if(!nav) return 0;
    nav->button=1;
    nav->tilt=0;
    return 1;
}
int gy_nav_tilt_cancel(GyNav *nav)
{
    if(!nav||!nav->tilt) return 0;
    nav->tilt=0;
    nav->button=0;
    return 1;
}
int gy_nav_clear(GyNav *nav)
{
    int had;
    if(!nav) return 0;
    had=nav->tilt||nav->button;
    nav->tilt=0;
    nav->button=0;
    return had;
}
int gy_nav_blocks_screen_edit(const GyNav *nav,int placing)
{
    return nav && nav->tilt && !placing;
}
int gy_nav_tilt_active(const GyNav *nav)
{
    return nav && nav->tilt;
}
int gy_nav_button_active(const GyNav *nav)
{
    return nav && nav->button;
}
