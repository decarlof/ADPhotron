/* Photron.cpp
 *
 * This is a driver for Photron Detectors
 *
 * Author: Kevin Peterson
 *         Argonne National Laboratory
 *
 * Created:  September 25, 2015
 *
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <cantProceed.h>
#include <osiSock.h>
#include <iocsh.h>
#include <epicsExit.h>

#include "ADDriver.h"
#include <epicsExport.h>
#include "Photron.h"

#include <windows.h>

static const char *driverName = "Photron";

static int PDCLibInitialized=0;

static ELLLIST *cameraList;


/** Constructor for Photron; most parameters are simply passed to ADDriver::ADDriver.
  * After calling the base class constructor this method creates a thread to compute the simulated detector data,
  * and sets reasonable default values for parameters defined in this class, asynNDArrayDriver and ADDriver.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] ipAddress The IP address of the camera or starting IP address for auto-detection
  * \param[in] autoDetect Enable auto-detection of camera. Set this to 0 to specify the IP address manually
  * \param[in] maxBuffers The maximum number of NDArray buffers that the NDArrayPool for this driver is
  *            allowed to allocate. Set this to -1 to allow an unlimited number of buffers.
  * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for this driver is
  *            allowed to allocate. Set this to -1 to allow an unlimited amount of memory.
  * \param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
  * \param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
  */
Photron::Photron(const char *portName, const char *ipAddress, int autoDetect,
                 int maxBuffers, size_t maxMemory, int priority, int stackSize)
    : ADDriver(portName, 1, NUM_PHOTRON_PARAMS, maxBuffers, maxMemory,
               asynEnumMask, asynEnumMask, /* asynEnum interface for dynamic mbbi/o */
               0, 0, /* ASYN_CANBLOCK=0, ASYN_MULTIDEVICE=0, autoConnect=1 */
               priority, stackSize),
      pRaw(NULL) {
  int status = asynSuccess;
  int pdcStatus = PDC_SUCCEEDED; // PDC_SUCCEEDED=1, PDC_FAILED=0
  const char *functionName = "Photron";
  unsigned long errCode;
  cameraNode *pNode = new cameraNode;
 
  this->cameraId = epicsStrDup(ipAddress);
  this->autoDetect = autoDetect;
  // Initialize the bitDepth for asynReport in case the feature isn't supported
  this->bitDepth = 0;

  // If this is the first camera we need to initialize the camera list
  if (!cameraList) {
    cameraList = new ELLLIST;
    ellInit(cameraList);
  }
  pNode->pCamera = this;
  ellAdd(cameraList, (ELLNODE *)pNode);

  // CREATE PARAMS HERE
  createParam(PhotronStatusString,        asynParamInt32, &PhotronStatus);
  createParam(PhotronAcquireModeString,   asynParamInt32, &PhotronAcquireMode);
  createParam(PhotronMaxFramesString,     asynParamInt32, &PhotronMaxFrames);
  createParam(Photron8BitSelectString,    asynParamInt32, &Photron8BitSel);
  createParam(PhotronRecordRateString,    asynParamInt32, &PhotronRecRate);
  createParam(PhotronAfterFramesString,   asynParamInt32, &PhotronAfterFrames);
  createParam(PhotronRandomFramesString,  asynParamInt32, &PhotronRandomFrames);
  createParam(PhotronRecCountString,      asynParamInt32, &PhotronRecCount);
  createParam(PhotronSoftTrigString,      asynParamInt32, &PhotronSoftTrig);
  createParam(PhotronRecReadyString,      asynParamInt32, &PhotronRecReady);
  createParam(PhotronLiveString,          asynParamInt32, &PhotronLive);
  createParam(PhotronPlaybackString,      asynParamInt32, &PhotronPlayback);
  createParam(PhotronEndlessString,       asynParamInt32, &PhotronEndless);
  createParam(PhotronReadMemString,       asynParamInt32, &PhotronReadMem);
  createParam(PhotronIRIGString,          asynParamInt32, &PhotronIRIG);
  createParam(PhotronMemIRIGDayString,    asynParamInt32, &PhotronMemIRIGDay);
  createParam(PhotronMemIRIGHourString,   asynParamInt32, &PhotronMemIRIGHour);
  createParam(PhotronMemIRIGMinString,    asynParamInt32, &PhotronMemIRIGMin);
  createParam(PhotronMemIRIGSecString,    asynParamInt32, &PhotronMemIRIGSec);
  createParam(PhotronMemIRIGUsecString,   asynParamInt32, &PhotronMemIRIGUsec);
  createParam(PhotronMemIRIGSigExString,  asynParamInt32, &PhotronMemIRIGSigEx);
  createParam(PhotronSyncPriorityString,  asynParamInt32, &PhotronSyncPriority);
  createParam(PhotronExtIn1SigString,     asynParamInt32, &PhotronExtIn1Sig);
  createParam(PhotronExtIn2SigString,     asynParamInt32, &PhotronExtIn2Sig);
  createParam(PhotronExtIn3SigString,     asynParamInt32, &PhotronExtIn3Sig);
  createParam(PhotronExtIn4SigString,     asynParamInt32, &PhotronExtIn4Sig);
  createParam(PhotronExtOut1SigString,    asynParamInt32, &PhotronExtOut1Sig);
  createParam(PhotronExtOut2SigString,    asynParamInt32, &PhotronExtOut2Sig);
  createParam(PhotronExtOut3SigString,    asynParamInt32, &PhotronExtOut3Sig);
  createParam(PhotronExtOut4SigString,    asynParamInt32, &PhotronExtOut4Sig);
  
  PhotronExtInSig[0] = &PhotronExtIn1Sig;
  PhotronExtInSig[1] = &PhotronExtIn2Sig;
  PhotronExtInSig[2] = &PhotronExtIn3Sig;
  PhotronExtInSig[3] = &PhotronExtIn4Sig;
  PhotronExtOutSig[0] = &PhotronExtOut1Sig;
  PhotronExtOutSig[1] = &PhotronExtOut2Sig;
  PhotronExtOutSig[2] = &PhotronExtOut3Sig;
  PhotronExtOutSig[3] = &PhotronExtOut4Sig;
  
  if (!PDCLibInitialized) {
    /* Initialize the Photron PDC library */
    pdcStatus = PDC_Init(&errCode);
    if (pdcStatus == PDC_FAILED) {
      asynPrint(
          this->pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s:%s: PDC_Init Error %d\n", driverName, functionName, errCode);
      return;
    }
    PDCLibInitialized = 1;
  }

  /* Create the epicsEvents for signaling to the acquisition task when 
     acquisition starts and stops */
  this->startEventId = epicsEventCreate(epicsEventEmpty);
  if (!this->startEventId) {
    printf("%s:%s epicsEventCreate failure for start event\n",
           driverName, functionName);
    return;
  }
  this->stopEventId = epicsEventCreate(epicsEventEmpty);
  if (!this->stopEventId) {
    printf("%s:%s epicsEventCreate failure for stop event\n",
           driverName, functionName);
    return;
  }
  
  /* Create the epicsEvents for signaling to the recording task when 
     to start watching the camera status */
  this->startRecEventId = epicsEventCreate(epicsEventEmpty);
  if (!this->startRecEventId) {
    printf("%s:%s epicsEventCreate failure for start rec event\n",
           driverName, functionName);
    return;
  }
  this->stopRecEventId = epicsEventCreate(epicsEventEmpty);
  if (!this->stopRecEventId) {
    printf("%s:%s epicsEventCreate failure for stop rec event\n",
           driverName, functionName);
    return;
  }
  
  /* Register the shutdown function for epicsAtExit */
  epicsAtExit(shutdown, (void*)this);

  /* Create the thread that updates the images */
  status = (epicsThreadCreate("PhotronTask", epicsThreadPriorityMedium,
                epicsThreadGetStackSize(epicsThreadStackMedium),
                (EPICSTHREADFUNC)PhotronTaskC, this) == NULL);
  if (status) {
    printf("%s:%s epicsThreadCreate failure for image task\n",
           driverName, functionName);
    return;
  }
  
  /* Create the thread that retrieves triggered recordings */
  status = (epicsThreadCreate("PhotronRecTask", epicsThreadPriorityMedium,
                epicsThreadGetStackSize(epicsThreadStackMedium),
                (EPICSTHREADFUNC)PhotronRecTaskC, this) == NULL);
  if (status) {
    printf("%s:%s epicsThreadCreate failure for record task\n",
           driverName, functionName);
    return;
  }
  
  /* Try to connect to the camera.  
   * It is not a fatal error if we cannot now, the camera may be off or owned by
   * someone else. It may connect later. */
  this->lock();
  status = connectCamera();
  this->unlock();
  if (status) {
    printf("%s:%s: cannot connect to camera %s, manually connect later\n", 
           driverName, functionName, cameraId);
    return;
  }
  
  // Does this need to be called before readParameters reads the trigger mode?
  //createStaticEnums();
  createDynamicEnums();
}


Photron::~Photron() {
  cameraNode *pNode = (cameraNode *)ellFirst(cameraList);
  static const char *functionName = "~Photron";

  // Attempt to stop the recording thread
  epicsEventSignal(this->stopRecEventId);
  
  this->lock();
  printf("Disconnecting camera %s\n", this->portName);
  disconnectCamera();
  this->unlock();

  // Find this camera in the list:
  while (pNode) {
    if (pNode->pCamera == this)
      break;
    pNode = (cameraNode *)ellNext(&pNode->node);
  }
  if (pNode) {
    ellDelete(cameraList, (ELLNODE *)pNode);
    delete pNode;
  }

  /* If this is the last camera in the IOC then unregister callbacks and 
     uninitialize */
  if (ellCount(cameraList) == 0) {
    delete cameraList;
  }
}


void Photron::shutdown (void* arg) {
  Photron *p = (Photron*)arg;
  if (p) delete p;
}


static void PhotronRecTaskC(void *drvPvt) {
  Photron *pPvt = (Photron *)drvPvt;
  pPvt->PhotronRecTask();
}


/** This thread puts the camera in playback mode and reads recorded image data
  * from the camera after recording is done.
  */
void Photron::PhotronRecTask() {
  /*asynStatus imageStatus;
  int imageCounter;
  int numImages, numImagesCounter;
  int imageMode;
  int arrayCallbacks;
  NDArray *pImage;
  double acquirePeriod, delay;
  epicsTimeStamp startTime, endTime;
  double elapsedTime;*/
  
  unsigned long status;
  unsigned long nRet;
  unsigned long nErrorCode;
  int acqMode;
  //int recFlag;
  //PDC_FRAME_INFO FrameInfo;
  
  const char *functionName = "PhotronRecTask";

  
  this->lock();
  /* Loop forever */
  while (1) {
    /* Are we in record mode? */
    getIntegerParam(PhotronAcquireMode, &acqMode);
    //printf("is acquisition active?\n");

    /* If we are not in record mode then wait for a semaphore that is given when 
       record mode is requested */
    if (acqMode != 1) {
      /* Release the lock while we wait for an event that says acquire has 
         started, then lock again */
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: waiting for acquire to start\n", driverName, 
                functionName);
      this->unlock();
      epicsEventWait(this->startRecEventId);
      this->lock();
      
    }
    
    // Wait for triggered recording
    while (acqMode == 1) {
      // Get camera status
      nRet = PDC_GetStatus(this->nDeviceNo, &status, &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetStatus failed %d\n", nErrorCode);
      }
      setIntegerParam(PhotronStatus, status);
      if (status == PDC_STATUS_REC) {
          setIntegerParam(ADStatus, ADStatusAcquire);
      }
      callParamCallbacks();
      
      // Triggered acquisition is done when camera status returns to live
      if (status == PDC_STATUS_LIVE) {
        //
        printf("!!!\tAcquisition is done\n");
        //epicsThreadSleep(1.0);
        //
        printf("Put camera in playback mode\n");
        setPlayback();
        //
        printf("Read info from camera\n");
        readMem();
        
        // Reset Acquire
        setIntegerParam(ADAcquire, 0);
        callParamCallbacks();
        
        //
        printf("Return camerea to ready-to-trigger state\n");
        setRecReady();
      }
      
      // release the lock so the trigger PV can be used
      this->unlock();
      epicsThreadSleep(0.001);
      this->lock();
      
      // Update the acq mode
      getIntegerParam(PhotronAcquireMode, &acqMode);
    }
  }
}
  
static void PhotronTaskC(void *drvPvt) {
  Photron *pPvt = (Photron *)drvPvt;
  pPvt->PhotronTask();
}

/** This thread calls readImage to retrieve new image data from the camera and 
  * does the callbacks to send it to higher layers. It implements the logic for 
  * single, multiple or continuous acquisition. */
void Photron::PhotronTask() {
  asynStatus imageStatus;
  int imageCounter;
  int numImages, numImagesCounter;
  int imageMode;
  int arrayCallbacks;
  int acquire;
  NDArray *pImage;
  double acquirePeriod, delay;
  epicsTimeStamp startTime, endTime;
  double elapsedTime;
  const char *functionName = "PhotronTask";

  this->lock();
  /* Loop forever */
  while (1) {
    /* Is acquisition active? */
    getIntegerParam(ADAcquire, &acquire);
    //printf("is acquisition active?\n");

    /* If we are not acquiring then wait for a semaphore that is given when 
       acquisition is started */
    if (!acquire) {
      setIntegerParam(ADStatus, ADStatusIdle);
      callParamCallbacks();
      /* Release the lock while we wait for an event that says acquire has 
         started, then lock again */
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: waiting for acquire to start\n", driverName, 
                functionName);
      this->unlock();
      epicsEventWait(this->startEventId);
      this->lock();
      setIntegerParam(ADNumImagesCounter, 0);
    }

    /* We are acquiring. */
    /* Get the current time */
    epicsTimeGetCurrent(&startTime);

    /* Get the exposure parameters */
    getDoubleParam(ADAcquirePeriod, &acquirePeriod);

    setIntegerParam(ADStatus, ADStatusAcquire);

    /* Open the shutter */
    //setShutter(ADShutterOpen);

    /* Call the callbacks to update any changes */
    callParamCallbacks();

    //printf("I should do something\n");
    
    /* Read the image */
    imageStatus = readImage();

    /* Close the shutter */
    //setShutter(ADShutterClosed);
    
    /* Call the callbacks to update any changes */
    callParamCallbacks();
    
    if (imageStatus == asynSuccess) {
      pImage = this->pArrays[0];

      /* Get the current parameters */
      getIntegerParam(NDArrayCounter, &imageCounter);
      getIntegerParam(ADNumImages, &numImages);
      getIntegerParam(ADNumImagesCounter, &numImagesCounter);
      getIntegerParam(ADImageMode, &imageMode);
      getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
      imageCounter++;
      numImagesCounter++;
      setIntegerParam(NDArrayCounter, imageCounter);
      setIntegerParam(ADNumImagesCounter, numImagesCounter);

      /* Put the frame number and time stamp into the buffer */
      pImage->uniqueId = imageCounter;
      pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
      updateTimeStamp(&pImage->epicsTS);

      /* Get any attributes that have been defined for this driver */
      this->getAttributes(pImage->pAttributeList);

      if (arrayCallbacks) {
        /* Call the NDArray callback */
        /* Must release the lock here, or we can get into a deadlock, because we
         * can block on the plugin lock, and the plugin can be calling us */
        this->unlock();
        asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                  "%s:%s: calling imageData callback\n", driverName,
                  functionName);
        doCallbacksGenericPointer(pImage, NDArrayData, 0);
        this->lock();
      }
    }

    /* See if acquisition is done */
    if ((imageStatus != asynSuccess) || (imageMode == ADImageSingle) ||
        ((imageMode == ADImageMultiple) && (numImagesCounter >= numImages))) {
      setIntegerParam(ADAcquire, 0);
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: acquisition completed\n", driverName, functionName);
    }

    /* Call the callbacks to update any changes */
    callParamCallbacks();
    getIntegerParam(ADAcquire, &acquire);

    /* If acquiring then sleep for the acquire period minus elapsed time. */
    if (acquire) {
      epicsTimeGetCurrent(&endTime);
      elapsedTime = epicsTimeDiffInSeconds(&endTime, &startTime);
      delay = acquirePeriod - elapsedTime;
      asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: delay=%f\n", driverName, functionName, delay);
      if (delay >= 0.0) {
        /* Set the status to readOut to indicate we are in the period delay */
        setIntegerParam(ADStatus, ADStatusWaiting);
        callParamCallbacks();
        this->unlock();
        epicsEventWaitWithTimeout(this->stopEventId, delay);
        this->lock();
      }
    }
  }
}


/* From asynPortDriver: Disconnects driver from device; */
asynStatus Photron::disconnect(asynUser* pasynUser) {
  return disconnectCamera();
}


asynStatus Photron::disconnectCamera() {
  int status = asynSuccess;
  static const char *functionName = "disconnectCamera";
  unsigned long nRet;
  unsigned long nErrorCode;

  /* Ensure that PDC library has been initialised */
  if (!PDCLibInitialized) {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
        "%s:%s: Connecting to camera %s while PDC library is uninitialized.\n", 
        driverName, functionName, this->cameraId);
    return asynError;
  }
  
  nRet = PDC_CloseDevice(this->nDeviceNo, &nErrorCode);
  if (nRet == PDC_FAILED){
    printf("PDC_CloseDevice for device #%d did not succeed. Error code = %d\n", 
           this->nDeviceNo, nErrorCode);
  } else {
    printf("PDC_CloseDevice succeeded for device #%d\n", this->nDeviceNo);
  }
  
  /* Camera is disconnected. Signal to asynManager that it is disconnected. */
  status = pasynManager->exceptionDisconnect(this->pasynUserSelf);
  if (status) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
      "%s:%s: error calling pasynManager->exceptionDisconnect, error=%s\n",
      driverName, functionName, pasynUserSelf->errorMessage);
  }
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
    "%s:%s: Camera disconnected; camera id: %s\n", 
    driverName, functionName, this->cameraId);

  return((asynStatus)status);
}


/* From asynPortDriver: Connects driver to device; */
asynStatus Photron::connect(asynUser* pasynUser) {
  return connectCamera();
}


asynStatus Photron::connectCamera() {
  int status = asynSuccess;
  static const char *functionName = "connectCamera";
  //
  struct in_addr ipAddr;
  unsigned long ipNumWire;
  unsigned long ipNumHost;
  //
  unsigned long nRet;
  unsigned long nErrorCode;
  PDC_DETECT_NUM_INFO DetectNumInfo;     /* Search result */
  unsigned long IPList[PDC_MAX_DEVICE];   /* IP ADDRESS being searched */
  
  /* default IP address is "192.168.0.10" */
  //IPList[0] = 0xC0A8000A;
  /* default IP for auto-detection is "192.168.0.0" */
  //IPList[0] = 0xC0A80000;
  
  /* Ensure that PDC library has been initialised */
  if (!PDCLibInitialized) {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
      "%s:%s: Connecting to camera %s while PDC library is uninitialized.\n", 
      driverName, functionName, this->cameraId);
    return asynError;
  }
  
  // The Prosilica driver does this, but it isn't obvious why
  /* First disconnect from the camera */
  //disconnectCamera();

  /* We have been given an IP address or IP name */
  status = hostToIPAddr(this->cameraId, &ipAddr);
  if (status) {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
      "%s:%s: Cannot find IP address %s\n", 
      driverName, functionName, this->cameraId);
    //return asynError;
  }
  ipNumWire = (unsigned long) ipAddr.s_addr;
  /* The Photron SDK needs the ip address in host byte order */
  ipNumHost = ntohl(ipNumWire);

  IPList[0] = ipNumHost;
  
  // Attempt to detect the type of detector at the specified ip addr
  nRet = PDC_DetectDevice(
              PDC_INTTYPE_G_ETHER, /* Gigabit ethernet interface */
              IPList,              /* IP address */
              1,                   /* Max number of searched devices */
              this->autoDetect,    /* 0=PDC_DETECT_NORMAL;1=PDC_DETECT_AUTO */
              &DetectNumInfo,
              &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_DetectDevice Error %d\n", nErrorCode);
    return asynError;
  }
  
  printf("PDC_DetectDevice \"Successful\"\n");
  printf("\tdevice index: %d\n", DetectNumInfo.m_nDeviceNum);
  printf("\tdevice code: %d\n", DetectNumInfo.m_DetectInfo[0].m_nDeviceCode);
  printf("\tnRet = %d\n", nRet);

  if (DetectNumInfo.m_nDeviceNum == 0) {
    printf("No devices detected\n");
    return asynError;
  }

  /* only do this if not auto-searching for devices */
  if ((this->autoDetect == PDC_DETECT_NORMAL) && 
     (DetectNumInfo.m_DetectInfo[0].m_nTmpDeviceNo != IPList[0])) {
    printf("The specified and detected IP addresses differ:\n");
    printf("\tIPList[0] = %x\n", IPList[0]);
    printf("\tm_nTmpDeviceNo = %x\n", 
           DetectNumInfo.m_DetectInfo[0].m_nTmpDeviceNo);
    return asynError;
  }

  nRet = PDC_OpenDevice(&(DetectNumInfo.m_DetectInfo[0]), &(this->nDeviceNo),
                        &nErrorCode);
  /* When should PDC_OpenDevice2 be used instead of PDC_OpenDevice? */
  //nRet = PDC_OpenDevice2(&(DetectNumInfo.m_DetectInfo[0]), 
  //            10,  /* nMaxRetryCount */
  //            0,  /* nConnectMode -- 1=normal, 0=safe */
  //            &(this->nDeviceNo),
  //            &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_OpenDeviceError %d\n", nErrorCode);
    return asynError;
  } else {
    printf("Device #%i opened successfully\n", this->nDeviceNo);
  }

  /* Assume only one child, for now */
  this->nChildNo = 1;
  
  /* PDC_GetStatus is also called in readParameters(), but it is called here
     so that the camera can be put into live mode--will remove this after
     making the mode a PV */
  nRet = PDC_GetStatus(this->nDeviceNo, &(this->nStatus), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetStatus failed %d\n", nErrorCode);
    return asynError;
  } else {
    if (this->nStatus == PDC_STATUS_PLAYBACK) {
      nRet = PDC_SetStatus(this->nDeviceNo, PDC_STATUS_LIVE, &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_SetStatus failed. error = %d\n", nErrorCode);
      }
    }
  }
  /* Get information from the camera */
  status = getCameraInfo();
  if (status) {
    return((asynStatus)status);
  }
  
  /* Set some initial values for other parameters */
  status =  setStringParam (ADManufacturer, "Photron");
  status |= setStringParam (ADModel, this->deviceName);
  status |= setIntegerParam(ADSizeX, this->sensorWidth);
  status |= setIntegerParam(ADSizeY, this->sensorHeight);
  status |= setIntegerParam(ADMaxSizeX, this->sensorWidth);
  status |= setIntegerParam(ADMaxSizeY, this->sensorHeight);
  
  if (status) {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: unable to set camera parameters on camera %s\n",
              driverName, functionName, this->cameraId);
    return asynError;
  }
  
  /* Read the current camera settings */
  status = readParameters();
  if (status) {
    return((asynStatus)status);
  }
  
  /* We found the camera. Everything is OK. Signal to asynManager that we are 
     connected. */
  status = pasynManager->exceptionConnect(this->pasynUserSelf);
  if (status) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
      "%s:%s: error calling pasynManager->exceptionConnect, error=%s\n",
      driverName, functionName, pasynUserSelf->errorMessage);
    return asynError;
  }
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
    "%s:%s: Camera connected; camera id: %ld\n", driverName,
    functionName, this->cameraId);
  return asynSuccess;
}


asynStatus Photron::getCameraInfo() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  char sensorBitChar;
  static const char *functionName = "getCameraInfo";
  //
  int index;
  char nFlag; /* Existing function flag */

  /* Determine which functions are supported by the camera */
  for( index=2; index<98; index++) {
    nRet = PDC_IsFunction(this->nDeviceNo, this->nChildNo, index, &nFlag, 
                          &nErrorCode);
    if (nRet == PDC_FAILED) {
      if (nErrorCode == PDC_ERROR_NOT_SUPPORTED) {
        this->functionList[index] = PDC_EXIST_NOTSUPPORTED;
      } else {
        printf("PDC_IsFunction failed for function %d, error = %d\n", 
               index, nErrorCode);
        return asynError;
      }
    } else {
      this->functionList[index] = nFlag;
    }
  }
  //printf("function queries succeeded\n");
  
  /* query the controller for info */
  
  nRet = PDC_GetDeviceCode(this->nDeviceNo, &(this->deviceCode), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetDeviceCode failed %d\n", nErrorCode);
    return asynError;
  }  
  
  nRet = PDC_GetDeviceName(this->nDeviceNo, 0, this->deviceName, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetDeviceName failed %d\n", nErrorCode);
    return asynError;
  }
  
  nRet = PDC_GetDeviceID(this->nDeviceNo, &(this->deviceID), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetDeviceID failed %d\n", nErrorCode);
    return asynError;
  }
  
  nRet = PDC_GetLotID(this->nDeviceNo, 0, &(this->lotID), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetLotID failed %d\n", nErrorCode);
    return asynError;
  }
  
  nRet = PDC_GetProductID(this->nDeviceNo, 0, &(this->productID), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetProductID failed %d\n", nErrorCode);
    return asynError;
  }
  
  nRet = PDC_GetIndividualID(this->nDeviceNo, 0, &(this->individualID), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetIndividualID failed %d\n", nErrorCode);
    return asynError;
  }
  
  nRet = PDC_GetVersion(this->nDeviceNo, 0, &(this->version), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetVersion failed %d\n", nErrorCode);
    return asynError;
  }  

  nRet = PDC_GetMaxChildDeviceCount(this->nDeviceNo, &(this->maxChildDevCount), 
                                    &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetMaxChildDeviceCount failed %d\n", nErrorCode);
    return asynError;
  }  

  nRet = PDC_GetChildDeviceCount(this->nDeviceNo, &(this->childDevCount), 
                                 &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetChildDeviceCount failed %d\n", nErrorCode);
    return asynError;
  }  
  
  nRet = PDC_GetMaxResolution(this->nDeviceNo, this->nChildNo, 
                              &(this->sensorWidth), &(this->sensorHeight), 
                              &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetMaxResolution failed %d\n", nErrorCode);
    return asynError;
  }
  
  /* This gets the dynamic range of the camera. The third argument is an 
     unsigned long in the SDK documentation but a char * in PDCFUNC.h.
     It appears that only a single char is returned. */
  nRet = PDC_GetMaxBitDepth(this->nDeviceNo, this->nChildNo, &sensorBitChar,
                            &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetMaxBitDepth failed %d\n", nErrorCode);
    return asynError;
  } else {
    this->sensorBits = (unsigned long) sensorBitChar;
  }
  
  nRet = PDC_GetExternalCount(this->nDeviceNo, &(this->inPorts), 
                              &(this->outPorts), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetExternalCount failed %d\n", nErrorCode);
    return asynError;
  }
  
  // Do these mode lists need to be called from readParameters?
  // If the same mode is available on two ports, can it only be used with one?
  // PDC_EXTIO_MAX_PORT is defined in PDCVALUE.h
  for (index=0; index<PDC_EXTIO_MAX_PORT; index++) {
    // Input port
    if (index < this->inPorts) {
      // Port exists, query the input list
      nRet = PDC_GetExternalInModeList(this->nDeviceNo, index+1,
                                       &(this->ExtInModeListSize[index]),
                                       this->ExtInModeList[index], &nErrorCode);
    } else {
      // Port doesn't exist; zero the list size
      this->ExtInModeListSize[index] = 0;
    }
    
    // Output port
    if (index < this->outPorts) {
      // Port exists, query the input list
      nRet = PDC_GetExternalOutModeList(this->nDeviceNo, index+1,
                                       &(this->ExtOutModeListSize[index]),
                                       this->ExtOutModeList[index], &nErrorCode);
    } else {
      // Port doesn't exist; zero the list size
      this->ExtOutModeListSize[index] = 0;
    }
  }
  
  // Is this always the same or should it be moved to readParameters?
  nRet = PDC_GetSyncPriorityList(this->nDeviceNo, &(this->SyncPriorityListSize),
                                 this->SyncPriorityList, &nErrorCode);
  
  // Is this always the same or should it be moved to readParameters?
  nRet = PDC_GetRecordRateList(this->nDeviceNo, this->nChildNo, 
                               &(this->RateListSize), this->RateList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetRecordRateList failed %d\n", nErrorCode);
    return asynError;
  } 
  
  // This needs to be called once before readParameters is called, otherwise
  // updateResolution will crash the IOC
  nRet = PDC_GetResolutionList(this->nDeviceNo, this->nChildNo, 
                               &(this->ResolutionListSize),
                               this->ResolutionList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetResolutionList failed %d\n", nErrorCode);
    return asynError;
  }
  
  // Query the trigger mode list
  // This is necessary to work around a bug in the SA-Z firmware.  When the SA-Z
  // is powered on, the first call to PDC_GetTriggerModeList can return trigger
  // modes that are disabled.  Subsequent calls omit the disabled modes.
  nRet = PDC_GetTriggerModeList(this->nDeviceNo, &(this->TriggerModeListSize),
                                this->TriggerModeList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetTriggerModeList failed %d\n", nErrorCode);
    return asynError;
  } else {
    printf("\t!!! FIRST num trig modes = %d\n", this->TriggerModeListSize);
  }
  
  return asynSuccess;
}

asynStatus Photron::readImage() {
  int status = asynSuccess;
  //
  int sizeX, sizeY;
  size_t dims[2];
  double gain;
  //
  NDArray *pImage;
  NDArrayInfo_t arrayInfo;
  int colorMode = NDColorModeMono;
  //
  unsigned long nRet;
  unsigned long nErrorCode;
  void *pBuf;  /* Memory sequence pointer for storing a live image */
  //
  NDDataType_t dataType;
  int pixelSize;
  size_t dataSize;
  static const char *functionName = "readImage";

  getIntegerParam(ADSizeX,  &sizeX);
  getIntegerParam(ADSizeY,  &sizeY);
  getDoubleParam (ADGain,   &gain);
  
  //-------
  // DEBUG print the status - will I need to check the status before acquiring in the future?
  /*nRet = PDC_GetStatus(this->nDeviceNo, &(this->nStatus), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetStatus failed %d\n", nErrorCode);
    return asynError;
  } else {
    printf("PDC_GetStatus succeeded. status = %d\n", this->nStatus);
  }*/
  //-------
  
  if (this->pixelBits == 8) {
    // 8 bits
    dataType = NDUInt8;
    pixelSize = 1;
  } else {
    // 12 bits (stored in 2 bytes)
    dataType = NDUInt16;
    pixelSize = 2;
  }
  
  //printf("sizeof(epicsUInt8) = %d\n", sizeof(epicsUInt8));
  //printf("sizeof(epicsUInt16) = %d\n", sizeof(epicsUInt16));
  
  dataSize = sizeX * sizeY * pixelSize;
  pBuf = malloc(dataSize);
  
  nRet = PDC_GetLiveImageData(this->nDeviceNo, this->nChildNo,
                              this->pixelBits,
                              pBuf, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetLiveImageData Failed. Error %d\n", nErrorCode);
    free(pBuf);
    return asynError;
  }

  /* We save the most recent image buffer so it can be used in the read() 
   * function. Now release it before getting a new version. */
  if (this->pArrays[0]) 
    this->pArrays[0]->release();
  
  /* Allocate the raw buffer */
  dims[0] = sizeX;
  dims[1] = sizeY;
  pImage = this->pNDArrayPool->alloc(2, dims, dataType, 0, NULL);
  if (!pImage) {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: error allocating buffer\n", driverName, functionName);
    return(asynError);
  }
  
  memcpy(pImage->pData, pBuf, dataSize);
  
  this->pArrays[0] = pImage;
  pImage->pAttributeList->add("ColorMode", "Color mode", NDAttrInt32, 
                              &colorMode);
  pImage->getInfo(&arrayInfo);
  setIntegerParam(NDArraySize,  (int)arrayInfo.totalBytes);
  setIntegerParam(NDArraySizeX, (int)pImage->dims[0].size);
  setIntegerParam(NDArraySizeY, (int)pImage->dims[1].size);

  free(pBuf);
  
  return asynSuccess;
}


/** Called when asyn clients call pasynInt32->write().
  * This function performs actions for some parameters, including ADAcquire, ADBinX, etc.
  * For all parameters it sets the value in the parameter library and calls any registered callbacks..
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus Photron::writeInt32(asynUser *pasynUser, epicsInt32 value) {
  int function = pasynUser->reason;
  int status = asynSuccess;
  int adstatus, acqMode;
  static const char *functionName = "writeInt32";

  /* Set the parameter and readback in the parameter library.  This may be 
   * overwritten when we read back the status at the end, but that's OK */
  status |= setIntegerParam(function, value);

  if ((function == ADBinX) || (function == ADBinY) || (function == ADMinX) ||
     (function == ADMinY)) {
    /* These commands change the chip readout geometry.  We need to cache them 
     * and apply them in the correct order */
    //printf("calling setGeometry. function=%d, value=%d\n", function, value);
    status |= setGeometry();
  } else if (function == ADSizeX) {
    status |= setValidWidth(value);
  } else if (function == ADSizeY) {
    status |= setValidHeight(value);
  } else if (function == ADAcquire) {
    getIntegerParam(PhotronAcquireMode, &acqMode);
    if (acqMode == 0) {
      // For Live mode, signal the PhotronTask
      getIntegerParam(ADStatus, &adstatus);
      if (value && (adstatus == ADStatusIdle)) {
        /* Send an event to wake up the acquisition task.
        * It won't actually start generating new images until we release the lock
        * below */
        epicsEventSignal(this->startEventId);
      }
      if (!value && (adstatus != ADStatusIdle)) {
        /* This was a command to stop acquisition */
        /* Send the stop event */
        epicsEventSignal(this->stopEventId);
      }
    } else {
      // For Record mode
      if (value) {
        // Send a software trigger to start acquisition
        softwareTrigger();
        setIntegerParam(ADAcquire, 1);
      } else {
        // Stop acquisition
        this->abortFlag = 1;
        setIntegerParam(ADAcquire, 0);
      }
    }
  } else if (function == NDDataType) {
    status = setPixelFormat();
  } else if (function == PhotronAcquireMode) {
    // should the acquire state be checked?
    if (value == 0) {
      printf("Settings for returning to live mode should go here\n");
      
      // code to return to live mode goes here
      setLive();
      
      // Stop the PhotronRecTask (will it do one last read after returning to live?
      epicsEventSignal(this->stopRecEventId);
    } else {
      printf("Settings for entering recording mode should go here\n");
      
      // apply the trigger settings?
      
      // code to enter recording mode
      setRecReady();
      
      // Wake up the PhotronRecTask
      epicsEventSignal(this->startRecEventId);
    }
  } else if (function == Photron8BitSel) {
    /* Specifies the bit position during 8-bit transfer from a device of more 
       than 8 bits. */
    setTransferOption();
  } else if (function == PhotronRecRate) {
    setRecordRate(value);
  } else if (function == PhotronStatus) {
    setStatus(value);
  } else if (function == PhotronSoftTrig) {
    printf("Soft Trigger changed. value = %d\n", value);
    softwareTrigger();
    
  } else if (function == PhotronRecReady) {
    setRecReady();
    
  } else if (function == PhotronEndless) {
    setEndless();
    
  } else if (function == PhotronLive) {
    setLive();
    
  } else if (function == PhotronPlayback) {
    setPlayback();
    
  } else if (function == PhotronReadMem) {
    readMem();
    
  } else if ((function == ADTriggerMode) || (function == PhotronAfterFrames) ||
            (function == PhotronRandomFrames) || (function == PhotronRecCount)) {
    printf("function = %d\n", function);
    setTriggerMode();
  } else if (function == PhotronIRIG) {
    setIRIG(value);
  } else if (function == PhotronSyncPriority) {
    setSyncPriority(value);
  } else if (function == PhotronExtIn1Sig) {
    setExternalInMode(1, value);
  } else if (function == PhotronExtIn2Sig) {
    setExternalInMode(2, value);
  } else if (function == PhotronExtIn3Sig) {
    setExternalInMode(3, value);
  } else if (function == PhotronExtIn4Sig) {
    setExternalInMode(4, value);
  } else if (function == PhotronExtOut1Sig) {
    setExternalOutMode(1, value);
  } else if (function == PhotronExtOut2Sig) {
    setExternalOutMode(2, value);
  } else if (function == PhotronExtOut3Sig) {
    setExternalOutMode(3, value);
  } else if (function == PhotronExtOut4Sig) {
    setExternalOutMode(4, value);
  } else {
    /* If this is not a parameter we have handled call the base class */
    status = ADDriver::writeInt32(pasynUser, value);
  }
  
  /* Read the camera parameters and do callbacks */
  status |= readParameters();
  
  if (status) 
    asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d function=%d, value=%d\n", 
              driverName, functionName, status, function, value);
  else        
    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, value=%d\n",
              driverName, functionName, function, value);
  return((asynStatus)status);
}


asynStatus Photron::readEnum(asynUser *pasynUser, char *strings[], int values[],
                             int severities[], size_t nElements, size_t *nIn) {
  int function = pasynUser->reason;
  enumStruct_t *pEnum;
  int numEnums;
  int i;
  
  if (function == PhotronExtIn1Sig) {
    pEnum = inputModeEnums_[0];
    numEnums = numValidInputModes_[0];
  } else if (function == PhotronExtIn2Sig) {
    pEnum = inputModeEnums_[1];
    numEnums = numValidInputModes_[1];
  } else if (function == PhotronExtIn3Sig) {
    pEnum = inputModeEnums_[2];
    numEnums = numValidInputModes_[2];
  } else if (function == PhotronExtIn4Sig) {
    pEnum = inputModeEnums_[3];
    numEnums = numValidInputModes_[3];
  } else if (function == PhotronExtOut1Sig) {
    pEnum = outputModeEnums_[0];
    numEnums = numValidOutputModes_[0];
  } else if (function == PhotronExtOut2Sig) {
    pEnum = outputModeEnums_[1];
    numEnums = numValidOutputModes_[1];
  } else if (function == PhotronExtOut3Sig) {
    pEnum = outputModeEnums_[2];
    numEnums = numValidOutputModes_[2];
  } else if (function == PhotronExtOut4Sig) {
    pEnum = outputModeEnums_[3];
    numEnums = numValidOutputModes_[3];
  } else {
    *nIn = 0;
    return asynError;
  }
  
  for (i=0; ((i<numEnums) && (i<(int)nElements)); i++) {
    if (strings[i]) free(strings[i]);
    strings[i] = epicsStrDup(pEnum->string);
    values[i] = pEnum->value;
    severities[i] = 0;
    pEnum++;
  }
  *nIn = i;
  return asynSuccess;
}


asynStatus Photron::createDynamicEnums() {
  int index, mode;
  enumStruct_t *pEnum;
  unsigned long nRet, nErrorCode;
  char *enumStrings[NUM_TRIGGER_MODES];
  int enumValues[NUM_TRIGGER_MODES];
  int enumSeverities[NUM_TRIGGER_MODES];
  
  static const char *functionName = "createDynamicEnums";
  
  /* Trigger mode enums */
  nRet = PDC_GetTriggerModeList(this->nDeviceNo, &(this->TriggerModeListSize),
                                this->TriggerModeList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetTriggerModeList failed %d\n", nErrorCode);
    return asynError;
  }
  
  printf("\t!!! number of trigger modes detected: %d\n", this->TriggerModeListSize);
  
  numValidTriggerModes_ = 0;
  /* Loop over modes */
  for (index=0; index<this->TriggerModeListSize; index++) {
    // get a pointer an element in the triggerModeEnums_ array
    pEnum = triggerModeEnums_ + numValidTriggerModes_;
    // convert the trigger mode
    mode = this->trigModeToEPICS(this->TriggerModeList[index]);
    strcpy(pEnum->string, triggerModeStrings[mode]);
    pEnum->value = mode;
    numValidTriggerModes_++;
  }
  for (index=0; index<numValidTriggerModes_; index++) {
    enumStrings[index] = triggerModeEnums_[index].string;
    enumValues[index] = triggerModeEnums_[index].value;
    enumSeverities[index] = 0;
  }
  doCallbacksEnum(enumStrings, enumValues, enumSeverities, 
                  numValidTriggerModes_, ADTriggerMode, 0);
  
  return asynSuccess;
}


asynStatus Photron::createStaticEnums() {
  /* This function creates enum strings and values for all enums that are fixed
  for a given camera.  It is only called once at startup */
  int index, input, output, mode;
  enumStruct_t *pEnum;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "createStaticEnums";
  
  //printf("Creating static enums\n");
  
  // I/O port lists were already acquired when getCameraInfo was called
  for (input=0; input<PDC_EXTIO_MAX_PORT; input++) {
    //
    numValidInputModes_[input] = 0;
    // ExtInModeList has values in hex; need to convert them to epics index
    for (index=0; index<this->ExtInModeListSize[input]; index++) {
      pEnum = inputModeEnums_[input] + numValidInputModes_[input];
      mode = inputModeToEPICS(this->ExtInModeList[input][index]);
      strcpy(pEnum->string, inputModeStrings[mode]);
      pEnum->value = mode;
      numValidInputModes_[input]++;
    }

    numValidOutputModes_[input] = 0;
    // ExtOutModeList has values in hex; need to convert them to epics index
    for (index=0; index<this->ExtOutModeListSize[input]; index++) {
      pEnum = outputModeEnums_[input] + numValidOutputModes_[input];
      mode = outputModeToEPICS(this->ExtOutModeList[input][index]);
      strcpy(pEnum->string, outputModeStrings[mode]);
      pEnum->value = mode;
      numValidOutputModes_[input]++;
    }
  }
  
  return asynSuccess;
}


int Photron::inputModeToEPICS(int apiMode) {
  int mode;
  
  switch (apiMode) {
    case PDC_EXT_IN_ENCODER_POSI:
    case PDC_EXT_IN_ENCODER_NEGA:
      mode = (apiMode & 0xF) + 15;
      break;
    default:
      mode = apiMode - 1;
      break;
  }
  
  return mode;
}


int Photron::outputModeToEPICS(int apiMode) {
  int mode;
  
  if (apiMode < 0xF) {
    // 0x01 => 0 ; 0x0E => 13
    mode = apiMode - 1;
  } else if (apiMode < 0x4F) {
    // 0x1D => 14 ; 0x4E => 21
    mode = ((((apiMode & 0xF0) >> 4) - 1) * 2) + (apiMode & 0xF) + 1;
  } else if (apiMode < 0xFF) {
    // 0x50 => 22 ; 0x59 => 31
    mode = (apiMode & 0xF) + 22;
  } else {
    // 0x100 => 32 ; 0x102 => 34
    mode = (apiMode & 0xF) + 32;
  }
  
  return mode;
}


int Photron::trigModeToEPICS(int apiMode) {
  int mode;
  
  // TODO: add final modes
  switch (apiMode) {
    case PDC_TRIGGER_TWOSTAGE_HALF:
        mode = 8;
      break;
      
    case PDC_TRIGGER_TWOSTAGE_QUARTER:
        mode = 9;
      break;
      
    case PDC_TRIGGER_TWOSTAGE_ONEEIGHTH:
        mode = 10;
      break;
    
    default:
        // this won't work for recon cmd and random loop modes
        mode = apiMode >> 24;
      break;
  }
  
  return mode;
}

int Photron::trigModeToAPI(int mode) {
  int apiMode;
  
  // TODO: add final two modes
  switch (mode) {
    case 8:
      apiMode = PDC_TRIGGER_TWOSTAGE_HALF;
      break;
    case 9:
      apiMode = PDC_TRIGGER_TWOSTAGE_QUARTER;
      break;
    case 10:
      apiMode = PDC_TRIGGER_TWOSTAGE_ONEEIGHTH;
      break;
    default:
      apiMode = mode << 24;
      break;
  }
  
  return apiMode;
}

asynStatus Photron::softwareTrigger() {
  asynStatus status = asynSuccess;
  int acqMode;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "softwareTrigger";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Only send a software trigger if in Record mode
  if (acqMode == 1) {
    nRet = PDC_TriggerIn(this->nDeviceNo, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_TriggerIn failed. error = %d\n", nErrorCode);
      return asynError;
    }
  } else {
    printf("Ignoring software trigger\n");
  }
  
  return status;
}


asynStatus Photron::setRecReady() {
  asynStatus status = asynSuccess;
  int acqMode, mode, apiMode;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setRecReady";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Only set rec ready if in record mode
  if (acqMode == 1) {
    nRet = PDC_SetRecReady(nDeviceNo, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_SetRecReady failed. error = %d\n", nErrorCode);
      return asynError;
    }
    
    // This code is duplicated in setTriggerMode
    getIntegerParam(ADTriggerMode, &mode);
    
    // The mode isn't in the right format for the PDC_SetTriggerMode call
    apiMode = this->trigModeToAPI(mode);
    
    // Set endless for trigger modes that need it
    switch (apiMode) {
      case PDC_TRIGGER_CENTER:
      case PDC_TRIGGER_END:
      case PDC_TRIGGER_MANUAL:
      case PDC_TRIGGER_RANDOM_CENTER:
      case PDC_TRIGGER_RANDOM_MANUAL:
        //
        setEndless();
        break;
      default:
        //
        break;
    }
    
    //
    setIntegerParam(ADStatus, ADStatusWaiting);
    callParamCallbacks();
    
    } else {
    printf("Ignoring set rec ready\n");
  }
  
  return status;
}


asynStatus Photron::setEndless() {
  asynStatus status = asynSuccess;
  int acqMode;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setEndless";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Only set endless trigger if in record mode
  // TODO: add test for relevent trigger modes
  if (acqMode == 1) {
    nRet = PDC_SetEndless(this->nDeviceNo, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_SetEndless failed. error = %d\n", nErrorCode);
      return asynError;
    }
  } else {
    printf("Ignoring endless trigger\n");
  }
  
  return status;
}


asynStatus Photron::setLive() {
  asynStatus status = asynSuccess;
  int acqMode;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setLive";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Put the camera in live mode
  nRet = PDC_SetStatus(this->nDeviceNo, PDC_STATUS_LIVE, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetStatus failed. error = %d\n", nErrorCode);
    return asynError;
  }
  
  setIntegerParam(ADStatus, ADStatusIdle);
  callParamCallbacks();
  
  return status;
}


asynStatus Photron::setIRIG(epicsInt32 value) {
  asynStatus status = asynSuccess;
  unsigned long nRet, nErrorCode;
  epicsUInt32 secDiff, nsecDiff;
  static const char *functionName = "setIRIG";

  if (this->functionList[PDC_EXIST_IRIG] == PDC_EXIST_SUPPORTED) {
    if (value) {
      // Enabling IRIG resets the internal clock
      epicsTimeGetCurrent(&(this->preIRIGStartTime));
      nRet = PDC_SetIRIG(this->nDeviceNo, PDC_FUNCTION_ON, &nErrorCode);
      epicsTimeGetCurrent(&(this->postIRIGStartTime));
      secDiff = (this->postIRIGStartTime).secPastEpoch - (this->preIRIGStartTime).secPastEpoch;
      nsecDiff = (this->postIRIGStartTime).nsec - (this->preIRIGStartTime).nsec;
      // Note: The time spent executing epicsTimeGetCurrent is negligible
      //   time to execute epicsTimeGetCurrent = 285 nsec
      //   time to execute PDC_SetIRIG = 40.57 msec
      printf("IRIG clock correlation uncertainty: %d seconds and %d nanoseconds\n", secDiff, nsecDiff);
    } else {
      nRet = PDC_SetIRIG(this->nDeviceNo, PDC_FUNCTION_OFF, &nErrorCode);
    }
    if (nRet == PDC_FAILED) {
      printf("PDC_SetIRIG failed %d\n", nErrorCode);
      status = asynError;
    }
    else {
      printf("PDC_SetIRIG succeeded value=%d\n", value);
      
      // Changing the IRIG state can cause a change in the trigger mode
      createDynamicEnums();
    }
  }
  
  return status;
}


asynStatus Photron::setSyncPriority(epicsInt32 value) {
  asynStatus status = asynSuccess;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setSyncPriority";
  
  // PDC_SetSyncPriorityList
  if (this->functionList[PDC_EXIST_SYNC_PRIORITY] == PDC_EXIST_SUPPORTED) {
    nRet = PDC_SetSyncPriority(this->nDeviceNo, value, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_SetSyncPriority failed %d\n", nErrorCode);
      status = asynError;
    }
  }
  
  return status;
}


asynStatus Photron::setExternalInMode(epicsInt32 port, epicsInt32 value) {
  asynStatus status = asynSuccess;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setExternalInMode";
  
  //
  nRet = PDC_SetExternalInMode(this->nDeviceNo, port, value, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetExternalInMode failed %d\n", nErrorCode);
    status = asynError;
  }
  
  return status;
}


asynStatus Photron::setExternalOutMode(epicsInt32 port, epicsInt32 value) {
  asynStatus status = asynSuccess;
  unsigned long nRet, nErrorCode;
  static const char *functionName = "setExternalOutMode";
  
  //
  nRet = PDC_SetExternalOutMode(this->nDeviceNo, port, value, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetExternalOutMode failed %d\n", nErrorCode);
    status = asynError;
  }
  
  return status;
}


asynStatus Photron::setPlayback() {
  asynStatus status = asynSuccess;
  int acqMode;
  unsigned long nRet, nErrorCode, phostat;
  static const char *functionName = "setPlayback";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Only set playback if in record mode
  if (acqMode == 1) {
    // Put the camera in playback mode
    nRet = PDC_SetStatus(this->nDeviceNo, PDC_STATUS_PLAYBACK, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_SetStatus failed. error = %d\n", nErrorCode);
      return asynError;
    }
    
    // Confirm that the camera is in playback mode
    nRet = PDC_GetStatus(this->nDeviceNo, &phostat, &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetStatus failed. error = %d\n", nErrorCode);
      return asynError;
    }
    
    if (phostat == PDC_STATUS_PLAYBACK) {
      setIntegerParam(PhotronStatus, phostat);
      setIntegerParam(ADStatus, ADStatusReadout);
      callParamCallbacks();
    }
    
  } else {
    printf("Ignoring playback\n");
  }
  
  return status;
}


asynStatus Photron::readMem() {
  asynStatus status = asynSuccess;
  int acqMode, phostat, index, transferBitDepth;
  unsigned long nRet, nErrorCode;
  PDC_FRAME_INFO FrameInfo;
  unsigned long memRate, memWidth, memHeight;
  unsigned long memTrigMode, memAFrames, memRFrames, memRCount;
  PDC_IRIG_INFO tData;
  unsigned long tMode;
  //
  NDArray *pImage;
  NDArrayInfo_t arrayInfo;
  int colorMode = NDColorModeMono;
  //
  void *pBuf;  /* Memory sequence pointer for storing a live image */
  //
  NDDataType_t dataType;
  int pixelSize;
  size_t dims[2];
  size_t dataSize;
  //
  int imageCounter;
  int numImages, numImagesCounter;
  int imageMode;
  int arrayCallbacks;
  //double acquirePeriod, delay;
  epicsTimeStamp startTime, endTime;
  double elapsedTime;
  epicsUInt32 irigSeconds;
  //
  static const char *functionName = "readMem";
  
  status = getIntegerParam(PhotronAcquireMode, &acqMode);
  status = getIntegerParam(PhotronStatus, &phostat);
  
  // Zero image counter
  setIntegerParam(ADNumImagesCounter, 0);
  callParamCallbacks();
  
  // Only read memory if in record mode
  // AND status is playback
  if (acqMode == 1) {
    if (phostat == PDC_STATUS_PLAYBACK) {
      
      /* Get the current time */
      epicsTimeGetCurrent(&startTime);
      
      // Retrieves frame information 
      nRet = PDC_GetMemFrameInfo(this->nDeviceNo, this->nChildNo, &FrameInfo,
                                 &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetMemFrameInfo Error %d\n", nErrorCode);
        return asynError;
      }
      // display frame info
      printf("Frame Info:\n");
      printf("\tFrame Start:\t%d\n", FrameInfo.m_nStart);
      printf("\tFrame Trigger:\t%d\n", FrameInfo.m_nTrigger);
      printf("\tFrame End:\t%d\n", FrameInfo.m_nEnd);
      printf("\t2S Low->High:\t%d\n", FrameInfo.m_nTwoStageLowToHigh);
      printf("\t2S High->Low:\t%d\n", FrameInfo.m_nTwoStageHighToLow);
      printf("\tEvent frame numbers:\n");
      for (index=0; index<10; index++) {
        printf("\t\ti=%d\tframe: %d\n", index, FrameInfo.m_nEvent[index]);
      }
      printf("\tEvent count:\t%d\n", FrameInfo.m_nEventCount);
      printf("\tRecorded Frames:\t%d\n", FrameInfo.m_nRecordedFrames);
        
      // PDC_GetMemResolution
      nRet = PDC_GetMemResolution(this->nDeviceNo, this->nChildNo, &memWidth,
                                  &memHeight, &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetMemResolution Error %d\n", nErrorCode);
        return asynError;
      }
      printf("Memory Resolution: %d x %d\n", memWidth, memHeight);
      
      // PDC_GetMemRecordRate
      nRet = PDC_GetMemRecordRate(this->nDeviceNo, this->nChildNo, &memRate,
                                  &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetMemRecordRate Error %d\n", nErrorCode);
        return asynError;
      }
      printf("Memory Record Rate = %d Hz\n", memRate);
      
      // PDC_GetMemTriggerMode
      nRet = PDC_GetMemTriggerMode(this->nDeviceNo, this->nChildNo, 
                                   &memTrigMode, &memAFrames, &memRFrames, 
                                   &memRCount, &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetMemTriggerMode Error %d\n", nErrorCode);
        return asynError;
      }
      printf("Memory Trigger Mode = %d\n", memTrigMode);
      printf("Memory After Frames = %d\n", memAFrames);
      printf("Memory Random Frames = %d\n", memRFrames);
      printf("Memory Record Count = %d\n", memRCount);
      
      // PDC_GetMemIRIG
      nRet = PDC_GetMemIRIG(this->nDeviceNo, this->nChildNo, &tMode, &nErrorCode);
      if (nRet == PDC_FAILED) {
        printf("PDC_GetMemIRIG Error %d\n", nErrorCode);
        tMode = 0;
      } 
      printf("Memory IRIG mode: %d\n", tMode);
      if (tMode == 0) {
        setIntegerParam(PhotronMemIRIGDay, 0);
        setIntegerParam(PhotronMemIRIGHour, 0);
        setIntegerParam(PhotronMemIRIGMin, 0);
        setIntegerParam(PhotronMemIRIGSec, 0);
        setIntegerParam(PhotronMemIRIGUsec, 0);
        setIntegerParam(PhotronMemIRIGSigEx, 0);
      }
      
      if (this->pixelBits == 8) {
        // 8 bits
        dataType = NDUInt8;
        pixelSize = 1;
      } else {
        // 12 bits (stored in 2 bytes)
        dataType = NDUInt16;
        pixelSize = 2;
      }
      
      transferBitDepth = 8 * pixelSize;
      dataSize = memWidth * memHeight * pixelSize;
      pBuf = malloc(dataSize);
      
      epicsTimeGetCurrent(&startTime);
      
      //for (index=FrameInfo.m_nTrigger; index==(FrameInfo.m_nTrigger); index++) {
      //for (index=FrameInfo.m_nStart; index<(FrameInfo.m_nEnd+1); index++) {
      for (index=FrameInfo.m_nStart; index<101; index++) {
        // Allow user to abort acquisition
        if (this->abortFlag == 1) {
          printf("Aborting data readout!d\n");
          this->abortFlag = 0;
          break;
        }
        
        // Retrieve a frame
        nRet = PDC_GetMemImageData(this->nDeviceNo, this->nChildNo, index,
                                   transferBitDepth, pBuf, &nErrorCode);
        if (nRet == PDC_FAILED) {
          printf("PDC_GetMemImageData Error %d\n", nErrorCode);
        }
        
        // Retrieve frame time
        if (tMode == 1) {
        
          nRet = PDC_GetMemIRIGData(this->nDeviceNo, this->nChildNo, index,
                                    &tData, &nErrorCode);
          if (nRet == PDC_FAILED) {
            printf("PDC_GetMemIRIGData Error %d\n", nErrorCode);
          }
          
          setIntegerParam(PhotronMemIRIGDay, tData.m_nDayOfYear);
          setIntegerParam(PhotronMemIRIGHour, tData.m_nHour);
          setIntegerParam(PhotronMemIRIGMin, tData.m_nMinute);
          setIntegerParam(PhotronMemIRIGSec, tData.m_nSecond);
          setIntegerParam(PhotronMemIRIGUsec, tData.m_nMicroSecond);
          setIntegerParam(PhotronMemIRIGSigEx, tData.m_ExistSignal);
        }
        
        /* We save the most recent image buffer so it can be used in the read() 
         * function. Now release it before getting a new version. */
        if (this->pArrays[0]) 
          this->pArrays[0]->release();
  
        /* Allocate the raw buffer */
        dims[0] = memWidth;
        dims[1] = memHeight;
        pImage = this->pNDArrayPool->alloc(2, dims, dataType, 0, NULL);
        if (!pImage) {
          asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: error allocating buffer\n", driverName, functionName);
          return(asynError);
        }
  
        memcpy(pImage->pData, pBuf, dataSize);
  
        this->pArrays[0] = pImage;
        pImage->pAttributeList->add("ColorMode", "Color mode", NDAttrInt32, 
                                    &colorMode);
        pImage->getInfo(&arrayInfo);
        setIntegerParam(NDArraySize,  (int)arrayInfo.totalBytes);
        setIntegerParam(NDArraySizeX, (int)pImage->dims[0].size);
        setIntegerParam(NDArraySizeY, (int)pImage->dims[1].size);
        
        /* Call the callbacks to update any changes */
        callParamCallbacks();
      
        /* Get the current parameters */
        getIntegerParam(NDArrayCounter, &imageCounter);
        getIntegerParam(ADNumImages, &numImages);
        getIntegerParam(ADNumImagesCounter, &numImagesCounter);
        getIntegerParam(ADImageMode, &imageMode);
        getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
        imageCounter++;
        numImagesCounter++;
        setIntegerParam(NDArrayCounter, imageCounter);
        setIntegerParam(ADNumImagesCounter, numImagesCounter);

        /* Put the frame number and time stamp into the buffer */
        pImage->uniqueId = imageCounter;
        if (tMode == 1) {
          irigSeconds = (((((tData.m_nDayOfYear * 24) + tData.m_nHour) * 60) + tData.m_nMinute) * 60) + tData.m_nSecond;
          pImage->timeStamp = (this->postIRIGStartTime).secPastEpoch + irigSeconds + (this->postIRIGStartTime).nsec / 1.e9 + tData.m_nMicroSecond / 1.e6;
        }
        else {
          pImage->timeStamp = startTime.secPastEpoch + startTime.nsec / 1.e9;
        }
        updateTimeStamp(&pImage->epicsTS);

        /* Get any attributes that have been defined for this driver */
        this->getAttributes(pImage->pAttributeList);

        if (arrayCallbacks) {
          /* Call the NDArray callback */
          /* Must release the lock here, or we can get into a deadlock, because we
          * can block on the plugin lock, and the plugin can be calling us */
          this->unlock();
          asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: calling imageData callback\n", driverName,
                    functionName);
          doCallbacksGenericPointer(pImage, NDArrayData, 0);
          this->lock();
        }
      }
      
      epicsTimeGetCurrent(&endTime);
      elapsedTime = epicsTimeDiffInSeconds(&endTime, &startTime);
      printf("Elapsed time: %f\n", elapsedTime);
      
      free(pBuf);
      
    } else {
      printf("status != playback; Ignoring read mem\n");
    }
  } else {
    printf("Mode != record; Ignoring read mem\n");
  }
  
  return status;
}


asynStatus Photron::getGeometry() {
  int status = asynSuccess;
  int binX, binY;
  unsigned long minY, minX, sizeX, sizeY;
  static const char *functionName = "getGeometry";

  // Photron cameras don't allow binning
  binX = binY = 1;
  // Assume the reduce resolution images use the upper-left corner of the chip
  minX = minY = 0;
  
  status |= updateResolution();
  
  sizeX = this->width;
  sizeY = this->height;
  
  status |= setIntegerParam(ADBinX,  binX);
  status |= setIntegerParam(ADBinY,  binY);
  status |= setIntegerParam(ADMinX,  minX*binX);
  status |= setIntegerParam(ADMinY,  minY*binY);
  status |= setIntegerParam(ADSizeX, sizeX*binX);
  status |= setIntegerParam(ADSizeY, sizeY*binY);
  status |= setIntegerParam(NDArraySizeX, sizeX);
  status |= setIntegerParam(NDArraySizeY, sizeY);

  if (status)
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d\n", driverName, functionName, status);

  return((asynStatus)status);
}


asynStatus Photron::updateResolution() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  unsigned long sizeX, sizeY;
  unsigned long numSizesX, numSizesY;
  unsigned long width, height, value;
  int index;
  static const char *functionName = "updateResolution";

  // Is this needed or can we trust the values returned by setIntegerParam?
  nRet = PDC_GetResolution(this->nDeviceNo, this->nChildNo, 
                              &sizeX, &sizeY, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetResolution Error %d\n", nErrorCode);
    return asynError;
  }
  
  this->width = sizeX;
  this->height = sizeY;
  
  // We assume the resolution list is up-to-date (it should be updated by 
  // readParameters after the recording rate is modified
  
  // Only changing one dimension that results in another valid mode
  // for the same recording rate will not change the recording rate.
  // Find valid options for the current X and Y sizes
  numSizesX = numSizesY = 0;
  for (index=0; index<this->ResolutionListSize; index++) {
    value = this->ResolutionList[index];
    // height is the lower 16 bits of value
    height = value & 0xFFFF;
    // width is the upper 16 bits of value
    width = value >> 16;
    
    if (sizeX == width) {
      // This mode contains a valid value for Y
      this->ValidHeightList[numSizesY] = height;
      numSizesY++;
    }
    
    if (sizeY == height) {
      // This mode contains a valid value for X
      this->ValidWidthList[numSizesX] = width;
      numSizesX++;
    }
  }
  
  this->ValidWidthListSize = numSizesX;
  this->ValidHeightListSize = numSizesY;
  
  return asynSuccess;
}


asynStatus Photron::setValidWidth(epicsInt32 value) {
  int status = asynSuccess;
  int index;
  epicsInt32 upperDiff, lowerDiff;
  static const char *functionName = "setValidWidth";
  
  // Update the list of valid X and Y sizes (these change with recording rate)
  updateResolution();
  
  if (this->ValidWidthListSize == 0) {
    printf("Error: ValidWidthListSize is ZERO\n");
    return asynError;
  }
  
  if (this->ValidWidthListSize == 1) {
    // Don't allow the value to be changed
    value = this->ValidWidthList[0];
  } else {
    /* Choose the closest allowed width 
       Note: this->ValidWidthList is in decending order */
    for (index=0; index<(this->ValidWidthListSize-1); index++) {
      if (value > this->ValidWidthList[index+1]) {
        upperDiff = (epicsInt32)this->ValidWidthList[index] - value;
        lowerDiff = value - (epicsInt32)this->ValidWidthList[index+1];
        // One of the widths (index or index+1) is the best choice
        if (upperDiff < lowerDiff) {
          printf("Replaced %d ", value);
          value = this->ValidWidthList[index];
          printf("with %d\n", value);
          break;
        } else {
          printf("Replaced %d ", value);
          value = this->ValidWidthList[index+1];
          printf("with %d\n", value);
          break;
        }
      } else {
        // Are we at the end of the list?
        if (index == this->ValidWidthListSize-2) {
          // Value is lower than the lowest rate
          printf("Replaced %d ", value);
          value = this->ValidWidthList[index+1];
          printf("with %d\n", value);
          break;
        } else {
          // We haven't found the closest width yet
          continue;
        }
      }
    }
  }
  
  status |= setIntegerParam(ADSizeX, value);
  status |= setGeometry();
  
  return (asynStatus)status;
}


asynStatus Photron::setValidHeight(epicsInt32 value) {
  int status = asynSuccess;
  int index;
  epicsInt32 upperDiff, lowerDiff;
  static const char *functionName = "setValidHeight";
  
  // Update the list of valid X and Y sizes (these change with recording rate)
  updateResolution();
  
  if (this->ValidHeightListSize == 0) {
    printf("Error: ValidHeightListSize is ZERO\n");
    return asynError;
  }
  
  if (this->ValidHeightListSize == 1) {
    // Don't allow the value to be changed
    value = this->ValidHeightList[0];
  } else {
    /* Choose the closest allowed width 
       Note: this->ValidHeightList is in decending order */
    for (index=0; index<(this->ValidHeightListSize-1); index++) {
      if (value > this->ValidHeightList[index+1]) {
        upperDiff = (epicsInt32)this->ValidHeightList[index] - value;
        lowerDiff = value - (epicsInt32)this->ValidHeightList[index+1];
        // One of the widths (index or index+1) is the best choice
        if (upperDiff < lowerDiff) {
          printf("Replaced %d ", value);
          value = this->ValidHeightList[index];
          printf("with %d\n", value);
          break;
        } else {
          printf("Replaced %d ", value);
          value = this->ValidHeightList[index+1];
          printf("with %d\n", value);
          break;
        }
      } else {
        // Are we at the end of the list?
        if (index == this->ValidHeightListSize-2) {
          // Value is lower than the lowest rate
          printf("Replaced %d ", value);
          value = this->ValidHeightList[index+1];
          printf("with %d\n", value);
          break;
        } else {
          // We haven't found the closest width yet
          continue;
        }
      }
    }
  }
  
  status |= setIntegerParam(ADSizeY, value);
  status |= setGeometry();
  
  return (asynStatus)status;
}


asynStatus Photron::setGeometry() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  int binX, binY, minY, minX, sizeX, sizeY, maxSizeX, maxSizeY;
  static const char *functionName = "setGeometry";
  
  // in the past updateResolution was called here
  
  /* Get all of the current geometry parameters from the parameter library */
  status = getIntegerParam(ADBinX, &binX);
  if (binX < 1)
    binX = 1;
  status = getIntegerParam(ADBinY, &binY);
  if (binY < 1)
    binY = 1;
  status = getIntegerParam(ADMinX, &minX);
  status = getIntegerParam(ADMinY, &minY);
  status = getIntegerParam(ADSizeX, &sizeX);
  status = getIntegerParam(ADSizeY, &sizeY);
  status = getIntegerParam(ADMaxSizeX, &maxSizeX);
  status = getIntegerParam(ADMaxSizeY, &maxSizeY);

  if (minX + sizeX > maxSizeX) {
    sizeX = maxSizeX - minX;
    setIntegerParam(ADSizeX, sizeX);
  }
  if (minY + sizeY > maxSizeY) {
    sizeY = maxSizeY - minY;
    setIntegerParam(ADSizeY, sizeY);
  }
  
  // There are fixed resolutions that can be used
  nRet = PDC_SetResolution(this->nDeviceNo, this->nChildNo, 
                           sizeX, sizeY, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetResolution Error %d\n", nErrorCode);
    return asynError;
  }

  if (status)
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d\n", driverName, functionName, status);

  return((asynStatus)status);
}


asynStatus Photron::setTriggerMode() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  int mode, apiMode, AFrames, RFrames, RCount, maxFrames, acqMode, phostat;
  static const char *functionName = "setTriggerMode";
  
  status |= getIntegerParam(PhotronStatus, &phostat);
  status |= getIntegerParam(ADTriggerMode, &mode);
  status |= getIntegerParam(PhotronAfterFrames, &AFrames);
  status |= getIntegerParam(PhotronRandomFrames, &RFrames);
  status |= getIntegerParam(PhotronRecCount, &RCount);
  status |= getIntegerParam(PhotronMaxFrames, &maxFrames);
  status |= getIntegerParam(PhotronAcquireMode, &acqMode);
  
  // Put the camera in live mode before changing the trigger mode
  if (phostat != PDC_STATUS_LIVE) {
    setLive();
  }
  
  // The mode isn't in the right format for the PDC_SetTriggerMode call
  apiMode = this->trigModeToAPI(mode);
  
  // Set num random frames
  switch (apiMode) {
    case PDC_TRIGGER_RANDOM:
    case PDC_TRIGGER_RANDOM_RESET:
    case PDC_TRIGGER_RANDOM_CENTER:
    case PDC_TRIGGER_RANDOM_MANUAL:
      if (RFrames < 1) {
        RFrames = 1;
      } else if (RFrames > maxFrames) {
        RFrames = maxFrames;
      }
      break;
    default:
      // Non-random modes don't need random frames
      RFrames = 0;
      break;
  }
  
  // Set num after frames
  switch (apiMode) {
    case PDC_TRIGGER_MANUAL:
      if (AFrames < 1) {
        AFrames = 1;
      } else if (AFrames > maxFrames) {
        AFrames = maxFrames;
      }
      break;
    case PDC_TRIGGER_RANDOM_MANUAL:
      if (AFrames < 1) {
        AFrames = 1;
      } else if (AFrames > RFrames) {
        AFrames = RFrames;
      }
      break;
    default:
      AFrames = 0;
      break;
  }
  
  // TODO determine actual limits on RCount
  // PFV software limits num recordings to the following range: 1-10
  switch (apiMode) {
    case PDC_TRIGGER_RANDOM_CENTER:
    case PDC_TRIGGER_RANDOM_MANUAL:
      if (RCount < 1) {
        RCount = 1;
      } else if (RCount > 10) {
        RCount = 10;
      }
      break;
    
    default:
      RCount = 0;
      break;
  }
  
  nRet = PDC_SetTriggerMode(this->nDeviceNo, apiMode, AFrames, RFrames, RCount, 
                            &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetTriggerMode failed %d; apiMode = %x\n", nErrorCode, apiMode);
    return asynError;
  } else {
    printf("\tPDC_SetTriggerMode(-, %x, %d, %d, %d, -)\n", apiMode, AFrames, RFrames, RCount);
  }
  
  // Return camera to rec ready state if in record mode
  if (acqMode == 1) {
    setRecReady();
  }
  
  if (status)
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: error, status=%d\n", driverName, functionName, status);

  return((asynStatus)status);
}


asynStatus Photron::setPixelFormat() {
  int status = asynSuccess;
  int dataType;
  static const char *functionName = "setPixelFormat";
  
  status |= getIntegerParam(NDDataType, &dataType);
  
  if (dataType == NDUInt8) {
    this->pixelBits = 8;
  } else if (dataType == NDUInt16) {
    // The SA1.1 only has a 12-bit sensor
    this->pixelBits = 16;
  } else {
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: error unsupported data type %d\n", 
              driverName, functionName, dataType);
    return asynError;
  }
  
  return asynSuccess;
}


asynStatus Photron::setTransferOption() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  int n8BitSel;
  
  static const char *functionName = "setTransferOption";
  
  status = getIntegerParam(Photron8BitSel, &n8BitSel);
  
  // TODO: confirm that we are in 8-bit acquisition mode, 
  //       otherwise this isn't necessary
  nRet = PDC_SetTransferOption(this->nDeviceNo, this->nChildNo, n8BitSel,
                               PDC_FUNCTION_OFF, PDC_FUNCTION_OFF, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetMaxResolution failed %d\n", nErrorCode);
    return asynError;
  }  
  
  return asynSuccess;
}


asynStatus Photron::setRecordRate(epicsInt32 value) {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  int index;
  epicsInt32 upperDiff, lowerDiff;
  
  static const char *functionName = "setRecordRate";
  
  if (this->RateListSize == 0) {
    printf("Error: RateListSize is ZERO\n");
    return asynError;
  }
  
  if (this->RateListSize == 1) {
    // Don't allow the value to be changed
    value = this->RateList[0];
  } else {
    /* Choose the closest allowed rate 
       NOTE: RateList is in ascending order */
    for (index=0; index<(this->RateListSize-1); index++) {
      if (value < this->RateList[index+1]) {
        upperDiff = (epicsInt32)this->RateList[index+1] - value;
        lowerDiff = value - (epicsInt32)this->RateList[index];
        // One of the rates (index or index+1) is the best choice
        if (upperDiff < lowerDiff) {
          value = this->RateList[index+1];
          break;
        } else {
          value = this->RateList[index];
          break;
        }
      } else {
        // Are we at the end of the list?
        if (index == this->RateListSize-2) {
          // value is higher than the highest rate
          value = this->RateList[index+1];
          break;
        } else {
          // We haven't found the closest rate yet
          continue;
        }
      }
    }
  }
  
  nRet = PDC_SetRecordRate(this->nDeviceNo, this->nChildNo, value, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetRecordRate Error %d\n", nErrorCode);
    return asynError;
  } else {
    printf("PDC_SetRecordRate succeeded. Rate = %d\n", value);
  }
  
  // Changing the record rate changes the current and available resolutions
  
  return asynSuccess;
}


asynStatus Photron::setStatus(epicsInt32 value) {
  unsigned long nRet;
  unsigned long nErrorCode;
  unsigned long desiredStatus;
  int status = asynSuccess;
  static const char *functionName = "setStatus";
  
  /* The Status PV is an mbbo with only two valid states
     The Photron FASTCAM SDK uses a bitmask with seven bits */
  // TODO: simplify this logic since only values of 1 and 0 will be written
  if (value <= 0 || value > 7) {
    desiredStatus = 0;
  } else {
    desiredStatus = 1 << (value - 1);
  }
  
  //printf("Output status = 0x%x\n", desiredStatus);
  nRet = PDC_SetStatus(this->nDeviceNo, desiredStatus, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_SetStatus Error %d\n", nErrorCode);
    return asynError;
  }
  
  return asynSuccess;
}


asynStatus Photron::readParameters() {
  unsigned long nRet;
  unsigned long nErrorCode;
  int status = asynSuccess;
  int tmode;
  int index;
  char bitDepthChar;
  static const char *functionName = "readParameters";    
  
  printf("Reading parameters...\n");
  
  //##############################################################################
  
  nRet = PDC_GetStatus(this->nDeviceNo, &(this->nStatus), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetStatus failed %d\n", nErrorCode);
    return asynError;
  }
  status |= setIntegerParam(PhotronStatus, this->nStatus);
  
  nRet = PDC_GetRecordRate(this->nDeviceNo, this->nChildNo, &(this->nRate), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetRecordRate failed %d\n", nErrorCode);
    return asynError;
  }
  status |= setIntegerParam(PhotronRecRate, this->nRate);
  
  nRet = PDC_GetMaxFrames(this->nDeviceNo, this->nChildNo, &(this->nMaxFrames),
                          &(this->nBlocks), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetMaxFrames failed %d\n", nErrorCode);
    return asynError;
  }
  status |= setIntegerParam(PhotronMaxFrames, this->nMaxFrames);
  
  /*
  PDC_GetTriggerMode succeeded
        Mode = 0
        AFrames = 5457
        RFrames = 0
        RCount = 0
  */
  
  nRet = PDC_GetTriggerMode(this->nDeviceNo, &(this->triggerMode),
                            &(this->trigAFrames), &(this->trigRFrames),
                            &(this->trigRCount), &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetTriggerMode failed %d\n", nErrorCode);
    return asynError;
  }
  
  // The raw trigger mode needs to be converted to the index of the mbbo/mbbi
  tmode = this->trigModeToEPICS(this->triggerMode);
  
  status |= setIntegerParam(ADTriggerMode, tmode);
  status |= setIntegerParam(PhotronAfterFrames, this->trigAFrames);
  status |= setIntegerParam(PhotronRandomFrames, this->trigRFrames);
  status |= setIntegerParam(PhotronRecCount, this->trigRCount);
  
  if (this->functionList[PDC_EXIST_BITDEPTH] == PDC_EXIST_SUPPORTED) {
    nRet = PDC_GetBitDepth(this->nDeviceNo, this->nChildNo, &bitDepthChar,
                           &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetBitDepth failed %d\n", nErrorCode);
      return asynError;
    } else {
      this->bitDepth = (unsigned long) bitDepthChar;
    }
  }
  
  if (this->functionList[PDC_EXIST_IRIG] == PDC_EXIST_SUPPORTED) {
    nRet = PDC_GetIRIG(this->nDeviceNo, &(this->IRIG), &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetIRIG failed %d\n", nErrorCode);
      return asynError;
    }
  } else {
    this->IRIG = 0;
  }
  status |= setIntegerParam(PhotronIRIG, this->IRIG);
  
  //
  if (this->functionList[PDC_EXIST_SYNC_PRIORITY] == PDC_EXIST_SUPPORTED) {
    nRet = PDC_GetSyncPriority(this->nDeviceNo, &(this->syncPriority), &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetSyncPriority failed %d\n", nErrorCode);
      return asynError;
    }
  } else {
    this->syncPriority = 0;
  }
  status |= setIntegerParam(PhotronSyncPriority, this->syncPriority);
  
  for (index=0; index<this->inPorts; index++) {
    nRet = PDC_GetExternalInMode(this->nDeviceNo, index+1, 
                                  &(this->ExtInMode[index]), &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetExternalInMode failed %d; index=%d\n", nErrorCode, index);
      return asynError;
    }
  }
  setIntegerParam(PhotronExtIn1Sig, this->ExtInMode[0]);
  setIntegerParam(PhotronExtIn2Sig, this->ExtInMode[1]);
  setIntegerParam(PhotronExtIn3Sig, this->ExtInMode[2]);
  setIntegerParam(PhotronExtIn4Sig, this->ExtInMode[3]);

  for (index=0; index<this->outPorts; index++) {
    nRet = PDC_GetExternalOutMode(this->nDeviceNo, index+1, 
                                  &(this->ExtOutMode[index]), &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetExternalOutMode failed %d; index=%d\n", nErrorCode, index);
      return asynError;
    }
  }
  setIntegerParam(PhotronExtOut1Sig, this->ExtOutMode[0]);
  setIntegerParam(PhotronExtOut2Sig, this->ExtOutMode[1]);
  setIntegerParam(PhotronExtOut3Sig, this->ExtOutMode[2]);
  setIntegerParam(PhotronExtOut4Sig, this->ExtOutMode[3]);
  
  // Does this ever change?
  nRet = PDC_GetRecordRateList(this->nDeviceNo, this->nChildNo, 
                               &(this->RateListSize), 
                               this->RateList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetRecordRateList failed %d\n", nErrorCode);
    return asynError;
  } 
  
  // Can this be moved to the setRecordRate method? Does anything else effect it?
  nRet = PDC_GetResolutionList(this->nDeviceNo, this->nChildNo, 
                               &(this->ResolutionListSize),
                               this->ResolutionList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetResolutionList failed %d\n", nErrorCode);
    return asynError;
  }
  
  /* SA-Z note: if this isn't called here, the number of modes is incorrect */
  /*nRet = PDC_GetTriggerModeList(this->nDeviceNo, &(this->TriggerModeListSize),
                                this->TriggerModeList, &nErrorCode);
  if (nRet == PDC_FAILED) {
    printf("PDC_GetTriggerModeList failed %d\n", nErrorCode);
    return asynError;
  } else {
    printf("\t!!! num trig modes = %d\n", this->TriggerModeListSize);
  }*/
  
  if (functionList[PDC_EXIST_HIGH_SPEED_MODE] == PDC_EXIST_SUPPORTED) {
    nRet = PDC_GetHighSpeedMode(this->nDeviceNo, &(this->highSpeedMode),
                                &nErrorCode);
    if (nRet == PDC_FAILED) {
      printf("PDC_GetHighSpeedMode failed. Error %d\n", nErrorCode);
      return asynError;
    } 
  }
  
  // getGeometry needs to be called after the resolution list has been updated
  status |= getGeometry();
  
  /* Call the callbacks to update the values in higher layers */
  callParamCallbacks();

  if (status)
    asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: error, status=%d\n", driverName, functionName, status);
  return((asynStatus)status);
}


asynStatus Photron::parseResolutionList() {
  int index;
  unsigned long width, height, value;
  
  printf("  Available resolutions for rate=%d:\n", this->nRate);
  for (index=0; index<this->ResolutionListSize; index++) {
    value = this->ResolutionList[index];
    // height is the lower 16 bits of value
    height = value & 0xFFFF;
    // width is the upper 16 bits of value
    width = value >> 16;
    printf("\t%d\t%d x %d\n", index, width, height);
  }
  
  return asynSuccess;
}


void Photron::printResOptions() {
  int index;
  
  printf("  Valid heights for rate=%d and width=%d\n", this->nRate, this->width);
  for (index=0; index<this->ValidHeightListSize; index++) {
    printf("\t%d\n", this->ValidHeightList[index]);
  }
  
  printf("\n  Valid widths for rate=%d and height=%d\n", this->nRate, this->height);
  for (index=0; index<this->ValidWidthListSize; index++) {
    printf("\t%d\n", this->ValidWidthList[index]);
  }
}


void Photron::printTrigModes() {
  int index;
  int mode;
  
  printf("\n  Trigger Modes:\n");
  for (index=0; index<this->TriggerModeListSize; index++) {
    mode = this->TriggerModeList[index] >> 24;
    if (mode == 8) {
      printf("\t%d:\t%d", index, mode);
      printf("\t%d\n", (this->TriggerModeList[index] & 0xF));
    } else {
      printf("\t%d:\t%d\n", index, mode);
    }
    
  }
}


/** Report status of the driver.
  * Prints details about the driver if details>0.
  * It then calls the ADDriver::report() method.
  * \param[in] fp File pointed passed by caller where the output is written to.
  * \param[in] details If >0 then driver details are printed.
  */
void Photron::report(FILE *fp, int details) {
  int index, jndex;

  fprintf(fp, "Photron detector %s\n", this->portName);
  if (details > 0) {
    // put useful info here
    fprintf(fp, "  Camera Id:         %s\n",  this->cameraId);
    fprintf(fp, "  Auto-detect:       %d\n",  (int)this->autoDetect);
    fprintf(fp, "  Device name:       %s\n",  this->deviceName);
    fprintf(fp, "  Device code:       %d\n",  (int)this->deviceCode);
    if (details > 8) {
      fprintf(fp, "  Device ID:         %d\n",  (int)this->deviceID);
      fprintf(fp, "  Product ID:        %d\n",  (int)this->productID);
      fprintf(fp, "  Lot ID:            %d\n",  (int)this->lotID);
      fprintf(fp, "  Individual ID:     %d\n",  (int)this->individualID);
    }
    fprintf(fp, "  Version:           %0.2f\n",  (float)(this->version/100.0));
    fprintf(fp, "  Sensor width:      %d\n",  (int)this->sensorWidth);
    fprintf(fp, "  Sensor height:     %d\n",  (int)this->sensorHeight);
    fprintf(fp, "  Sensor bits:       %d\n",  (int)this->sensorBits);
    fprintf(fp, "  Max Child Dev #:   %d\n",  (int)this->maxChildDevCount);
    fprintf(fp, "  Child Dev #:       %d\n",  (int)this->childDevCount);
    fprintf(fp, "  In ports:          %d\n",  (int)this->inPorts);
    fprintf(fp, "  Out ports:         %d\n",  (int)this->outPorts);
    fprintf(fp, "\n");
    fprintf(fp, "  Width:             %d\n",  (int)this->width);
    fprintf(fp, "  Height:            %d\n",  (int)this->height);
    fprintf(fp, "  Camera Status:     %d\n",  (int)this->nStatus);
    fprintf(fp, "  Max Frames:        %d\n",  (int)this->nMaxFrames);
    fprintf(fp, "  Record Rate:       %d\n",  (int)this->nRate);
    fprintf(fp, "  Bit Depth:         %d\n",  (int)this->bitDepth);
    fprintf(fp, "\n");
    fprintf(fp, "  Trigger mode:      %x\n",  (int)this->triggerMode);
    fprintf(fp, "    A Frames:        %d\n",  (int)this->trigAFrames);
    fprintf(fp, "    R Frames:        %d\n",  (int)this->trigRFrames);
    fprintf(fp, "    R Count:         %d\n",  (int)this->trigRCount);
    fprintf(fp, "  IRIG:              %d\n",  (int)this->IRIG);
  }
  
  if (details > 4) {
    fprintf(fp, "  Available functions:\n");
    for( index=2; index<98; index++) {
      fprintf(fp, "    %d:         %d\n", index, this->functionList[index]);
    }
  }
  
  if (details > 2) {
    fprintf(fp, "\n  Available recording rates:\n");
    for (index=0; index<this->RateListSize; index++) {
      printf("\t%d:\t%d FPS\n", (index + 1), this->RateList[index]);
    }
    
    fprintf(fp, "\n");
    
    // Turn the resolution list into a more-usable form
    parseResolutionList();
    
    fprintf(fp, "\n");
    
    // 
    printResOptions();
    
    //
    printTrigModes();
  }
  
  if (details > 6) {
    
    fprintf(fp, "\n  External Inputs\n");
    for (index=0; index<this->inPorts; index++) {
      fprintf(fp, "    Port %d (%d modes)\n", index+1, this->ExtInModeListSize[index]);
      for (jndex=0; jndex<this->ExtInModeListSize[index]; jndex++) {
        fprintf(fp, "\t%d:\t0x%02x\n", jndex, this->ExtInModeList[index][jndex]);
      }
    }
    
    fprintf(fp, "\n  External Outputs\n");
    for (index=0; index<this->outPorts; index++) {
      fprintf(fp, "    Port %d (%d modes)\n", index+1, this->ExtOutModeListSize[index]);
      for (jndex=0; jndex<this->ExtOutModeListSize[index]; jndex++) {
        fprintf(fp, "\t%d:\t0x%02x\n", jndex, this->ExtOutModeList[index][jndex]);
      }
    }
    
    if (this->functionList[PDC_EXIST_SYNC_PRIORITY] == PDC_EXIST_SUPPORTED) {
      fprintf(fp, "\n  Sync Priority List:\n");
      for (index=0; index<this->SyncPriorityListSize; index++) {
        fprintf(fp, "\t%d\t%02x\n", index, this->SyncPriorityList[index]);\
      }
    }
  }
  
  if (details > 8) {
    /* Invoke the base class method */
    ADDriver::report(fp, details);
  }
}


/** Configuration command, called directly or from iocsh */
extern "C" int PhotronConfig(const char *portName, const char *ipAddress,
                             int autoDetect, int maxBuffers, int maxMemory,
                             int priority, int stackSize) {
  new Photron(portName, ipAddress, autoDetect,
              (maxBuffers < 0) ? 0 : maxBuffers,
              (maxMemory < 0) ? 0 : maxMemory, 
              priority, stackSize);
  return(asynSuccess);
}

/** Code for iocsh registration */
static const iocshArg PhotronConfigArg0 = {"Port name", iocshArgString};
static const iocshArg PhotronConfigArg1 = {"IP address", iocshArgString};
static const iocshArg PhotronConfigArg2 = {"Auto-detect", iocshArgInt};
static const iocshArg PhotronConfigArg3 = {"maxBuffers", iocshArgInt};
static const iocshArg PhotronConfigArg4 = {"maxMemory", iocshArgInt};
static const iocshArg PhotronConfigArg5 = {"priority", iocshArgInt};
static const iocshArg PhotronConfigArg6 = {"stackSize", iocshArgInt};
static const iocshArg * const PhotronConfigArgs[] =  {&PhotronConfigArg0,
                                                      &PhotronConfigArg1,
                                                      &PhotronConfigArg2,
                                                      &PhotronConfigArg3,
                                                      &PhotronConfigArg4,
                                                      &PhotronConfigArg5,
                                                      &PhotronConfigArg6};
static const iocshFuncDef configPhotron = {"PhotronConfig", 7, 
                                           PhotronConfigArgs};
static void configPhotronCallFunc(const iocshArgBuf *args) {
    PhotronConfig(args[0].sval, args[1].sval, args[2].ival, args[3].ival,
                  args[4].ival, args[5].ival, args[6].ival);
}

static void PhotronRegister(void) {
    iocshRegister(&configPhotron, configPhotronCallFunc);
}

extern "C" {
epicsExportRegistrar(PhotronRegister);
}
