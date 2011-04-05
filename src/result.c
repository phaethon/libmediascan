
#include <fcntl.h>
#include <string.h>

#ifndef WIN32
#include <strings.h>
#else
#include "win32config.h"
#endif

#include <sys/stat.h>

// If we are on MSVC, disable some stupid MSVC warnings
#ifdef _MSC_VER
#pragma warning( disable: 4996 )
#pragma warning( disable: 4244 ) 
#endif

#include <libavformat/avformat.h>

#ifdef _MSC_VER
#pragma warning( default: 4244 )
#endif

#include <libmediascan.h>



#include "common.h"
#include "result.h"
#include "error.h"
#include "video.h"
#include "audio.h"
#include "util.h"

// DLNA support
#include "libdlna/dlna.h"
#include "libdlna/profiles.h"

// Audio formats
//#include "wav.h"

const char CODEC_MP1[] = "mp1";

static type_ext audio_types[] = {
/*
  {"mp4", {"mp4", "m4a", "m4b", "m4p", "m4v", "m4r", "k3g", "skm", "3gp", "3g2", "mov", 0}},
  {"aac", {"aac", 0}},
  {"mp3", {"mp3", "mp2", 0}},
  {"ogg", {"ogg", "oga", 0}},
  {"mpc", {"mpc", "mp+", "mpp", 0}},
  {"ape", {"ape", "apl", 0}},
  {"flc", {"flc", "flac", "fla", 0}},
  {"asf", {"wma", "asf", "wmv", 0}},
*/
  {"wav", {"wav", "aif", "aiff", 0}},
//  {"wvp", {"wv", 0}},
  {0, {0, 0}}
};

static type_handler audio_handlers[] = {
/*
  { "mp4", get_mp4tags, 0, mp4_find_frame, mp4_find_frame_return_info },
  { "aac", get_aacinfo, 0, 0, 0 },
  { "mp3", get_mp3tags, get_mp3fileinfo, mp3_find_frame, 0 },
  { "ogg", get_ogg_metadata, 0, ogg_find_frame, 0 },
  { "mpc", get_ape_metadata, get_mpcfileinfo, 0, 0 },
  { "ape", get_ape_metadata, get_macfileinfo, 0, 0 },
  { "flc", get_flac_metadata, 0, flac_find_frame, 0 },
  { "asf", get_asf_metadata, 0, asf_find_frame, 0 },
  { "wav", wav_scan },
  { "wvp", get_ape_metadata, get_wavpack_info, 0 },
*/
  { NULL, 0 }
};

///-------------------------------------------------------------------------------------------------
///  Try to find a matching DLNA profile, this is OK if it fails
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r	   If non-null, the.
/// @param [in,out] codecs If non-null, the codecs.
///-------------------------------------------------------------------------------------------------

static void scan_dlna_profile(MediaScanResult *r, av_codecs_t *codecs)
{
  dlna_registered_profile_t *p;
  dlna_profile_t *profile = NULL;
  dlna_container_type_t st;
  AVFormatContext *avf = (AVFormatContext *)r->_avf;
  dlna_t *dlna = (dlna_t *)((MediaScan *)r->_scan)->_dlna;
  
  st = stream_get_container(avf);
  
  p = dlna->first_profile;
  while (p) {
    dlna_profile_t *prof = NULL;
    
    if (r->flags & USE_EXTENSION) {
      if (p->extensions) {
        /* check for valid file extension */
        if (!match_file_extension (r->path, p->extensions)) {
          p = p->next;
          continue;
        }
      }
    }
    
    prof = p->probe (avf, st, codecs);
    if (prof) {
      profile = prof;
      profile->class = p->class;
      break;
    }
    p = p->next;
  }
  
  if (profile) {
    r->mime_type = profile->mime;
    r->dlna_profile = profile->id;
  }
} /* scan_dlna_profile() */

///-------------------------------------------------------------------------------------------------
///  Ensures the object is open
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///
/// @return .
///-------------------------------------------------------------------------------------------------

static int ensure_opened(MediaScanResult *r)
{
  // Open the file unless we already have an open fd
  if ( !r->_fp ) {
    if ((r->_fp = fopen(r->path, "rb")) == NULL) {
      LOG_WARN("Cannot open %s: %s\n", r->path, strerror(errno));
      return 0;
    }
  }
  return 1;
} /* ensure_opened() */

///-------------------------------------------------------------------------------------------------
///  Sets a file metadata.
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///-------------------------------------------------------------------------------------------------

static void set_file_metadata(MediaScanResult *r)
{
  int fd;
  struct stat buf;
  
  if (r->_avf) {
    // Use ffmpeg API because it already has the file open
    AVFormatContext *avf = r->_avf;
    URLContext *h = url_fileno(avf->pb);
    fd = (intptr_t)h->priv_data;
  }
  else {
    // Open the file if necessary
    if ( !ensure_opened(r) )
      return;
    
    fd = fileno(r->_fp);
  }

  if ( !fstat(fd, &buf) ) {
    r->size = buf.st_size;
    r->mtime = (int)buf.st_mtime;
  }    
} /* set_file_metadata() */

///-------------------------------------------------------------------------------------------------
///  Scan a video file with libavformat
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///
/// @return .
///-------------------------------------------------------------------------------------------------

static int scan_video(MediaScanResult *r)
{
  AVFormatContext *avf = NULL;
  AVInputFormat *iformat = NULL;
  AVCodec *c = NULL;
  MediaScanVideo *v = NULL;
  MediaScanAudio *a = NULL;
  av_codecs_t *codecs = NULL;
  int AVError = 0;
  int ret = 1;

  if (r->flags & USE_EXTENSION) {
    // Set AVInputFormat based on file extension to avoid guessing
    while ((iformat = av_iformat_next(iformat))) {
      if ( av_match_ext(r->path, iformat->name) )
        break;

      if (iformat->extensions) {
        if ( av_match_ext(r->path, iformat->extensions) )
          break;
      }
    }

    if (iformat)
      LOG_INFO("Forcing format: %s\n", iformat->name);
  }

  if ( (AVError = av_open_input_file(&avf, r->path, iformat, 0, NULL)) != 0 ) {
    r->error = error_create(r->path, MS_ERROR_FILE, "[libavformat] Unable to open file for reading");
    r->error->averror = AVError;
    ret = 0;
    goto out;
  }

  if ( (AVError = av_find_stream_info(avf)) < 0 ) {
    r->error = error_create(r->path, MS_ERROR_READ, "[libavformat] Unable to find stream info");
    r->error->averror = AVError;
    ret = 0;
	av_close_input_file(avf);
    goto out;
  }

  r->_avf = (void *)avf;
  
  // Use libdlna's handy codecs struct
  codecs = av_profile_get_codecs(avf);
  if (!codecs) {
    ret = 0;
    goto out;
  }

  if ( stream_ctx_is_audio(codecs) ) {
    // XXX some extensions (e.g. mp4) can be either video or audio,
    // if after checking we don't find a video stream, we need to
    // send the result through the audio path
    LOG_WARN("XXX Scanning audio file with video path\n");
    ret = 0;
    goto out;
  }
  
  scan_dlna_profile(r, codecs);
  
  // General metadata
  set_file_metadata(r);

  r->bitrate     = avf->bit_rate;
  r->duration_ms = (int)(avf->duration / 1000);

  // Video-specific metadata
  v = r->video = video_create();
  
  c = avcodec_find_decoder(codecs->vc->codec_id);  
  if (c) {
    v->codec = c->name;
  }
  else if (codecs->vc->codec_name[0] != '\0') {
    v->codec = codecs->vc->codec_name;
  }
  else {
    v->codec = "Unknown";
  }
  
  v->width = codecs->vc->width;
  v->height = codecs->vc->height;
  v->fps = av_q2d(codecs->vs->avg_frame_rate);
  
  // Audio metadata from the primary audio stream
  if (codecs->ac) {
    a = r->audio = audio_create();
    
    c = avcodec_find_decoder(codecs->ac->codec_id);  
    if (c) {
      a->codec = c->name;
    }
    else if (codecs->ac->codec_name[0] != '\0') {
      a->codec = codecs->ac->codec_name;
    }
	// Special case for handling MP1 audio streams which FFMPEG can't identify a codec for
	else if (codecs->ac->codec_id == CODEC_ID_MP1) { 
	  a->codec = CODEC_MP1;
	}
    else {
      a->codec = "Unknown";
    }
    
    a->bitrate = codecs->ac->bit_rate;
    a->samplerate = codecs->ac->sample_rate;
    a->channels = codecs->ac->channels;    
  }
  
  // XXX additional streams(?), thumbnails, tags
out:
  if (codecs)
    free(codecs);

  return ret;
} /* scan_video() */

///-------------------------------------------------------------------------------------------------
///  Result create.
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] s If non-null, the.
///
/// @return null if it fails, else.
///-------------------------------------------------------------------------------------------------

MediaScanResult *result_create(MediaScan *s)
{
  MediaScanResult *r = (MediaScanResult *)calloc(sizeof(MediaScanResult), 1);
  if (r == NULL) {
	ms_errno = MSENO_MEMERROR;
    FATAL("Out of memory for new MediaScanResult object\n");
    return NULL;
  }
  
  LOG_MEM("new MediaScanResult @ %p\n", r);
  
  r->type = TYPE_UNKNOWN;
  r->flags = USE_EXTENSION;
  r->path = NULL;
  r->error = NULL;
  
  r->mime_type = NULL;
  r->dlna_profile = NULL;
  r->size = 0;
  r->mtime = 0;
  r->bitrate = 0;
  r->duration_ms = 0;
  
  r->audio = NULL;
  r->image = NULL;
  r->video = NULL;
  
  r->_scan = s;
  r->_avf = NULL;
  r->_fp = NULL;
  
  return r;
} /* result_create() */

///-------------------------------------------------------------------------------------------------
///  Result scan.
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///
/// @return .
///-------------------------------------------------------------------------------------------------

int result_scan(MediaScanResult *r)
{
  if (!r->type || !r->path) {
    r->error = error_create("", MS_ERROR_TYPE_INVALID_PARAMS, "Invalid parameters passed to result_scan()");
    return 0;
  }
  
  switch (r->type) {
    case TYPE_VIDEO:
      return scan_video(r);
      break;
    
    default:
      break;
  }
  
  return 0;
} /* result_scan() */

///-------------------------------------------------------------------------------------------------
///  Result destroy.
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///-------------------------------------------------------------------------------------------------

void result_destroy(MediaScanResult *r)
{
  if (r->error)
    error_destroy(r->error);
  
  if (r->video)
    video_destroy(r->video);

  if (r->audio)
    audio_destroy(r->audio);
  
  if (r->_avf)
  {
    av_close_input_file(r->_avf);
  }
  
  if (r->_fp)
    fclose(r->_fp);

  LOG_MEM("destroy MediaScanResult @ %p\n", r);
  free(r);
} /* result_destroy() */

///-------------------------------------------------------------------------------------------------
///  Dump result to the log output
///
/// @author Andy Grundman
/// @date 03/24/2011
///
/// @param [in,out] r If non-null, the.
///-------------------------------------------------------------------------------------------------

void ms_dump_result(MediaScanResult *r)
{
  LOG_OUTPUT("%s\n", r->path);
  LOG_OUTPUT("  MIME type:    %s\n", r->mime_type);
  LOG_OUTPUT("  DLNA profile: %s\n", r->dlna_profile);
  LOG_OUTPUT("  File size:    %lld\n", r->size);
  LOG_OUTPUT("  Modified:     %d\n", r->mtime);
  LOG_OUTPUT("  Bitrate:      %d bps\n", r->bitrate);
  LOG_OUTPUT("  Duration:     %d ms\n", r->duration_ms);
  
  switch (r->type) {
    case TYPE_VIDEO:
      LOG_OUTPUT("  Video: %s\n", r->video->codec);
      LOG_OUTPUT("    Dimensions: %d x %d\n", r->video->width, r->video->height);
      LOG_OUTPUT("    Framerate:  %.2f\n", r->video->fps);
      if (r->audio) {
        LOG_OUTPUT("  Audio: %s\n", r->audio->codec);
        LOG_OUTPUT("    Bitrate:    %d bps\n", r->audio->bitrate);
        LOG_OUTPUT("    Samplerate: %d kHz\n", r->audio->samplerate);
        LOG_OUTPUT("    Channels:   %d\n", r->audio->channels);
      }
      LOG_OUTPUT("  FFmpeg details:\n");
      av_dump_format(r->_avf, 0, r->path, 0);
      break;
      
    default:
      LOG_OUTPUT("  Type: Unknown\n");
      break;
  } 
} /* ms_dump_result() */

/*
static type_handler *
get_type_handler(char *ext, type_ext *types, type_handler *handlers)
{
  int typeindex = -1;
  int i, j;
  type_handler *hdl = NULL;
  
  for (i = 0; typeindex == -1 && types[i].type; i++) {
    for (j = 0; typeindex == -1 && types[i].ext[j]; j++) {
#ifdef _MSC_VER
      if (!stricmp(types[i].ext[j], ext)) {
#else
      if (!strcasecmp(types[i].ext[j], ext)) {
#endif
        typeindex = i;
        break;
      }
    }
  }
  
  LOG_DEBUG("typeindex: %d\n", typeindex);
    
  if (typeindex > -1) {
    for (hdl = handlers; hdl->type; ++hdl)
      if (!strcmp(hdl->type, types[typeindex].type))
        break;
  }
  
  if (hdl)
    LOG_DEBUG("type handler: %s\n", hdl->type);
  
  return hdl;
}

static void
scan_audio(ScanData s)
{
  char *ext = strrchr(s->path, '.');
  if (ext == NULL)
    return;
  
  type_handler *hdl = get_type_handler(ext + 1, audio_types, audio_handlers);
  if (hdl == NULL)
    return;
  
  // Open the file unless we already have an open fd
  int opened = s->fp != NULL;
  if (!opened) {
    if ((s->fp = fopen(s->path, "rb")) == NULL) {
      LOG_WARN("Cannot open %s: %s\n", s->path, strerror(errno));
      return;
    }
  }

  hdl->scan(s);

  // Close the file if we opened it
  if (!opened) {
    fclose(s->fp);
    s->fp = NULL;
  }
  
  return;
}

static void
scan_image(ScanData s)
{
  // XXX
}

ScanData
mediascan_new_ScanData(const char *path, int flags, int type)
{
  ScanData s = (ScanData)calloc(sizeof(struct _ScanData), 1);
  if (s == NULL) {
    FATAL("Out of memory for new ScanData object\n");
    return NULL;
  }
  
  s->path = path;
  s->flags = flags;
  s->type = type;
  
  switch (type) {
    case TYPE_VIDEO:
      scan_video(s);
      break;
      
    case TYPE_AUDIO:
      scan_audio(s);
      break;
      
    case TYPE_IMAGE:
      scan_image(s);
      break;
  }
    
  return s;
}

void
mediascan_free_ScanData(ScanData s)
{
  if (s->streams)
    free(s->streams);
  
  if (s->_avf != NULL)
    av_close_input_file(s->_avf);
  
  free(s);
}

void
mediascan_add_StreamData(ScanData s, int nstreams)
{
  s->nstreams = nstreams;
  s->streams = (StreamData)calloc(sizeof(struct _StreamData), nstreams);
}

*/
