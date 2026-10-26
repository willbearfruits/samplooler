/* The vendored C libraries' implementations, built without the app's warnings: NanoVG's GL3 back end (its GL
   entry points come from glad) and stb_image_write for --shot's PNG. */
#include <glad/gl.h>

#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
