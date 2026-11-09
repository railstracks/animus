// ============================================================================
// StbImageImpl — sole compilation unit for the stb image libraries.
//
// ImageResize.h used to compile all three stb implementations inline,
// which only worked while it had a single includer (ImageTool.cpp). It
// now carries declarations only, and everything compiles here:
//
//   stb_image (read):     compiled here UNLESS the llama.cpp mtmd library
//                         is in the build — its mtmd-helper.cpp already
//                         compiles the same upstream v2.30, and a second
//                         copy would collide on every stbi_* symbol.
//   stb_image_write:      always compiled here (mtmd does not ship it).
//   stb_image_resize2:    always compiled here (mtmd does not ship it).
// ============================================================================

#if defined(__has_include)
#  if __has_include(<mtmd.h>)
#    define ANIMUS_STB_READ_FROM_MTMD 1
#  endif
#endif

#ifndef ANIMUS_STB_READ_FROM_MTMD
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_ONLY_TGA
#define STBI_ONLY_WEBP
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "stb/stb_image_resize2.h"
