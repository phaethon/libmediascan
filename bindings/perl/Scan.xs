#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"
#include "xs_object_magic.h"

#include <libmediascan.h>

// Include the XS::Object::Magic code inline to get
// around some problems on Windows
#include "Magic.c"

// Enable for debug output
#define MEDIA_SCAN_DEBUG

#ifdef MEDIA_SCAN_DEBUG
# define DEBUG_TRACE(...) PerlIO_printf(PerlIO_stderr(), __VA_ARGS__)
#else
# define DEBUG_TRACE(...)
#endif

#define my_hv_store(a,b,c)     hv_store(a,b,strlen(b),c,0)
#define my_hv_store_ent(a,b,c) hv_store_ent(a,b,c,0)
#define my_hv_fetch(a,b)       hv_fetch(a,b,strlen(b),0)
#define my_hv_exists(a,b)      hv_exists(a,b,strlen(b))
#define my_hv_exists_ent(a,b)  hv_exists_ent(a,b,0)
#define my_hv_delete(a,b)      hv_delete(a,b,strlen(b),0)

static void
_on_result(MediaScan *s, MediaScanResult *result, void *userdata)
{
  HV *selfh = (HV *)userdata;
  SV *obj = NULL;
  SV *callback = NULL;
  
  if (!my_hv_exists(selfh, "on_result"))
    return;
  
  callback = *(my_hv_fetch(selfh, "on_result"));
  obj = newRV_noinc(newSVpvn("", 0));
  
  switch (result->type) {
    case TYPE_VIDEO:
      sv_bless(obj, gv_stashpv("Media::Scan::Video", 0));
      break;
    
    case TYPE_AUDIO:
      sv_bless(obj, gv_stashpv("Media::Scan::Audio", 0));
      break;
    
    case TYPE_IMAGE:
      sv_bless(obj, gv_stashpv("Media::Scan::Image", 0));
      break;
    
    default:
      break;
  }
  
  xs_object_magic_attach_struct(aTHX_ SvRV(obj), (void *)result);
  
  {
    dSP;
    PUSHMARK(SP);
    XPUSHs(obj);
    PUTBACK;
    
    call_sv(callback, G_VOID | G_DISCARD | G_EVAL);
    
    SPAGAIN;
    if (SvTRUE(ERRSV)) {
      warn("Error in on_result callback (ignored): %s", SvPV_nolen(ERRSV));
      POPs;
    }
  }
}

static void
_on_error(MediaScan *s, MediaScanError *error, void *userdata)
{
  HV *selfh = (HV *)userdata;
  SV *obj = NULL;
  SV *callback = NULL;
  
  if (!my_hv_exists(selfh, "on_error"))
    return;
  
  callback = *(my_hv_fetch(selfh, "on_error"));
  obj = newRV_noinc(newSVpvn("", 0));
  sv_bless(obj, gv_stashpv("Media::Scan::Error", 0));
  xs_object_magic_attach_struct(aTHX_ SvRV(obj), (void *)error);
  
  {
    dSP;
    PUSHMARK(SP);
    XPUSHs(obj);
    PUTBACK;
    call_sv(callback, G_VOID | G_DISCARD | G_EVAL);
    
    SPAGAIN;
    if (SvTRUE(ERRSV)) {
      warn("Error in on_error callback (ignored): %s", SvPV_nolen(ERRSV));
      POPs;
    }
  }
}

static void
_on_progress(MediaScan *s, MediaScanProgress *progress, void *userdata)
{
  HV *selfh = (HV *)userdata;
  SV *obj = NULL;
  SV *callback = NULL;
  
  if (!my_hv_exists(selfh, "on_progress"))
    return;
  
  callback = *(my_hv_fetch(selfh, "on_progress"));
  obj = newRV_noinc(newSVpvn("", 0));
  sv_bless(obj, gv_stashpv("Media::Scan::Progress", 0));
  xs_object_magic_attach_struct(aTHX_ SvRV(obj), (void *)progress);
  
  {
    dSP;
    PUSHMARK(SP);
    XPUSHs(obj);
    PUTBACK;
    call_sv(callback, G_VOID | G_DISCARD | G_EVAL);
    
    SPAGAIN;
    if (SvTRUE(ERRSV)) {
      warn("Error in on_progress callback (ignored): %s", SvPV_nolen(ERRSV));
      POPs;
    }
  }
}

MODULE = Media::Scan		PACKAGE = Media::Scan		

void
xs_new(SV *self)
CODE:
{
  MediaScan *s = ms_create();
  xs_object_magic_attach_struct(aTHX_ SvRV(self), s);
}

void
set_log_level(MediaScan *, int level)
CODE:
{
  ms_set_log_level(level);
}

void
set_progress_interval(MediaScan *s, int seconds)
CODE:
{
  ms_set_progress_interval(s, seconds);
}

void
xs_scan(SV *self)
CODE:
{
  int i;
  MediaScan *s = xs_object_magic_get_struct_rv(aTHX_ self);
  HV *selfh = (HV *)SvRV(self);
  AV *paths, *ignore;
  int async;
  
  // Set log level
  ms_set_log_level( SvIV(*(my_hv_fetch(selfh, "loglevel"))) );
  
  // Set paths to scan
  paths = (AV *)SvRV(*(my_hv_fetch(selfh, "paths")));
  for (i = 0; i < av_len(paths) + 1; i++) {
    SV **path = av_fetch(paths, i, 0);
    if (path != NULL && SvPOK(*path))
      ms_add_path(s, SvPVX(*path));
  }
  
  // Set extensions to ignore
  ignore = (AV *)SvRV(*(my_hv_fetch(selfh, "ignore")));
  for (i = 0; i < av_len(ignore) + 1; i++) {
    SV **ext = av_fetch(ignore, i, 0);
    if (ext != NULL && SvPOK(*ext))
      ms_add_ignore_extension(s, SvPVX(*ext));
  }
  
  // Set async or sync operation
  async = SvIV(*(my_hv_fetch(selfh, "async")));
  ms_set_async(s, async ? 1 : 0);
  
  // Set callbacks
  ms_set_result_callback(s, _on_result);
  ms_set_error_callback(s, _on_error);
  ms_set_progress_callback(s, _on_progress);
  
  ms_set_userdata(s, (void *)selfh);
  
  ms_scan(s);
}

void
DESTROY(MediaScan *s)
CODE:
{
  ms_destroy(s);
}

INCLUDE: xs/Error.xs
INCLUDE: xs/Progress.xs
INCLUDE: xs/Result.xs
INCLUDE: xs/Video.xs
