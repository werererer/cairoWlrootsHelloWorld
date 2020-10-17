# cairoWlrootsTest
Uncomment line 85 in test.c. 
You will get an error then the next time you call a wlr_render_... function.

This is the error you will get:
```
gles2_get_renderer_in_context: Assertion `wlr_egl_is_current(renderer->egl)' failed.
```
This is the line to uncomment:
```c
struct wlr_texture *cTexture = wlr_texture_from_pixels(sample->renderer, WL_SHM_FORMAT_ARGB8888, stride, width, height, cData);
```
