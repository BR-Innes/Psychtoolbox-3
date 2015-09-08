/*
 * PsychToolbox/Source/Common/PsychOculusVRCore/PsychOculusVR.c
 *
 * PROJECTS: PsychOculusVRCore only.
 *
 * AUTHORS:
 *
 * mario.kleiner.de@gmail.com   mk
 *
 * PLATFORMS:   All.
 *
 * HISTORY:
 *
 * 1.09.2015   mk      Created.
 *
 * DESCRIPTION:
 *
 * A Psychtoolbox driver for the Oculus VR virtual reality
 * head sets. The initial version will support the Rift DK2,
 * and possibly the old Rift DK1, although that hasn't been
 * tested.
 *
 */

#include "PsychOculusVR.h"

#include <math.h>

// Includes from Oculus SDK 0.5:
#include "OVR_CAPI.h"

// Number of maximum simultaneously open kinect devices:
#define MAX_PSYCH_OCULUS_DEVS 10
#define MAX_SYNOPSIS_STRINGS 40

//declare variables local to this file.
static const char *synopsisSYNOPSIS[MAX_SYNOPSIS_STRINGS];

// Our device record:
typedef struct PsychOculusDevice {
    ovrHmd hmd;
    psych_bool isTracking;
    ovrSizei texSize;
    ovrEyeRenderDesc eyeRenderDesc[2];
    ovrDistortionMesh eyeDistortionMesh[2];
    ovrVector2f UVScaleOffset[2][2];
    ovrMatrix4f timeWarpMatrices[2];
    ovrPosef headPose[2];
} PsychOculusDevice;

PsychOculusDevice oculusdevices[MAX_PSYCH_OCULUS_DEVS];
static int available_devices = 0;
static unsigned int devicecount = 0;
static unsigned int verbosity = 3;
static psych_bool initialized = FALSE;

void InitializeSynopsis(void)
{
    int i = 0;
    const char **synopsis = synopsisSYNOPSIS;

    synopsis[i++] = "PsychOculusVRCore - A Psychtoolbox driver for Oculus VR hardware.\n";
    synopsis[i++] = "This driver allows to control Oculus Rift DK1/DK2 and future Oculus devices.\n";
    synopsis[i++] = "The PsychOculusVRCore driver is licensed to you under the terms of the MIT license.";
    synopsis[i++] = "See 'help License.txt' in the Psychtoolbox root folder for more details.\n";
    synopsis[i++] = "\n";
    synopsis[i++] = "Usage:";
    synopsis[i++] = "\n";
    synopsis[i++] = "numHMDs = PsychOculusVRCore('GetCount');";
    synopsis[i++] = "oculusPtr = PsychOculusVRCore('Open' [, deviceIndex=0]);";
    synopsis[i++] = "PsychOculusVRCore('Close' [, oculusPtr]);";
    synopsis[i++] = "PsychOculusVRCore('Start', oculusPtr);";
    synopsis[i++] = "PsychOculusVRCore('Stop', oculusPtr);";
    synopsis[i++] = "state = PsychOculusVRCore('GetTrackingState', oculusPtr [, predictionTime=0]);";
    synopsis[i++] = "[width, height] = PsychOculusVRCore('GetFovTextureSize', oculusPtr, eye [, fov=[45,45,45,45]][, pixelsPerDisplay=1]);";
    synopsis[i++] = NULL;  //this tells PsychOculusVRDisplaySynopsis where to stop

    if (i > MAX_SYNOPSIS_STRINGS) {
        PrintfExit("%s: increase dimension of synopsis[] from %ld to at least %ld and recompile.", __FILE__, (long) MAX_SYNOPSIS_STRINGS, (long) i);
    }
}

PsychError PsychOculusVRDisplaySynopsis(void)
{
    int i;

    for (i = 0; synopsisSYNOPSIS[i] != NULL; i++)
        printf("%s\n",synopsisSYNOPSIS[i]);

    return(PsychError_none);
}

static inline double deg2rad(double deg)
{
    return deg / 360.0 * 2 * M_PI;
}

PsychOculusDevice* PsychGetOculus(int handle, psych_bool dontfail)
{
    if (handle < 1 || handle > MAX_PSYCH_OCULUS_DEVS || oculusdevices[handle-1].hmd == NULL) {
        if (!dontfail) {
            printf("PTB-ERROR: Invalid Oculus device handle %i passed. No such device open.\n", handle);
            PsychErrorExitMsg(PsychError_user, "Invalid Oculus handle.");
        }

        return(NULL);
    }

    return(&(oculusdevices[handle-1]));
}

void PsychOculusVRCheckInit(void)
{
    // Already initialized? No op then.
    if (initialized) return;

    // Initialize Oculus VR runtime with default parameters:
    if (ovr_Initialize(NULL)) {
        if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Oculus VR runtime version '%s' initialized.\n", ovr_GetVersionString());

        // Get count of available devices:
        available_devices = ovrHmd_Detect();
        if (available_devices < 0) {
            available_devices = 0;
            if (verbosity >= 2) printf("PsychOculusVRCore-WARNING: Could not connect to Oculus VR server process yet. Did you forget to start it?\n");
        }

        if (verbosity >= 3) printf("PsychOculusVRCore-INFO: At startup there are %i Oculus HMDs available.\n", available_devices);
        initialized = TRUE;
    }
    else {
        PsychErrorExitMsg(PsychError_system, "PsychOculusVRCore-ERROR: Initialization of VR runtime failed. Driver disabled!");
    }
}

void PsychOculusStop(int handle)
{
    PsychOculusDevice* oculus;
    oculus = PsychGetOculus(handle, TRUE);
    if (NULL == oculus || !oculus->isTracking) return;

    // Request stop of tracking:
    if (!ovrHmd_ConfigureTracking(oculus->hmd, 0, 0)) {
        if (verbosity >= 0) printf("PsychOculusVRCore-ERROR: Failed to stop tracking on device with handle %i [%s].\n", handle, ovrHmd_GetLastError(oculus->hmd));
        PsychErrorExitMsg(PsychError_system, "Stop of Oculus HMD tracking failed for reason given above.");
    }
    else if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Tracking stopped on device with handle %i.\n", handle);

    oculus->isTracking = FALSE;

    return;
}

void PsychOculusClose(int handle)
{
    int i;
    PsychOculusDevice* oculus;
    oculus = PsychGetOculus(handle, TRUE);
    if (NULL == oculus) return;

    // Stop device:
    PsychOculusStop(handle);

    // Release distortion meshes, if any:
    if (oculus->eyeDistortionMesh[0].pVertexData) {
        ovrHmd_DestroyDistortionMesh(&(oculus->eyeDistortionMesh[0]));
    }

    if (oculus->eyeDistortionMesh[1].pVertexData) {
        ovrHmd_DestroyDistortionMesh(&(oculus->eyeDistortionMesh[1]));
    }

    // Close the HMD:
    ovrHmd_Destroy(oculus->hmd);
    oculus->hmd = NULL;
    if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Closed Oculus HMD with handle %i.\n", handle);

    // Done with this device:
    devicecount--;
}

void PsychOculusVRInit(void) {
    int handle;

    for (handle = 0 ; handle < MAX_PSYCH_OCULUS_DEVS; handle++)
        oculusdevices[handle].hmd = NULL;

    available_devices = 0;
    devicecount = 0;
    initialized = FALSE;
}

PsychError PsychOculusVRShutDown(void) {
    int handle;

    if (initialized) {
        for (handle = 0 ; handle < MAX_PSYCH_OCULUS_DEVS; handle++)
            PsychOculusClose(handle);

        // Shutdown runtime:
        ovr_Shutdown();

        if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Oculus VR runtime shutdown complete. Bye!\n");
    }
    initialized = FALSE;

    return(PsychError_none);
}

PsychError PSYCHOCULUSVRGetCount(void)
{
    static char useString[] = "numHMDs = PsychOculusVR('GetCount');";
    static char synopsisString[] = "Returns count of currently connected HMDs.\n\n";
    static char seeAlsoString[] = "Open";

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if( PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    // Check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(1));
    PsychErrorExit(PsychCapNumInputArgs(0));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    available_devices = ovrHmd_Detect();
    if (available_devices < 0) {
        available_devices = 0;
        if (verbosity >= 2) printf("PsychOculusVRCore-WARNING: Could not connect to Oculus VR server process yet. Did you forget to start it?\n");
    }

    PsychCopyOutDoubleArg(1, FALSE, available_devices);

    return(PsychError_none);
}

PsychError PSYCHOCULUSVROpen(void)
{
    static char useString[] = "oculusPtr = PsychOculusVR('Open' [, deviceIndex=0]);";
    //                                                          1
    static char synopsisString[] =
        "Open connection to Oculus VR HMD, return a 'oculusPtr' handle to it.\n\n"
        "The call tries to open the HMD with index 'deviceIndex', or the "
        "first detected HMD, if 'deviceIndex' is omitted. You can pass in a 'deviceIndex' "
        "of -1 to open an emulated HMD. It doesn't provide any sensor input, but allows "
        "some basic testing and debugging of VR software nonetheless.\n"
        "The returned handle can be passed to the other subfunctions to operate the device.\n";

    static char seeAlsoString[] = "GetCount Close";

    PsychOculusDevice* oculus;
    int i, j;
    int deviceIndex = 0;
    int handle = 0;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if( PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    // Check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(1));
    PsychErrorExit(PsychCapNumInputArgs(1));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    // Find a free device slot:
    for (handle = 0; (handle < MAX_PSYCH_OCULUS_DEVS) && oculusdevices[handle].hmd; handle++);
    if ((handle >= MAX_PSYCH_OCULUS_DEVS) || oculusdevices[handle].hmd) PsychErrorExitMsg(PsychError_internal, "Maximum number of simultaneously open Oculus VR devices reached.");

    // Get optional Oculus device index:
    PsychCopyInIntegerArg(1, FALSE, &deviceIndex);

    // Don't support anything than a single "default" OculusVR Rift yet - A limitation of the current SDK:
    if (deviceIndex < -1) PsychErrorExitMsg(PsychError_user, "Invalid 'deviceIndex' provided. Must be greater or equal to zero!");

    available_devices = ovrHmd_Detect();
    if (available_devices < 0) {
        available_devices = 0;
        if (verbosity >= 2) printf("PsychOculusVRCore-WARNING: Could not connect to Oculus VR server process yet. Did you forget to start it?\n");
    }

    if ((deviceIndex >= 0) && (deviceIndex >= available_devices)) {
        if (verbosity >= 0) printf("PsychOculusVRCore-ERROR: Invalid deviceIndex %i >= number of available HMDs %i.\n", deviceIndex, available_devices);
        PsychErrorExitMsg(PsychError_user, "Invalid 'deviceIndex' provided. Not enough HMDs available!");
    }

    // Zero init device structure:
    memset(&oculusdevices[handle], 0, sizeof(PsychOculusDevice));

    oculus = &oculusdevices[handle];

    // Try to open real or emulated HMD with deviceIndex:
    if (deviceIndex >= 0) {
        // The real thing:
        oculusdevices[handle].hmd = ovrHmd_Create(deviceIndex);
        if (NULL == oculusdevices[handle].hmd) {
            if (verbosity >= 0) {
                printf("PsychOculusVRCore-ERROR: Failed to connect to Oculus Rift with deviceIndex %i. This could mean that the device\n", deviceIndex);
                printf("PsychOculusVRCore-ERROR: is already in use by another application or driver.\n");
            }
            PsychErrorExitMsg(PsychError_user, "Could not connect to Rift device with given 'deviceIndex'! [ovrHmd_Create() failed]");
        }
        else if (verbosity >= 3) {
            printf("PsychOculusVRCore-INFO: Opened Oculus Rift with deviceIndex %i as handle %i.\n", deviceIndex, handle + 1);
        }
    }
    else {
        // Emulated: Simulate a Rift DK2.
        oculusdevices[handle].hmd = ovrHmd_CreateDebug(ovrHmd_DK2);
        if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Opened an emulated Oculus Rift DK2 as handle %i.\n", handle + 1);
    }

    // Stats for nerds:
    if (verbosity >= 3) {
        printf("PsychOculusVRCore-INFO: Product: %s - Manufacturer: %s - SerialNo: %s [VID: 0x%x PID: 0x%x]\n",
               oculus->hmd->ProductName, oculus->hmd->Manufacturer, oculus->hmd->SerialNumber, (int) oculus->hmd->VendorId, (int) oculus->hmd->ProductId);
        printf("PsychOculusVRCore-INFO: Firmware version: %i.%i\n", (int) oculus->hmd->FirmwareMajor, (int) oculus->hmd->FirmwareMinor);
        printf("PsychOculusVRCore-INFO: CameraFrustumHFovInRadians: %f - CameraFrustumVFovInRadians: %f\n", oculus->hmd->CameraFrustumHFovInRadians, oculus->hmd->CameraFrustumVFovInRadians);
        printf("PsychOculusVRCore-INFO: CameraFrustumNearZInMeters: %f - CameraFrustumFarZInMeters:  %f\n", oculus->hmd->CameraFrustumNearZInMeters, oculus->hmd->CameraFrustumFarZInMeters);
        printf("PsychOculusVRCore-INFO: Panel size in pixels w x h = %i x %i [WindowPos %i x %i]\n", oculus->hmd->Resolution.w, oculus->hmd->Resolution.h, oculus->hmd->WindowsPos.x, oculus->hmd->WindowsPos.y);
        printf("PsychOculusVRCore-INFO: DisplayDeviceName: %s\n", oculus->hmd->DisplayDeviceName);
        printf("PsychOculusVRCore-INFO: ----------------------------------------------------------------------------------\n");
    }

    // Increment count of open devices:
    devicecount++;

    // Return device handle: We use 1-based handle indexing to make life easier for Octave/Matlab:
    PsychCopyOutDoubleArg(1, FALSE, handle + 1);

    return(PsychError_none);
}

PsychError PSYCHOCULUSVRClose(void)
{
    static char useString[] = "PsychOculusVR('Close' [, oculusPtr]);";
    //                                                  1
    static char synopsisString[] =
        "Close connection to Oculus Rift device 'oculusPtr'. Do nothing if no such device is open.\n"
        "If the optional 'oculusPtr' is omitted, then close all open devices and shutdown the driver, "
        "ie. perform the same cleanup as if 'clear PsychOculusVR' would be executed.\n";
    static char seeAlsoString[] = "Open";

    int handle = -1;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if(PsychIsGiveHelp()){PsychGiveHelp();return(PsychError_none);};

    //check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(0));
    PsychErrorExit(PsychCapNumInputArgs(1));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    // Get optional device handle:
    PsychCopyInIntegerArg(1, FALSE, &handle);

    if (handle >= 1) {
        // Close device:
        PsychOculusClose(handle);
    }
    else {
        // No handle provided: Close all devices, shutdown driver.
        PsychOculusVRShutDown();
    }

    return(PsychError_none);
}

PsychError PSYCHOCULUSVRStart(void)
{
    static char useString[] = "PsychOculusVR('Start', oculusPtr);";
    //                                                1
    static char synopsisString[] =
        "Start head orientation and position tracking operation on Oculus device 'oculusPtr'.\n\n";
    static char seeAlsoString[] = "Stop";

    int handle;
    PsychOculusDevice *oculus;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if (PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    // Check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(0));
    PsychErrorExit(PsychCapNumInputArgs(1));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    // Get device handle:
    PsychCopyInIntegerArg(1, TRUE, &handle);
    oculus = PsychGetOculus(handle, FALSE);

    if (oculus->isTracking) {
        if (verbosity >= 0) printf("PsychOculusVRCore-ERROR: Tried to start tracking on device %i, but tracking is already started.\n", handle);
        PsychErrorExitMsg(PsychError_user, "Tried to start tracking on HMD, but tracking already active.");
    }

    // Request start of tracking for retrieval of head orientation and position, with drift correction, e.g., via magnetometer.
    // Do not fail if retrieval of any of this information isn't supported by the given hardware, ie., the required set of caps is empty == 0.
    // Rift DK1 only had orientation tracking, with magnetometer based drift correction. Rift DK2 also has vision based position tracking and
    // drift correction. The software emulated Rift has none of these and just returns a "static" head. This will start tracking:
    if (!ovrHmd_ConfigureTracking(oculus->hmd, ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position, 0)) {
        if (verbosity >= 0) printf("PsychOculusVRCore-ERROR: Failed to start tracking on device with handle %i [%s].\n", handle, ovrHmd_GetLastError(oculus->hmd));
        PsychErrorExitMsg(PsychError_system, "Start of Oculus HMD tracking failed for reason given above.");
    }
    else if (verbosity >= 3) printf("PsychOculusVRCore-INFO: Tracking started on device with handle %i.\n", handle);

    oculus->isTracking = TRUE;

    // Tracking is running.
    return(PsychError_none);
}

PsychError PSYCHOCULUSVRStop(void)
{
    static char useString[] = "PsychOculusVR('Stop', oculusPtr);";
    static char synopsisString[] =
        "Stop head tracking operation on Oculus device 'oculusPtr'.\n\n";
    static char seeAlsoString[] = "Start";

    int handle;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if (PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    // Check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(0));
    PsychErrorExit(PsychCapNumInputArgs(1));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    PsychCopyInIntegerArg(1, TRUE, &handle);

    // Stop device:
    PsychOculusStop(handle);

    return(PsychError_none);
}

PsychError PSYCHOCULUSVRGetTrackingState(void)
{
    static char useString[] = "state = PsychOculusVR('GetTrackingState', oculusPtr [, predictionTime=0]);";
    static char synopsisString[] =
        "Return current state of head position and orientation tracking for Oculus device 'oculusPtr'.\n"
        "Head position and orientation is predicted for target time 'predictionTime' in seconds if provided, "
        "based on the latest measurements from the tracking hardware. If 'predictionTime' is omitted or set "
        "to zero, then no prediction is performed and the current state based on latest measurements is returned.\n\n"
        "'state' is a row vector with the following values reported at given index:\n"
        "1 = Time in seconds of predicted tracking state.\n"
        "[2,3,4] = Head position [x,y,z] in meters.\n"
        "[5,6,7,8] = Head orientation [x,y,z,w] as quaternion.\n"
        "[9,10,11] = Linear velocity [vx,vy,vz] in meters/sec.\n"
        "[12,13,14] = Angular velocity [rx,ry,rz] in radians/sec\n";
        "[15,16,17] = Linear acceleration [ax,ay,az] in meters/sec^2.\n"
        "[18,19,20] = Angular acceleration [rax,ray,raz] in radians/sec^2\n";

    static char seeAlsoString[] = "Start Stop";

    int handle;
    double predictionTime = 0.0;
    PsychOculusDevice *oculus;
    ovrTrackingState state;
    double* v;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if (PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    //check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(1));
    PsychErrorExit(PsychCapNumInputArgs(2));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    PsychCopyInIntegerArg(1, TRUE, &handle);
    oculus = PsychGetOculus(handle, FALSE);

    PsychCopyInDoubleArg(2, FALSE, &predictionTime);

    // Get current tracking status info at time 0 ie., current measurements:
    state = ovrHmd_GetTrackingState(oculus->hmd, predictionTime);

    // Print out tracking status:
    if (verbosity >= 4) {
        printf("PsychOculusVRCore-INFO: Tracking state predicted for device %i at time %f.\n", handle, predictionTime);
        printf("PsychOculusVRCore-INFO: LastCameraFrameCounter = %i : Time %f : Status %i\n", state.LastCameraFrameCounter, state.HeadPose.TimeInSeconds, state.StatusFlags);
        printf("PsychOculusVRCore-INFO: HeadPose: Position    [x,y,z]   = [%f, %f, %f]\n", state.HeadPose.ThePose.Position.x, state.HeadPose.ThePose.Position.y, state.HeadPose.ThePose.Position.z);
        printf("PsychOculusVRCore-INFO: HeadPose: Orientation [x,y,z,w] = [%f, %f, %f, %f]\n", state.HeadPose.ThePose.Orientation.x, state.HeadPose.ThePose.Orientation.y, state.HeadPose.ThePose.Orientation.z, state.HeadPose.ThePose.Orientation.w);

        /// Current pose of the external camera (if present).
        /// This pose includes camera tilt (roll and pitch). For a leveled coordinate
        /// system use LeveledCameraPose.
        //ovrPosef       CameraPose;

        /// Camera frame aligned with gravity.
        /// This value includes position and yaw of the camera, but not roll and pitch.
        /// It can be used as a reference point to render real-world objects in the correct location.
        //ovrPosef       LeveledCameraPose;

        /// The most recent sensor data received from the HMD.
        //ovrSensorData  RawSensorData;

        /// Tracking status described by ovrStatusBits.
        //unsigned int   StatusFlags;

        /// Tag the vision processing results to a certain frame counter number.
        //uint32_t LastCameraFrameCounter;

    }

    PsychAllocOutDoubleMatArg(1, FALSE, 1, 20, 1, &v);
    v[0] = state.HeadPose.TimeInSeconds;

    v[1] = state.HeadPose.ThePose.Position.x;
    v[2] = state.HeadPose.ThePose.Position.y;
    v[3] = state.HeadPose.ThePose.Position.z;

    v[4] = state.HeadPose.ThePose.Orientation.x;
    v[5] = state.HeadPose.ThePose.Orientation.y;
    v[6] = state.HeadPose.ThePose.Orientation.z;
    v[7] = state.HeadPose.ThePose.Orientation.w;

    v[8]  = state.HeadPose.LinearVelocity.x;
    v[9]  = state.HeadPose.LinearVelocity.y;
    v[10] = state.HeadPose.LinearVelocity.z;

    v[11] = state.HeadPose.AngularVelocity.x;
    v[12] = state.HeadPose.AngularVelocity.y;
    v[13] = state.HeadPose.AngularVelocity.z;

    v[14] = state.HeadPose.LinearAcceleration.x;
    v[15] = state.HeadPose.LinearAcceleration.y;
    v[16] = state.HeadPose.LinearAcceleration.z;

    v[17] = state.HeadPose.AngularAcceleration.x;
    v[18] = state.HeadPose.AngularAcceleration.y;
    v[19] = state.HeadPose.AngularAcceleration.z;

    return(PsychError_none);
}

PsychError PSYCHOCULUSVRGetFovTextureSize(void)
{
    static char useString[] = "[width, height, viewPx, viewPy, viewPw, viewPh, pptax, pptay, hmdShiftx, hmdShifty, hmdShiftz, meshVertices, meshIndices, uvScaleX, uvScaleY, uvOffsetX, uvOffsetY, eyeRotStartMatrix, eyeRotEndMatrix] = PsychOculusVR('GetFovTextureSize', oculusPtr, eye [, fov=[45,45,45,45]][, pixelsPerDisplay=1]);";
    //                          1      2       3       4       5       6       7      8      9          10         11         12            13           14        15        16         17         18                 19                                                    1          2      3                    4
    static char synopsisString[] =
    "Return recommended size of renderbuffers for Oculus device 'oculusPtr'.\n"
    "'eye' which eye to provide the size for: 0 = Left, 1 = Right.\n"
    "'fov' Optional field of view in degrees, from line of sight: [leftdeg, rightdeg, updeg, downdeg]. "
    "Defaults to +/- 45 degrees in all directions if omitted.\n"
    "'pixelsPerDisplay' Ratio of the number of render target pixels to display pixels at the center "
    "of distortion. Defaults to 1.0 if omitted. Lower values can improve performance, higher values "
    "give improved quality.\n"
    "\n"
    "Return values are 'width' for minimum recommended width of framebuffer in pixels and "
    "'height' for minimum recommended height of framebuffer in pixels.\n";
    static char seeAlsoString[] = "";

    int handle, eyeIndex;
    PsychOculusDevice *oculus;
    int n, m, p, i;
    double *fov;
    ovrFovPort ofov;
    double pixelsPerDisplay;
    unsigned int distortionCaps;
    ovrDistortionVertex* pVertexData;
    unsigned short* pIndexData;
    double *outVertexMesh, *outIndexMesh;
    double *startMatrix, *endMatrix;
    float *mv;

    // All sub functions should have these two lines
    PsychPushHelp(useString, synopsisString,seeAlsoString);
    if (PsychIsGiveHelp()) {PsychGiveHelp(); return(PsychError_none);};

    //check to see if the user supplied superfluous arguments
    PsychErrorExit(PsychCapNumOutputArgs(19));
    PsychErrorExit(PsychCapNumInputArgs(4));
    PsychErrorExit(PsychRequireNumInputArgs(2));

    // Make sure driver is initialized:
    PsychOculusVRCheckInit();

    // Get device handle:
    PsychCopyInIntegerArg(1, TRUE, &handle);
    oculus = PsychGetOculus(handle, FALSE);

    // Get eye index - left = 0, right = 1:
    PsychCopyInIntegerArg(2, TRUE, &eyeIndex);
    if (eyeIndex < 0 || eyeIndex > 1) PsychErrorExitMsg(PsychError_user, "Invalid 'eye' specified. Must be 0 or 1 for left- or right eye.");

    // Get optional field of view in degrees in left,right,up,down direction from line of sight:
    if (PsychAllocInDoubleMatArg(3, FALSE, &n, &m, &p, &fov)) {
        // Validate and assign:
        if (n * m * p != 4) PsychErrorExitMsg(PsychError_user, "Invalid 'fov' specified. Must be a 4-component vector of form [leftdeg, rightdeg, updeg, downdeg].");
        ofov.LeftTan  = tan(deg2rad(fov[0]));
        ofov.RightTan = tan(deg2rad(fov[1]));
        ofov.UpTan = tan(deg2rad(fov[2]));
        ofov.DownTan = tan(deg2rad(fov[3]));
    }
    else {
        // None specified: Default to +/- 45 degrees in all directions:
        //ofov.LeftTan  = tan(deg2rad(45.0));
        //ofov.RightTan = tan(deg2rad(45.0));
        //ofov.UpTan = tan(deg2rad(45.0));
        //ofov.DownTan = tan(deg2rad(45.0));
        ofov = oculus->hmd->DefaultEyeFov[eyeIndex];
    }

    // Get optional pixelsPerDisplay parameter:
    pixelsPerDisplay = 1.0;
    PsychCopyInDoubleArg(4, FALSE, &pixelsPerDisplay);
    if (pixelsPerDisplay <= 0.0) PsychErrorExitMsg(PsychError_user, "Invalid 'pixelsPerDisplay' specified. Must be greater than zero.");

    // Ask the api for optimal texture size, aka the size of the client draw buffer:
    oculus->texSize = ovrHmd_GetFovTextureSize(oculus->hmd, (ovrEyeType) eyeIndex, ofov, (float) pixelsPerDisplay);
oculus->texSize.w = 1680;
oculus->texSize.h = 1050;

oculus->texSize.w = 1080;
oculus->texSize.h = 1920;

    // Return recommended width and height of drawBuffer:
    PsychCopyOutDoubleArg(1, FALSE, oculus->texSize.w);
    PsychCopyOutDoubleArg(2, FALSE, oculus->texSize.h);

    // Get eye render description for this eye:
    oculus->eyeRenderDesc[eyeIndex] = ovrHmd_GetRenderDesc(oculus->hmd, (ovrEyeType) eyeIndex, ofov);

    if (verbosity > 3) {
        printf("PsychOculusVRCore-INFO: For HMD %i, eye %i - RenderDescription:\n", handle, eyeIndex);
        printf("PsychOculusVRCore-INFO: FoV: %f %f %f %f - %f %f %f %f\n", ofov.LeftTan, ofov.RightTan, ofov.UpTan, ofov.DownTan, oculus->eyeRenderDesc[eyeIndex].Fov.LeftTan, oculus->eyeRenderDesc[eyeIndex].Fov.RightTan, oculus->eyeRenderDesc[eyeIndex].Fov.UpTan, oculus->eyeRenderDesc[eyeIndex].Fov.DownTan);
        printf("PsychOculusVRCore-INFO: DistortedViewport: [x,y,w,h] = [%i, %i, %i, %i]\n", oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.x, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.y, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.w, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.h);
        printf("PsychOculusVRCore-INFO: PixelsPerTanAngleAtCenter: %f x %f\n", oculus->eyeRenderDesc[eyeIndex].PixelsPerTanAngleAtCenter.x, oculus->eyeRenderDesc[eyeIndex].PixelsPerTanAngleAtCenter.y);
        printf("PsychOculusVRCore-INFO: HmdToEyeViewOffset: [x,y,z] = [%f, %f, %f]\n", oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.x, oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.y, oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.z);
    }

oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.x = 0;
oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.y = 0;
oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.w = oculus->texSize.w;
oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.h = oculus->texSize.h;

    // DistortedViewport [x,y,w,h]:
    PsychCopyOutDoubleArg(3, FALSE, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.x);
    PsychCopyOutDoubleArg(4, FALSE, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Pos.y);
    PsychCopyOutDoubleArg(5, FALSE, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.w);
    PsychCopyOutDoubleArg(6, FALSE, oculus->eyeRenderDesc[eyeIndex].DistortedViewport.Size.h);

    // PixelsPerTanAngleAtCenter:
    PsychCopyOutDoubleArg(7, FALSE, oculus->eyeRenderDesc[eyeIndex].PixelsPerTanAngleAtCenter.x);
    PsychCopyOutDoubleArg(8, FALSE, oculus->eyeRenderDesc[eyeIndex].PixelsPerTanAngleAtCenter.y);

    // HmdToEyeViewOffset: [x,y,z]:
    PsychCopyOutDoubleArg(9, FALSE, oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.x);
    PsychCopyOutDoubleArg(10, FALSE, oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.y);
    PsychCopyOutDoubleArg(11, FALSE, oculus->eyeRenderDesc[eyeIndex].HmdToEyeViewOffset.z);

    // See enum ovrDistortionCaps: ovrDistortionCap_TimeWarp | ovrDistortionCap_Vignette | ovrDistortionCap_Overdrive
    // distortionCaps = ovrDistortionCap_LinuxDevFullscreen | ovrDistortionCap_HqDistortion | ovrDistortionCap_FlipInput;
    // TODO FIXME: distortionCaps are not used by SDK version 0.5 for ovrHmd_CreateDistortionMesh(), but maybe by later
    // versions? Need to recheck once we use higher SDK versions.
    distortionCaps = 0;
    if (!ovrHmd_CreateDistortionMesh(oculus->hmd, oculus->eyeRenderDesc[eyeIndex].Eye, oculus->eyeRenderDesc[eyeIndex].Fov, distortionCaps, &(oculus->eyeDistortionMesh[eyeIndex]))) {
        if (verbosity > 0) printf("PsychOculusVRCore-ERROR: Failed to compute distortion mesh for HMD %i, eye %i: [%s]\n", handle, eyeIndex, ovrHmd_GetLastError(oculus->hmd));
        PsychErrorExitMsg(PsychError_system, "Failed to compute distortion mesh for eye.");
    }

    if (verbosity > 2) {
        printf("PsychOculusVRCore-INFO: Distortion mesh has %i vertices, %i indices for triangles.\n", oculus->eyeDistortionMesh[eyeIndex].VertexCount, oculus->eyeDistortionMesh[eyeIndex].IndexCount);
    }

    // Return vertex data for the distortion mesh:

    // Each mesh has 10 parameters per vertex:
    m = 10;
    // For given number of vertices:
    n = oculus->eyeDistortionMesh[eyeIndex].VertexCount;
    // And one layer for a 2D matrix:
    p = 1;
    PsychAllocOutDoubleMatArg(12, FALSE, m, n, p, &outVertexMesh);

    pVertexData = oculus->eyeDistortionMesh[eyeIndex].pVertexData;
    for (i = 0; i < n; i++) {
        // Store i'th column for i'th vertex:

        // output vertex 2D (x,y) position:
        *(outVertexMesh++) = pVertexData->ScreenPosNDC.x;
        *(outVertexMesh++) = pVertexData->ScreenPosNDC.y;

        // Timewarp lerp factor:
        *(outVertexMesh++) = pVertexData->TimeWarpFactor;

        // Vignette fade factor:
        *(outVertexMesh++) = pVertexData->VignetteFactor;

        // The tangents of the horizontal and vertical eye angles for the red channel.
        *(outVertexMesh++) = pVertexData->TanEyeAnglesR.x;
        *(outVertexMesh++) = pVertexData->TanEyeAnglesR.y * -1.0;

        // The tangents of the horizontal and vertical eye angles for the green channel.
        *(outVertexMesh++) = pVertexData->TanEyeAnglesG.x;
        *(outVertexMesh++) = pVertexData->TanEyeAnglesG.y * -1.0;

        // The tangents of the horizontal and vertical eye angles for the blue channel.
        *(outVertexMesh++) = pVertexData->TanEyeAnglesB.x;
        *(outVertexMesh++) = pVertexData->TanEyeAnglesB.y * -1.0;

        // Advance to next vertex in mesh:
        pVertexData++;
    }

    // Return index data for the distortion mesh: The mesh is composed of triangles.
    m = p = 1;
    n = oculus->eyeDistortionMesh[eyeIndex].IndexCount;
    PsychAllocOutDoubleMatArg(13, FALSE, m, n, p, &outIndexMesh);

    pIndexData = oculus->eyeDistortionMesh[eyeIndex].pIndexData;
    for (i = 0; i < n; i++) {
        // Store i'th index - Convert uint16 to double:
        *(outIndexMesh++) = (double) *(pIndexData++);
    }

    // Get UV texture sampling scale and offset:
    ovrHmd_GetRenderScaleAndOffset(oculus->eyeRenderDesc[eyeIndex].Fov,
                                   oculus->texSize, oculus->eyeRenderDesc[eyeIndex].DistortedViewport,
                                   (ovrVector2f*) &(oculus->UVScaleOffset[eyeIndex]));

    // EyeToSourceUVScale:
    PsychCopyOutDoubleArg(14, FALSE, oculus->UVScaleOffset[eyeIndex][0].x);
    PsychCopyOutDoubleArg(15, FALSE, oculus->UVScaleOffset[eyeIndex][0].y);

    // EyeToSourceUVOffset:
    PsychCopyOutDoubleArg(16, FALSE, oculus->UVScaleOffset[eyeIndex][1].x);
    PsychCopyOutDoubleArg(17, FALSE, oculus->UVScaleOffset[eyeIndex][1].y);

    ovrEyeType eye = oculus->hmd->EyeRenderOrder[eyeIndex];
    oculus->headPose[eye] = ovrHmd_GetHmdPosePerEye(oculus->hmd, eye);

    ovrHmd_GetEyeTimewarpMatrices(oculus->hmd, (ovrEyeType) eyeIndex,
                                  oculus->headPose[eyeIndex],
                                  oculus->timeWarpMatrices);

    PsychAllocOutDoubleMatArg(18, FALSE, 4, 4, 1, &startMatrix);
    mv = &(oculus->timeWarpMatrices[0].M[0][0]);
    for (i = 0; i < 4 * 4; i++)
        *(startMatrix++) = (double) *(mv++);

    PsychAllocOutDoubleMatArg(19, FALSE, 4, 4, 1, &endMatrix);
    mv = &(oculus->timeWarpMatrices[1].M[0][0]);
    for (i = 0; i < 4 * 4; i++)
        *(endMatrix++) = (double) *(mv++);

    return(PsychError_none);
}