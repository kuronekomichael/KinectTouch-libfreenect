//============================================================================
// Name        : KinectTouch.cpp
// Author      : github.com/robbeofficial
// Version     : 0.something
// Description : recognizes touch points on arbitrary surfaces using kinect
// 				 and maps them to TUIO cursors
// 				 (turns any surface into a touchpad)
//============================================================================

/*
 * 1. point your kinect from a higher place down to your table
 * 2. start the program (keep your hands off the table for the beginning)
 * 3. use your table as a giant touchpad
 */

#include <iostream>
#include <vector>
#include <map>
using namespace std;

// openCV
#include <opencv/highgui.h>
#include <opencv/cv.h>
using namespace cv;

#define FREENECT

#ifdef FREENECT
#include "libfreenect.h"
#include <pthread.h>
#else
// openNI
#include <XnOpenNI.h>
#include <XnCppWrapper.h>
using namespace xn;
#define CHECK_RC(rc, what)											\
	if (rc != XN_STATUS_OK)											\
	{																\
		printf("%s failed: %s\n", what, xnGetStatusString(rc));		\
		return rc;													\
	}
#endif

// TUIO
#include "TuioServer.h"
using namespace TUIO;

// TODO smoothing using kalman filter

//---------------------------------------------------------------------------
// Globals
//---------------------------------------------------------------------------

#ifdef FREENECT
int die = 0;
pthread_t freenect_thread;
freenect_context *f_ctx;
freenect_device *f_dev;
uchar depth_mid[640*480], depth_front[640*480];
int got_depth = 0;
pthread_mutex_t gl_backbuf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gl_frame_cond = PTHREAD_COND_INITIALIZER;

#else
// openNI
xn::Context xnContext;
xn::DepthGenerator xnDepthGenerator;
xn::ImageGenerator xnImgeGenertor;
#endif

bool mousePressed = false;

//---------------------------------------------------------------------------
// Functions
//---------------------------------------------------------------------------

#ifdef FREENECT
uchar tMax = 0, tMin = 99999999;
void depth_cb(freenect_device *dev, void *v_depth, uint32_t timestamp)
{
	int i;
	pthread_mutex_lock(&gl_backbuf_mutex);
	pthread_cond_init(&gl_frame_cond, NULL);
	uchar *depth = (uchar*)v_depth;

	for (i = 0; i < 640*480; i++) {
		depth_mid[i] = depth[i];
		if (tMax < depth[i]) {
			tMax = depth[i];
		}
		if (tMin > depth[i]) {
			tMin = depth[i];
		}
	}
	got_depth++;

	//printf("tMin = %d, tMax = %d", tMin, tMax);

	pthread_cond_signal(&gl_frame_cond);
	pthread_mutex_unlock(&gl_backbuf_mutex);
	pthread_cond_init(&gl_frame_cond, NULL);
}

void *freenect_threadfunc(void *arg)
{

	freenect_set_led(f_dev, LED_RED);
	freenect_set_depth_callback(f_dev, depth_cb);
	freenect_set_depth_mode(f_dev, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT));
//typedef enum {
//	FREENECT_RESOLUTION_LOW    = 0, /**< QVGA - 320x240 */
//	FREENECT_RESOLUTION_MEDIUM = 1, /**< VGA  - 640x480 */
//	FREENECT_RESOLUTION_HIGH   = 2, /**< SXGA - 1280x1024 */
//	FREENECT_RESOLUTION_DUMMY  = 2147483647, /**< Dummy value to force enum to be 32 bits wide */
//} freenect_resolution;
//typedef enum {
//	FREENECT_DEPTH_11BIT        = 0, /**< 11 bit depth information in one uint16_t/pixel */
//	FREENECT_DEPTH_10BIT        = 1, /**< 10 bit depth information in one uint16_t/pixel */
//	FREENECT_DEPTH_11BIT_PACKED = 2, /**< 11 bit packed depth information */
//	FREENECT_DEPTH_10BIT_PACKED = 3, /**< 10 bit packed depth information */
//	FREENECT_DEPTH_REGISTERED   = 4, /**< processed depth data in mm, aligned to 640x480 RGB */
//	FREENECT_DEPTH_MM           = 5, /**< depth to each pixel in mm, but left unaligned to RGB image */
//	FREENECT_DEPTH_DUMMY        = 2147483647, /**< Dummy value to force enum to be 32 bits wide */
//} freenect_depth_format;

	freenect_start_depth(f_dev);

	printf("\nstarting freenect_threadfunc\n");

	while (die == 0 && freenect_process_events(f_ctx) >= 0) {
		;;
	}

	printf("\nshutting down streams...\n");

	freenect_stop_depth(f_dev);

	freenect_close_device(f_dev);
	freenect_shutdown(f_ctx);

	printf("-- done!\n");
	return NULL;
}

int initKinnect() {

	pthread_mutex_init(&gl_backbuf_mutex, NULL);
	pthread_cond_init(&gl_frame_cond, NULL);

	// setup
	if (freenect_init(&f_ctx, NULL) < 0) {
	  printf("freenect_init() failed\n");
	  return 1;
	}

	freenect_set_log_level(f_ctx, FREENECT_LOG_DEBUG);
	freenect_select_subdevices(f_ctx, (freenect_device_flags)(FREENECT_DEVICE_CAMERA));

	int nr_devices = freenect_num_devices(f_ctx);
	printf ("Number of devices found: %d\n", nr_devices);

	if (nr_devices <= 0) {
	  printf("devices not found\n");
		freenect_shutdown(f_ctx);
		return -1;
	}

	if (freenect_open_device(f_ctx, &f_dev, 0) < 0) {
	  printf("Could not open device\n");
		freenect_shutdown(f_ctx);
		return -1;
	}

	// depth読み取り用のスレッド起動
	int res = pthread_create(&freenect_thread, NULL, freenect_threadfunc, NULL);
	if (res) {
		printf("pthread_create failed\n");
		freenect_close_device(f_dev);
		freenect_shutdown(f_ctx);
		return -1;
	}

	return 0;
}
void updateKinnect() {
	// depthが取れるまで待ち
	while (!got_depth) {
			pthread_cond_wait(&gl_frame_cond, &gl_backbuf_mutex);
	}
}
uchar* getKinnectDepthMap() {
	int i;
	pthread_mutex_lock(&gl_backbuf_mutex);
	got_depth = 0;
	for (i = 0; i < 640*480; i++) {
		depth_front[i] = depth_mid[i];
	}
	pthread_mutex_unlock(&gl_backbuf_mutex);

	printf("tMin = %d, tMax = %d", tMin, tMax);

	return depth_front;
}
#else
int initKinnect() {
	const XnChar* fname = "niConfig.xml";
	XnStatus nRetVal = XN_STATUS_OK;

	// initialize context
	nRetVal = xnContext.InitFromXmlFile(fname);
	CHECK_RC(nRetVal, "InitFromXmlFile");

	// initialize depth generator
	nRetVal = xnContext.FindExistingNode(XN_NODE_TYPE_DEPTH, xnDepthGenerator);
	CHECK_RC(nRetVal, "FindExistingNode(XN_NODE_TYPE_DEPTH)");

	// initialize image generator
	nRetVal = xnContext.FindExistingNode(XN_NODE_TYPE_IMAGE, xnImgeGenertor);
	CHECK_RC(nRetVal, "FindExistingNode(XN_NODE_TYPE_IMAGE)");

	return 0;
}
void updateKinnect() {
	xnContext.WaitAndUpdateAll();
}
uchar* getKinnectDepthMap() {
	return (uchar*) xnDepthGenerator.GetDepthMap();
}
#endif

void average(vector<Mat1s>& frames, Mat1s& mean) {
	Mat1d acc(mean.size());
	Mat1d frame(mean.size());

	for (unsigned int i=0; i<frames.size(); i++) {
		frames[i].convertTo(frame, CV_64FC1);
		acc = acc + frame;
	}

	acc = acc / frames.size();

	acc.convertTo(mean, CV_16SC1);
}

int main() {

	const unsigned int nBackgroundTrain = 30;	// サンプリング回数
	const unsigned short touchDepthMin = 10;	// タッチ判定の最小値
	const unsigned short touchDepthMax = 20;	// タッチ判定の最大値
	const unsigned int touchMinArea = 50;		// このエリアよりも輪郭が大きいなら、タッチ箇所とみなす

	const bool localClientMode = false; 		// connect to a local client

	const double debugFrameMaxDepth = 4000;		// maximal distance (in millimeters) for 8 bit debug depth frame quantization. 4000mm === 4m
	const char* windowName = "Debug";			// ウィンドウ名
	const Scalar debugColor0(0, 0, 128);		// タッチ近似領域の色：Scalr(Blue, Green, Red) === (0x800000) === red
	const Scalar debugColor1(255, 0, 0);		// ROIを囲む枠線の色
	const Scalar debugColor2(255, 255, 255);	// タッチの色

	int xMin = 110;
	int xMax = 560;
	int yMin = 120;
	int yMax = 320;

	Mat1s depth(480, 640);		// 16 bit depth (in millimeters) <Mat1s === short>
	Mat1b depth8(480, 640);		// 8 bit depth <Mat1b === uchar>
	//Mat3b rgb(480, 640);		// 8 bit depth <Mat3b === Vec3b<r,g,b>>

	Mat3b debug(480, 640);		// debug visualization

	Mat1s foreground(640, 480);
	Mat1b foreground8(640, 480);

	Mat1b touch(640, 480); // touch mask

	Mat1s background(480, 640);
	vector<Mat1s> buffer(nBackgroundTrain);

	if (initKinnect() != 0) {
		printf("initKinnect Error\n");
		return -1;
	}

	// TUIO server object
	TuioServer* tuio;
	if (localClientMode) {
		tuio = new TuioServer();
	} else {
		printf("connect TuioServer 150.43.77.24:3333\n");
		tuio = new TuioServer("150.43.77.24", 3333, false);
	}
	TuioTime time;

	// create some sliders
	namedWindow(windowName);
	createTrackbar("xMin", windowName, &xMin, 640);
	createTrackbar("xMax", windowName, &xMax, 640);
	createTrackbar("yMin", windowName, &yMin, 480);
	createTrackbar("yMax", windowName, &yMax, 480);

	// create background model (average depth)
	for (unsigned int i=0; i<nBackgroundTrain; i++) {
		updateKinnect();
		depth.data = getKinnectDepthMap();
		buffer[i] = depth;
	}
	average(buffer, background);

	while ( waitKey(1) != 27 ) {
		// データ読み取り
		updateKinnect();

		// update 16 bit depth matrix
		depth.data = getKinnectDepthMap();
		//xnImgeGenertor.GetGrayscale8ImageMap()

		// update rgb image
		//rgb.data = (uchar*) xnImgeGenertor.GetRGB24ImageMap(); // segmentation fault here
		//cvtColor(rgb, rgb, CV_RGB2BGR);

		// extract foreground by simple subtraction of very basic background model
		foreground = background - depth;

		// タッチマスク
		// find touch mask by thresholding (points that are close to background = touch points)
		touch = (foreground > touchDepthMin) & (foreground < touchDepthMax);

		// extract ROI
		Rect roi(xMin, yMin, xMax - xMin, yMax - yMin);
		Mat touchRoi = touch(roi);

		// タッチ位置を探す
		vector< vector<Point2i> > contours;
		vector<Point2f> touchPoints;//タッチ位置
		findContours(touchRoi, contours, CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, Point2i(xMin, yMin));//輪郭を探しだす by OpenCV
		for (unsigned int i=0; i<contours.size(); i++) {
			Mat contourMat(contours[i]);
			// find touch points by area thresholding
			if ( contourArea(contourMat) > touchMinArea ) {	// 小さすぎる点はタッチと見なさない
				Scalar center = mean(contourMat);
				Point2i touchPoint(center[0], center[1]);
				touchPoints.push_back(touchPoint);
			}
		}

		// send TUIO cursors
		time = TuioTime::getSessionTime();
		tuio->initFrame(time);

		for (unsigned int i=0; i<touchPoints.size(); i++) { // touch points
				float cursorX = (touchPoints[i].x - xMin) / (xMax - xMin);
				float cursorY = 1 - (touchPoints[i].y - yMin)/(yMax - yMin);
				TuioCursor* cursor = tuio->getClosestTuioCursor(cursorX,cursorY);
				// TODO improve tracking (don't move cursors away, that might be closer to another touch point)
				if (cursor == NULL || cursor->getTuioTime() == time) {
					tuio->addTuioCursor(cursorX,　cursorY);
					printf("addTuioCursor TuioServer(%f, %f)\n", cursorX,　cursorY);
				} else {
					tuio->updateTuioCursor(cursor, cursorX, cursorY);
					printf("updateTuioCursor TuioServer(%f, %f)\n", cursorX,　cursorY);
				}
		}

		tuio->stopUntouchedMovingCursors();
		tuio->removeUntouchedStoppedCursors();
		tuio->commitFrame();

		//--------------------
		// draw debug frame
		//--------------------
/*
	Mat1s depth(480, 640);		// 16 bit depth (in millimeters) <Mat1s === short>
	Mat1b depth8(480, 640);		// 8 bit depth <Mat1b === uchar>
	Mat3b debug(480, 640);		// debug visualization
*/
		// render depth to debug frame
		// 行列をスケーリングして別のデータ型に変換 cvConvertScale を参照してください．
		depth.convertTo(depth8, CV_8U/* 0〜255 */, 255 / debugFrameMaxDepth);
		cvtColor(/* 入力画像 */depth8, /* 出力画像 */debug, /* 変換方法 */CV_GRAY2BGR);
		debug.setTo(debugColor0, touch);  // touch mask
		rectangle(debug, roi, debugColor1, 2); // surface boundaries
		for (unsigned int i = 0; i < touchPoints.size(); i++) { // touch points
			circle(debug, touchPoints[i], 5, debugColor2, CV_FILLED);
		}

		// render debug frame (with sliders)
		imshow(windowName, debug);
		//imshow("image", rgb);
	}
	die++;
	sleep(1);

	printf("main thread finished.\n");

	return 0;
}
