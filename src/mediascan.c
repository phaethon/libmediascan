#include <ctype.h>
#include <dirent.h>

// Global debug flag, used by LOG_LEVEL macro
int Debug = 0;

#include <libmediascan.h>
#include "common.h"
#include "queue.h"
#include "progress.h"

#include <libavformat/avformat.h>

static int Initialized = 0;
static long PathMax = 0;

// File/dir queue struct definitions
struct fileq_entry {
  char *file;
  SIMPLEQ_ENTRY(fileq_entry) entries;
};
SIMPLEQ_HEAD(fileq, fileq_entry);

struct dirq_entry {
  char *dir;
  struct fileq *files;
  int processed;
  SIMPLEQ_ENTRY(dirq_entry) entries;
};
SIMPLEQ_HEAD(dirq, dirq_entry);

/*
 Video support
 -------------
 We are only supporting a subset of FFmpeg's supported formats:
 
 Containers:   MPEG-4, ASF, AVI, Matroska, VOB, FLV, WebM
 Video codecs: h.264, MPEG-1, MPEG-2, MPEG-4, Microsoft MPEG-4, WMV, VP6F (FLV), VP8
 Audio codecs: AAC, AC3, DTS, MP3, MP2, Vorbis, WMA
 Subtitles:    All
 
 Audio support
 -------------
 TODO
 
 Image support
 -------------
 TODO
*/

// File extensions to look for (leading/trailing comma are required)
static const char *AudioExts = ",aif,aiff,wav,";
static const char *VideoExts = ",asf,avi,divx,flv,m2t,m4v,mkv,mov,mpg,mpeg,mp4,m2p,m2t,mts,m2ts,ts,vob,webm,wmv,xvid,3gp,3g2,3gp2,3gpp";
static const char *ImageExts = "";

#define REGISTER_DECODER(X,x) { \
          extern AVCodec x##_decoder; \
          avcodec_register(&x##_decoder); }
#define REGISTER_PARSER(X,x) { \
          extern AVCodecParser x##_parser; \
          av_register_codec_parser(&x##_parser); }

static void
register_codecs(void)
{
  // Video codecs
  REGISTER_DECODER (H264, h264);
  REGISTER_DECODER (MPEG1VIDEO, mpeg1video);
  REGISTER_DECODER (MPEG2VIDEO, mpeg2video);
  REGISTER_DECODER (MPEG4, mpeg4);
  REGISTER_DECODER (MSMPEG4V1, msmpeg4v1);
  REGISTER_DECODER (MSMPEG4V2, msmpeg4v2);
  REGISTER_DECODER (MSMPEG4V3, msmpeg4v3);
  REGISTER_DECODER (VP6F, vp6f);
  REGISTER_DECODER (VP8, vp8);
  REGISTER_DECODER (WMV1, wmv1);
  REGISTER_DECODER (WMV2, wmv2);
  REGISTER_DECODER (WMV3, wmv3);
  
  // Audio codecs, needed to get details of audio tracks in videos
  REGISTER_DECODER (AAC, aac);
  REGISTER_DECODER (AC3, ac3);
  REGISTER_DECODER (DCA, dca); // DTS
  REGISTER_DECODER (MP3, mp3);
  REGISTER_DECODER (MP2, mp2);
  REGISTER_DECODER (VORBIS, vorbis);
  REGISTER_DECODER (WMAPRO, wmapro);
  REGISTER_DECODER (WMAV1, wmav1);
  REGISTER_DECODER (WMAV2, wmav2);
  REGISTER_DECODER (WMAVOICE, wmavoice);
  
  // Subtitles
  REGISTER_DECODER (ASS, ass);
  REGISTER_DECODER (DVBSUB, dvbsub);
  REGISTER_DECODER (DVDSUB, dvdsub);
  REGISTER_DECODER (PGSSUB, pgssub);
  REGISTER_DECODER (XSUB, xsub);
  
  // Parsers 
  REGISTER_PARSER (AAC, aac);
  REGISTER_PARSER (AC3, ac3);
  REGISTER_PARSER (DCA, dca); // DTS
  REGISTER_PARSER (H264, h264);
  REGISTER_PARSER (MPEG4VIDEO, mpeg4video);
  REGISTER_PARSER (MPEGAUDIO, mpegaudio);
  REGISTER_PARSER (MPEGVIDEO, mpegvideo);
}

#define REGISTER_DEMUXER(X,x) { \
    extern AVInputFormat x##_demuxer; \
    av_register_input_format(&x##_demuxer); }
#define REGISTER_PROTOCOL(X,x) { \
    extern URLProtocol x##_protocol; \
    av_register_protocol2(&x##_protocol, sizeof(x##_protocol)); }

static void
register_formats(void)
{
  // demuxers
  REGISTER_DEMUXER (ASF, asf);
  REGISTER_DEMUXER (AVI, avi);
  REGISTER_DEMUXER (FLV, flv);
  REGISTER_DEMUXER (H264, h264);
  REGISTER_DEMUXER (MATROSKA, matroska);
  REGISTER_DEMUXER (MOV, mov);
  REGISTER_DEMUXER (MPEGPS, mpegps);             // VOB files
  REGISTER_DEMUXER (MPEGVIDEO, mpegvideo);
  
  // protocols
  REGISTER_PROTOCOL (FILE, file);
}

static void
_init(void)
{
  if (Initialized)
    return;
  
  register_codecs();
  register_formats();
  //av_register_all();
  
  PathMax = pathconf(".", _PC_PATH_MAX); // 1024
  
  Initialized = 1;
}

static inline int
_is_media(const char *path)
{
  char *ext = strrchr(path, '.');
  if (ext != NULL) {
    // Copy the extension and lowercase it
    char extc[10];
    extc[0] = ',';
    strncpy(extc + 1, ext + 1, 7);
    extc[9] = 0;
    
    char *p = &extc[1];
    while (*p != 0) {
      *p = tolower(*p);
      p++;
    }
    *p++ = ',';
    *p = 0;
    
    char *found;
    
    found = strstr(AudioExts, extc);
    if (found)
      return TYPE_AUDIO;
    
    found = strstr(VideoExts, extc);
    if (found)
      return TYPE_VIDEO;
    
    found = strstr(ImageExts, extc);
    if (found)
      return TYPE_IMAGE;
    
    return 0;
  }
  return 0;
}

void
ms_set_log_level(int level)
{
  Debug = level;
}

MediaScan *
ms_create(void)
{
  _init();
  
  MediaScan *s = (MediaScan *)calloc(sizeof(MediaScan), 1);
  if (s == NULL) {
    LOG_ERROR("Out of memory for new MediaScan object\n");
    return NULL;
  }
  
  LOG_LEVEL(9, "new MediaScan @ %p\n", s);
  
  s->npaths = 0;
  s->paths[0] = NULL;
  s->nignore_exts = 0;
  s->ignore_exts[0] = NULL;
  s->async = 0;
  
  s->progress = progress_create();
  
  s->on_result = NULL;
  s->on_error = NULL;
  s->on_progress = NULL;
  
  // List of all dirs found
  s->_dirq = malloc(sizeof(struct dirq));
  SIMPLEQ_INIT((struct dirq *)s->_dirq);
  
  return s;
}

void
ms_destroy(MediaScan *s)
{
  int i;
  
  for (i = 0; i < s->npaths; i++) {
    LOG_LEVEL(9, "free s->paths[%d] (%s)\n", i, s->paths[i]);
    free( s->paths[i] );
  }
  
  for (i = 0; i < s->nignore_exts; i++) {
    LOG_LEVEL(9, "free s->ignore_exts[%d] (%s)\n", i, s->ignore_exts[i]);
    free( s->ignore_exts[i] );
  }
  
  // Free everything in our list of dirs/files
  struct dirq_entry *entry;
  struct fileq_entry *file_entry;
  SIMPLEQ_FOREACH(entry, (struct dirq *)s->_dirq, entries) {
    if (entry->files != NULL) {
      SIMPLEQ_FOREACH(file_entry, entry->files, entries) {
        LOG_LEVEL(9, "  free fileq entry %s\n", file_entry->file);
        free(file_entry->file);
        free(file_entry);
      }
      free(entry->files);
    }
    LOG_LEVEL(9, "free dirq entry %s\n", entry->dir);
    free(entry->dir);
    free(entry);
  }
  
  free(s->_dirq);
  
  progress_destroy(s->progress);
  
  LOG_LEVEL(9, "free MediaScan @ %p\n", s);
  free(s);
}

void
ms_add_path(MediaScan *s, const char *path)
{
  if (s->npaths == MAX_PATHS) {
    LOG_ERROR("Path limit reached (%d)\n", MAX_PATHS);
    return;
  }
  
  int len = strlen(path) + 1;
  char *tmp = malloc(len);
  if (tmp == NULL) {
    LOG_ERROR("Out of memory for adding path\n");
    return;
  }
  
  strncpy(tmp, path, len);
  
  s->paths[ s->npaths++ ] = tmp;
}

void
ms_add_ignore_extension(MediaScan *s, const char *extension)
{
  if (s->nignore_exts == MAX_IGNORE_EXTS) {
    LOG_ERROR("Ignore extension limit reached (%d)\n", MAX_IGNORE_EXTS);
    return;
  }
  
  int len = strlen(extension) + 1;
  char *tmp = malloc(len);
  if (tmp == NULL) {
    LOG_ERROR("Out of memory for ignore extension\n");
    return;
  }
  
  strncpy(tmp, extension, len);
  
  s->ignore_exts[ s->nignore_exts++ ] = tmp;
}

void
ms_set_async(MediaScan *s, int enabled)
{
  s->async = enabled ? 1 : 0;
}

void
ms_set_result_callback(MediaScan *s, ResultCallback callback)
{
  s->on_result = callback;
}

void
ms_set_error_callback(MediaScan *s, ErrorCallback callback)
{
  s->on_error = callback;
}

void
ms_set_progress_callback(MediaScan *s, ProgressCallback callback)
{
  s->on_progress = callback;
}

static int
_should_scan(MediaScan *s, const char *path)
{
  return 1;
}

static void
recurse_dir(MediaScan *s, const char *path, struct dirq_entry *curdir)
{
  char *dir;

  if (path[0] != '/') { // XXX Win32
    // Get full path
    char *buf = (char *)malloc((size_t)PathMax);
    if (buf == NULL) {
      LOG_ERROR("Out of memory for directory scan\n");
      return;
    }

    dir = getcwd(buf, (size_t)PathMax);
    strcat(dir, "/");
    strcat(dir, path);
  }
  else {
    dir = strdup(path);
  }

  // Strip trailing slash if any
  char *p = &dir[0];
  while (*p != 0) {
#ifdef _WIN32
    if (p[1] == 0 && (*p == '/' || *p == '\\'))
#else
    if (p[1] == 0 && *p == '/')
#endif
      *p = 0;
    p++;
  }
  
  LOG_LEVEL(2, "dir: %s\n", dir);
  curdir->processed = 1;

  DIR *dirp;
  if ((dirp = opendir(dir)) == NULL) {
    LOG_ERROR("Unable to open directory %s: %s\n", dir, strerror(errno));
    goto out;
  }

  char *tmp = malloc((size_t)PathMax);

  struct dirent *dp;
  while ((dp = readdir(dirp)) != NULL) {
    char *name = dp->d_name;

    // skip all dot files
    if (name[0] != '.') {
      // XXX some platforms may be missing d_type/DT_DIR
      if (dp->d_type == DT_DIR) {
        // Construct full path
        *tmp = 0;
        strcat(tmp, dir);
        strcat(tmp, "/");
        strcat(tmp, name);
        
        // Entry for complete list of dirs
        struct dirq_entry *entry = malloc(sizeof(struct dirq_entry));
        entry->dir = strdup(tmp);
        entry->files = NULL;
        entry->processed = 0;
        SIMPLEQ_INSERT_TAIL((struct dirq *)s->_dirq, entry, entries);
        
        s->progress->dir_total++;
        
        LOG_LEVEL(2, "  [%5d] subdir: %s\n", s->progress->dir_total, entry->dir);
      }
      else {
        if ( _should_scan(s, name) ) {
          // We don't need to store the full path to every file          
          struct fileq_entry *entry = malloc(sizeof(struct fileq_entry));
          entry->file = strdup(name);
          
          if (curdir->files == NULL) {
            LOG_LEVEL(9, "malloc curdir->files for %s\n", curdir->dir);
            curdir->files = malloc(sizeof(struct fileq));
            SIMPLEQ_INIT((struct fileq *)curdir->files);
          }
          
          SIMPLEQ_INSERT_TAIL(curdir->files, entry, entries);
          
          s->progress->file_total++;
          
          LOG_LEVEL(2, "  [%5d] file: %s\n", s->progress->file_total, entry->file);
        }
      }
    }
  }
    
/*    
          *tmp = 0;
          strcat(tmp, dir);
          strcat(tmp, "/");
          strcat(tmp, dp->d_name);

          LOG_LEVEL(1, "  [file] %s\n", tmp);

          // Scan the file
          MediaScanResult *r = scan_file(s, tmp);
          if (r->error->error_code > 0) {
            if (s->on_error) {
              s->on_error(s, r->error);
            }
          }
          else {
            s->on_result(s, r);
          }
          
          if (s->on_progress) {
            s->on_progress(s, s->progress);
          }
          
          result_destroy(r);
        }
      }
    }
  }
*/

  closedir(dirp);

  // process any new subdirs (processed == 0)
  if ( !SIMPLEQ_EMPTY((struct dirq *)s->_dirq) ) {
    struct dirq_entry *entry;
    
    SIMPLEQ_FOREACH(entry, (struct dirq *)s->_dirq, entries) {
      if (entry->processed == 0)
        recurse_dir(s, entry->dir, entry);
    }
  }
  
  free(tmp);

out:
  free(dir);
}

void
ms_scan(MediaScan *s)
{
  if (s->on_result == NULL) {
    LOG_ERROR("Result callback not set, aborting scan\n");
    return;
  }
  
  if (s->async) {
    LOG_ERROR("async mode not yet supported\n");
    // XXX TODO
  }
  
  int i;  
  for (i = 0; i < s->npaths; i++) {
    struct dirq_entry *entry = malloc(sizeof(struct dirq_entry));
    entry->dir = strdup("/"); // so free doesn't choke on this item later
    entry->files = NULL;
    SIMPLEQ_INSERT_TAIL((struct dirq *)s->_dirq, entry, entries);
    
    LOG_LEVEL(1, "Scanning %s\n", s->paths[i]);
    recurse_dir(s, s->paths[i], entry);
  }
}

/*
ScanData
mediascan_scan_file(const char *path, int flags)
{
  _init();
  
  ScanData s = NULL;
  int type = _is_media(path);
            
  if (type == TYPE_VIDEO && !(flags & SKIP_VIDEO)) {
    s = mediascan_new_ScanData(path, flags, type);
  }
  else if (type == TYPE_AUDIO && !(flags & SKIP_AUDIO)) {
    s = mediascan_new_ScanData(path, flags, type);
  }
  else if (type == TYPE_IMAGE && !(flags & SKIP_IMAGE)) {
    s = mediascan_new_ScanData(path, flags, type);
  }
  
  return s;
}

*/
