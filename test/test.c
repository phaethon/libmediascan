
#include <stdio.h>
#include <string.h>


#ifdef WIN32
#include <Windows.h>
#include <direct.h>
#endif

#include <limits.h>
#include <libmediascan.h>
#include <libavformat/avformat.h>

#include "../src/mediascan.h"
#include "../src/common.h"
#include "CUnit/CUnit/Headers/Basic.h"

int setupbackground_tests();
int setup_thumbnail_tests();
int setupdefect_tests();

#ifdef _MSC_VER
/*
int strcasecmp(const char *string1, const char *string2 )
{
	return _stricmp(string1, string2);
}


int strncasecmp(const char *string1, const char *string2, size_t count )
{
	return _strnicmp(string1, string2, count);
}
*/
#endif


static int result_called = FALSE;
static MediaScanResult result;

static void my_result_callback(MediaScan *s, MediaScanResult *r, void *userdata) {

	result.type = r->type;
	result.path = strdup(r->path);
	result.flags = r->flags;

	if(r->error)
		memcpy(result.error, r->error, sizeof(MediaScanError));

	result.mime_type = strdup(r->mime_type);
	result.dlna_profile = strdup(r->dlna_profile);
	result.size = r->size;
	result.mtime = r->mtime;
	result.bitrate = r->bitrate;
	result.duration_ms = r->duration_ms;

	if(r->audio)
	{
		result.audio = malloc(sizeof(MediaScanAudio));
		memcpy( result.audio, r->audio, sizeof(MediaScanAudio));
	}

	if(r->video)
	{
		result.video = malloc(sizeof(MediaScanVideo));
		memcpy( result.video, r->video, sizeof(MediaScanVideo));
	}

	if(r->image)
	{
		result.image = malloc(sizeof(MediaScanImage));
		memcpy( result.image, r->image, sizeof(MediaScanImage));
	}

	result_called = TRUE;
}

static void my_error_callback(MediaScan *s, MediaScanError *error, void *userdata) { 

} /* my_error_callback() */

///-------------------------------------------------------------------------------------------------
///  Unit Test Scan Function.
///
/// @author Henry Bennett
/// @date 03/16/2011
///
/// ### remarks Henry Bennett, 03/15/2011.
///-------------------------------------------------------------------------------------------------

void test_ms_scan(void){
	#ifdef WIN32
	const char dir[MAX_PATH_STR_LEN] = "data\\video\\dlna";
	#else
	const char dir[MAX_PATH_STR_LEN] = "data/video/dlna";
	#endif
	
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	CU_ASSERT(s->npaths == 0);
	ms_add_path(s, dir);
	CU_ASSERT(s->npaths == 1);
	CU_ASSERT_STRING_EQUAL(s->paths[0], dir);


	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	ms_scan(s);

	ms_destroy(s);
}

///-------------------------------------------------------------------------------------------------
///  This test will check the scanning functionality without adding a directory to the scan
/// 	list.
///
/// @author Henry Bennett
/// @date 03/16/2011
///
/// ### remarks Henry Bennett, 03/16/2011.
///-------------------------------------------------------------------------------------------------

void test_ms_scan_2(void)
{
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	// Do not add a path and then attempt to scan
	CU_ASSERT(s->npaths == 0);

	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	ms_scan(s);

	ms_destroy(s);
}

///-------------------------------------------------------------------------------------------------
///  Robustness test for ms_scan. Try to add more than 64 directories to scan.
///
/// @author Henry Bennett
/// @date 03/16/2011
///
/// ### remarks Henry Bennett, 03/16/2011.
///-------------------------------------------------------------------------------------------------

void test_ms_scan_3(void)	{

	char dir[MAX_PATH_STR_LEN];
	char dir2[MAX_PATH_STR_LEN];
	int i = 0;

	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	CU_ASSERT(s->npaths == 0);

	// Add 128 Paths to the list, which should work fine
	for(i = 0; i < MAX_PATHS; i++)
	{
		sprintf(dir, "testdir%04d", i);
		CU_ASSERT(s->npaths == (i) );
		ms_add_path(s, dir);
		CU_ASSERT(s->npaths == (i+1) );
		CU_ASSERT_STRING_EQUAL(s->paths[i], dir);
	}

	CU_ASSERT(s->npaths == MAX_PATHS );
	CU_ASSERT_STRING_EQUAL(s->paths[MAX_PATHS - 1], dir);

	strcpy(dir2, dir);
	sprintf(dir, "toomany");
	ms_add_path(s, dir); // This will fail

	// Make sure number of paths hasn't gone up and the last entry is the same
	CU_ASSERT(s->npaths == MAX_PATHS );
	CU_ASSERT_STRING_EQUAL(s->paths[MAX_PATHS - 1], dir2);

	ms_destroy(s);
}

///-------------------------------------------------------------------------------------------------
///  Test ms_scan with a non-exsistent directory structure.
///
/// @author Henry Bennett
/// @date 03/16/2011
///-------------------------------------------------------------------------------------------------

void test_ms_scan_4(void)	{
	char dir[MAX_PATH_STR_LEN] = "notadirectory";
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);


	CU_ASSERT(s->npaths == 0);
	ms_add_path(s, dir);
	CU_ASSERT(s->npaths == 1);
	CU_ASSERT_STRING_EQUAL(s->paths[0], dir);

	// Won't run without the call backs in place
	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	// Okay this is the test, try to scan a non-exsistent directory structure
	ms_scan(s);
	CU_ASSERT(ms_errno == MSENO_DIRECTORYFAIL); // If we got this errno, then we got the failure we wanted

	ms_destroy(s);
} /* test_ms_scan_4 */

///-------------------------------------------------------------------------------------------------
///  Test ms_scan's path stripping functionality
///
/// @author Henry Bennett
/// @date 03/16/2011
///-------------------------------------------------------------------------------------------------

void test_ms_scan_5(void)	{
		#define NUM_STRINGS 3
	
	int i = 0;
	char in_dir[NUM_STRINGS][MAX_PATH_STR_LEN] = 
		{ "data", "data/", "data\\" };
	char out_dir[NUM_STRINGS][MAX_PATH_STR_LEN] = 
		{ "data", "data/", "data\\" };

	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	for(i = 0; i < NUM_STRINGS; i++)
	{
	ms_add_path(s, in_dir[i]);
	CU_ASSERT(s->npaths == i + 1);
	CU_ASSERT_STRING_EQUAL(s->paths[i], in_dir[i]);
	}

	// Won't run without the call backs in place
	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	// Run the scan
	ms_errno = 0;
	ms_scan(s);
	CU_ASSERT(ms_errno != MSENO_DIRECTORYFAIL); // Should get no error if the stripping worked correctly

	ms_destroy(s);
} /* test_ms_scan_5 */

static int progress_called = FALSE;

static void my_progress_callback_6(MediaScan *s, MediaScanProgress *progress, void *userdata) {
  // Check final progress callback only
//  if (!progress->cur_item) {
//    ok(progress->dir_total == 3, "final progress callback dir_total is %d", progress->dir_total);
//    ok(progress->file_total == 22, "final progress callback file_total is %d", progress->file_total);
//  }
//  
	if(s->progress->cur_item != NULL)
		progress_called = TRUE;

} /* my_progress_callback() */

///-------------------------------------------------------------------------------------------------
///  Test ms_scan's on progress notification
///
/// @author Henry Bennett
/// @date 03/16/2011
///-------------------------------------------------------------------------------------------------

void test_ms_scan_6(void)	{
#ifdef WIN32
	char dir[MAX_PATH_STR_LEN] = "data\\audio\\mp3";
#else
	char dir[MAX_PATH_STR_LEN] = "data/audio/mp3";
#endif
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);


	CU_ASSERT(s->npaths == 0);
	ms_add_path(s, dir);
	CU_ASSERT(s->npaths == 1);
	CU_ASSERT_STRING_EQUAL(s->paths[0], dir);


	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	CU_ASSERT(s->on_progress == NULL);
	ms_set_progress_callback(s, my_progress_callback_6); 
	CU_ASSERT(s->on_progress == my_progress_callback_6);
	
	
	CU_ASSERT(s->progress->interval == 1); // Verify that the progress interval is set to 1

	ms_set_progress_interval(s, 60);
	CU_ASSERT(s->progress->interval == 60);

	// Reset the callback time
	progress_called = FALSE;
	s->progress->_last_update_ts = 0;
	ms_scan(s);
	CU_ASSERT(progress_called == FALSE);
	#ifdef WIN32
	Sleep(40); // wait 40ms
	#else
	sleep(1);
	#endif
	ms_scan(s);
	CU_ASSERT(progress_called == FALSE);

	// Reset the callback time
	s->progress->_last_update_ts = 0;
	ms_scan(s);
	CU_ASSERT(progress_called == FALSE);
	
	#ifdef WIN32
	Sleep(61); // wait 61ms
	#else
	sleep(1);
	#endif;
	
	ms_scan(s);
	CU_ASSERT(progress_called == TRUE);

	ms_destroy(s);

} /* test_ms_scan_6 */

static	void my_result_callback_1(MediaScan *s, MediaScanResult *result, void *userdata) {

}

static int error_called = FALSE;
static MediaScanError error;

static void my_error_callback_1(MediaScan *s, MediaScanError *error, void *userdata) { 
	error_called = TRUE;
	memcpy(&error, error, sizeof(MediaScanError));
} /* my_error_callback() */

///-------------------------------------------------------------------------------------------------
///  Test test_ms_file_scan_1'
///
/// @author Henry Bennett
/// @date 03/18/2011
///-------------------------------------------------------------------------------------------------

void test_ms_file_scan_1(void)	{
#ifdef WIN32
	char valid_file[MAX_PATH_STR_LEN] = "data\\video\\bars-mpeg4-aac.m4v";
	char invalid_file[MAX_PATH_STR_LEN] = "data\\video\\notafile.m4v";
#else
	char valid_file[MAX_PATH_STR_LEN] = "data/video/bars-mpeg4-aac.m4v";
	char invalid_file[MAX_PATH_STR_LEN] = "data/video/notafile.m4v";
#endif
	MediaScan *s = ms_create();
	
	CU_ASSERT_FATAL(s != NULL);

	// Check null mediascan oject
	ms_errno = 0;
	ms_scan_file(NULL, invalid_file, TYPE_VIDEO);
	CU_ASSERT(ms_errno == MSENO_NULLSCANOBJ);

	// Check for no callback set
	ms_errno = 0;
	ms_scan_file(s, invalid_file, TYPE_VIDEO);
	CU_ASSERT(ms_errno == MSENO_NORESULTCALLBACK);

	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback_1);
	CU_ASSERT(s->on_result == my_result_callback_1);

	// Now scan an invalid file, without an error handler
	ms_errno = 0;
	ms_scan_file(s, invalid_file, TYPE_VIDEO);
	CU_ASSERT(ms_errno == MSENO_NOERRORCALLBACK);

	// Set up an error callback
	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	// Now scan an invalid file, with an error handler
	ms_errno = 0;
	error_called = FALSE;
	ms_scan_file(s, invalid_file, TYPE_VIDEO);
	CU_ASSERT(ms_errno != MSENO_NOERRORCALLBACK);
	CU_ASSERT(error_called == TRUE);

	// Now scan a valid video file, with an error handler
	ms_errno = 0;
	error_called = FALSE;
	ms_scan_file(s, valid_file, TYPE_VIDEO);
	// Need more info to run this test

	ms_destroy(s);
} /* test_ms_file_scan_1 */


void test_ms_file_asf_audio(void)	{
#ifdef WIN32
	char asf_file[MAX_PATH_STR_LEN] = "data\\audio\\wmv92-with-audio.wmv";
#else
	char asf_file[MAX_PATH_STR_LEN] = "data/audio/wmv92-with-audio.wmv";
#endif
	MediaScan *s = ms_create();
	
	CU_ASSERT_FATAL(s != NULL);

	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback_1);
	CU_ASSERT(s->on_result == my_result_callback_1);

	// Set up an error callback
	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	// Now scan a valid video file, with an error handler
	ms_errno = 0;
	error_called = FALSE;
	ms_scan_file(s, asf_file, TYPE_VIDEO);
	// Need more info to run this test

	ms_destroy(s);
} /* test_ms_file_asf_audio */


///-------------------------------------------------------------------------------------------------
///  Test ms_set_async and ms_set_log_level
///
/// @author Henry Bennett
/// @date 03/18/2011
///-------------------------------------------------------------------------------------------------

void test_ms_misc_functions(void)	{
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);


	ms_destroy(s);
} /* test_ms_misc_functions() */


void test_ms_large_directory(void)	{
	char dir[MAX_PATH_STR_LEN] = "data";
	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	CU_ASSERT(s->npaths == 0);
	ms_add_path(s, dir);
	CU_ASSERT(s->npaths == 1);
	CU_ASSERT_STRING_EQUAL(s->paths[0], dir);


	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	ms_scan(s);

	ms_destroy(s);
} /* test_ms_misc_functions() */

#ifndef WIN32
void TouchFile(const char *filename) {
  struct timeval now;
  
  gettimeofday(&now, NULL);
  utimes(filename  , &now);
}
#endif

void test_ms_db(void)	{

#ifdef WIN32
	char valid_file[MAX_PATH_STR_LEN] = "data\\video\\bars-mpeg4-aac.m4v";
#else
	char valid_file[MAX_PATH_STR_LEN] = "data/video/bars-mpeg4-aac.m4v";
#endif

	MediaScan *s = ms_create();

	CU_ASSERT_FATAL(s != NULL);

	CU_ASSERT(s->npaths == 0);

	CU_ASSERT(s->on_result == NULL);
	ms_set_result_callback(s, my_result_callback);
	CU_ASSERT(s->on_result == my_result_callback);

	CU_ASSERT(s->on_error == NULL);
	ms_set_error_callback(s, my_error_callback); 
	CU_ASSERT(s->on_error == my_error_callback);

	ms_errno = 0;
	error_called = FALSE;

	// Okay scan a file
	result_called = FALSE;
	ms_scan_file(s, valid_file, TYPE_VIDEO);
	CU_ASSERT(result_called == TRUE);

	// now scan the same file again and make sure it is skipped
	result_called = FALSE;
	ms_scan_file(s, valid_file, TYPE_VIDEO);
	CU_ASSERT(result_called == FALSE);

	// now scan the same file again, this time with a different modification time and make sure it is scanned
	TouchFile(valid_file);
	result_called = FALSE;
	ms_scan_file(s, valid_file, TYPE_VIDEO);
	CU_ASSERT(result_called == TRUE);

	ms_destroy(s);
} /* test_ms_db() */


///-------------------------------------------------------------------------------------------------
///  ------------------------------------------------------------------------------------------
/// 	  The main() function for setting up and running the tests. Returns a CUE_SUCCESS on
/// 	  successful running, another CUnit error code on failure.
///
/// @author Henry Bennett
/// @date 03/16/2011
///
/// @return .
///-------------------------------------------------------------------------------------------------

int run_unit_tests()
{
   CU_pSuite pSuite = NULL;

   /* initialize the CUnit test registry */
   if (CUE_SUCCESS != CU_initialize_registry())
      return CU_get_error();

   /* add a suite to the registry */
   pSuite = CU_add_suite("File Scanning", NULL, NULL);
   if (NULL == pSuite) {
      CU_cleanup_registry();
      return CU_get_error();
   }

#ifdef SUPER
//		ms_set_log_level(DEBUG);
//     av_log_set_level(AV_LOG_DEBUG);
   /* add the tests to the ms_scan suite */
   if (
	   NULL == CU_add_test(pSuite, "Simple test of ms_scan()", test_ms_scan) ||
	   NULL == CU_add_test(pSuite, "Test ms_scan() with no paths", test_ms_scan_2) ||
	   NULL == CU_add_test(pSuite, "Test of ms_scan() with too many paths", test_ms_scan_3) ||
	   NULL == CU_add_test(pSuite, "Test of ms_scan() with a bad directory", test_ms_scan_4) ||
// TODO: The following test fails due to the scanner freezing up.
//	   NULL == CU_add_test(pSuite, "Test of ms_scan() with strange path slashes", test_ms_scan_5) ||
	   NULL == CU_add_test(pSuite, "Test of ms_scan()'s progress notifications", test_ms_scan_6) ||
	   NULL == CU_add_test(pSuite, "Test of ms_file_scan()", test_ms_file_scan_1) ||
//NULL == CU_add_test(pSuite, "Test of scanning LOTS of files", test_ms_large_directory) ||
	   NULL == CU_add_test(pSuite, "Test of misc functions", test_ms_misc_functions) ||
  	   NULL == CU_add_test(pSuite, "Simple test of ASF audio file", test_ms_file_asf_audio) ||
   	   NULL == CU_add_test(pSuite, "Test Berkeley database functionality", test_ms_db)
			 
	   )
   {
      CU_cleanup_registry();
      return CU_get_error();
   }
#endif
	 
//   setupbackground_tests();
   setupdefect_tests();
//   setupimage_tests();
//	 setup_thumbnail_tests();

   /* Run all tests using the CUnit Basic interface */
   CU_basic_set_mode(CU_BRM_VERBOSE);
   CU_basic_run_tests();
//	CU_automated_run_tests();

   CU_cleanup_registry();
   return CU_get_error();
} /* run_unit_tests() */


