#ifndef CRTUBE_PS2_IMAGES_H
#define CRTUBE_PS2_IMAGES_H

/* Loads the four cached YouTube home thumbnails. The request is synchronous
   and is intended to run only after the user starts Network Status tests. */
int crtube_images_load_home_results(void);

/* Draws the cached 2x2 recommendation grid. */
void crtube_images_draw_home_grid(void);

#endif
