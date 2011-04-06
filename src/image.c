
#include <libmediascan.h>
#include <stdlib.h>

#ifdef WIN32
#include "mediascan_win32.h"
#endif

#include "common.h"
#include "buffer.h"
#include "image.h"
#include "error.h"
#include "image_jpeg.h"

MediaScanImage *
image_create(void)
{
  MediaScanImage *i = (MediaScanImage *)calloc(sizeof(MediaScanImage), 1);
  if (i == NULL) {
    ms_errno = MSENO_MEMERROR;
    FATAL("Out of memory for new MediaScanImage object\n");
    return NULL;
  }
  
  LOG_MEM("new MediaScanImage @ %p\n", i);
  
  i->orientation = ORIENTATION_NORMAL;
  
  return i;
}

void
image_destroy(MediaScanImage *i)
{
  int x;
  
  // Note: i->path is always a pointer to an existing path from Result, etc
  // and is not freed here.
  
  // free uncompressed data if any
  image_unload(i);
  
  // free compressed data if any
  if (i->_dbuf) {
    buffer_free(i->_dbuf);
    free(i->_dbuf);
  }
  
  // free thumbnails
  for (x = 0; x < i->nthumbnails; x++)
    image_destroy(i->thumbnails[x]);
  
  LOG_MEM("destroy MediaScanImage @ %p\n", i);
  free(i);
}

int
image_read_header(MediaScanImage *i, MediaScanResult *r)
{
  unsigned char *bptr;
  int ret = 1;
  
  bptr = buffer_ptr((Buffer *)r->_buf);
  
  // Determine image type and basic details
  switch (bptr[0]) {
    case 0xff:
      if (bptr[1] == 0xd8 && bptr[2] == 0xff) {
        i->codec = "JPEG";
        if ( !image_jpeg_read_header(i, r) ) {
          ret = 0;
          goto out;
        }
      }
      break;
    case 0x89:
      if (bptr[1] == 'P' && bptr[2] == 'N' && bptr[3] == 'G'
        && bptr[4] == 0x0d && bptr[5] == 0x0a && bptr[6] == 0x1a && bptr[7] == 0x0a) {
          i->codec = "PNG";
      }
      break;
    case 'G':
      if (bptr[1] == 'I' && bptr[2] == 'F' && bptr[3] == '8'
        && (bptr[4] == '7' || bptr[4] == '9') && bptr[5] == 'a') {
          i->codec = "GIF";
      }
      break;
    case 'B':
      if (bptr[1] == 'M') {
        i->codec = "BMP";
      }
      break;
  }
  
  if ( !i->codec ) {
    ret = 0;
    goto out;
  }
  
out:
  return ret;
}

void
image_add_thumbnail(MediaScanImage *i, MediaScanImage *thumb)
{
  if (i->nthumbnails < MAX_THUMBS)
    i->thumbnails[ i->nthumbnails++ ] = thumb;
}

int
image_load(MediaScanImage *i, MediaScanThumbSpec *spec_hint)
{
  int ret = 1;
  
  if (i->_pixbuf_size)
    return 1;
  
  // Each type-specific loader is expected to call image_alloc_pixbuf to
  // allocate the necessary space for the decompressed image
  
  if (!strcmp("JPEG", i->codec)) {
    if ( !image_jpeg_load(i, spec_hint) ) {
      ret = 0;
      goto out;
    }
  }
  else if (!strcmp("PNG", i->codec)) {
    if ( !image_png_load(i) ) {
      ret = 0;
      goto out;
    }
  }
  else if (!strcmp("GIF", i->codec)) {
    if ( !image_gif_load(i) ) {
      ret = 0;
      goto out;
    }
  }
  else if (!strcmp("BMP", i->codec)) {
    if ( !image_bmp_load(i) ) {
      ret = 0;
      goto out;
    }
  }
  
out:
  return ret;
}

void
image_alloc_pixbuf(MediaScanImage *i, int width, int height)
{
  int size = width * height * sizeof(uint32_t);
  
  // XXX memory_limit
  
  i->_pixbuf = (uint32_t *)malloc(size);
  i->_pixbuf_size = size;
  
  LOG_MEM("new pixbuf for image of size %d x %d (%d bytes)\n",
    width, height, size);
}

void
image_unload(MediaScanImage *i)
{
  if (i->_jpeg)
    image_jpeg_destroy(i);
  
  if (i->_pixbuf_size) {
    LOG_MEM("destroy pixbuf of size %d bytes\n", i->_pixbuf_size);
    
    free(i->_pixbuf);
    i->_pixbuf_size = 0;
  }
}
