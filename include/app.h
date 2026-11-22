#ifndef CRTUBE_PS2_APP_H
#define CRTUBE_PS2_APP_H

void crtube_app_run(void);
unsigned short crtube_controller_buttons(void);
unsigned short crtube_controller_take_pressed(void);
int crtube_controller_take_exit_request(void);
void crtube_app_return_to_osdsys(void);

#endif
