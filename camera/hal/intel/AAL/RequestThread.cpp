/*
 * Copyright (C) 2014-2017 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "RequestThread"

#include "RequestThread.h"
#include "ResultProcessor.h"
#include "CameraMetadataHelper.h"
#include "PlatformData.h"
#include "PerformanceTraces.h"

NAMESPACE_DECLARATION {
/**
 * Stream type value conversion. Android headers are missing this.
 */
const metadata_value_t streamTypeValues[] = {
    { "OUTPUT", CAMERA3_STREAM_OUTPUT },
    { "INPUT", CAMERA3_STREAM_INPUT },
    { "BIDIRECTIONAL", CAMERA3_STREAM_BIDIRECTIONAL }
};

RequestThread::RequestThread(int cameraId, ICameraHw *aCameraHW) :
    MessageThread(this, "Cam3ReqThread"),
    mCameraId(cameraId),
    mCameraHw(aCameraHW),
    mMessageQueue("RequestThread", MESSAGE_ID_MAX),
    mRequestsInHAL(0),
    mFlushing(false),
    mWaitingRequest(nullptr),
    mBlockAction(REQBLK_NONBLOCKING),
    mInitialized(false),
    mResultProcessor(nullptr),
    mStreamSeqNo(0)
{
    LOG1("@%s", __FUNCTION__);
}

RequestThread::~RequestThread()
{
    deinit();
}

status_t
RequestThread::init(const camera3_callback_ops_t *callback_ops)
{
    LOG1("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    status = mRequestsPool.init(MAX_REQUEST_IN_PROCESS_NUM);
    if (status != NO_ERROR) {
        LOGE("Error creating RequestPool: %d", status);
        return status;
    }

    mResultProcessor = new ResultProcessor(this, callback_ops);
    mInitialized = true;
    return NO_ERROR;
}

status_t
RequestThread::deinit()
{
    if (mResultProcessor!= nullptr) {
        mBlockAction = REQBLK_NONBLOCKING;
        mResultProcessor->requestExitAndWait();
        delete mResultProcessor;
        mResultProcessor = nullptr;
    }

    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    mMessageQueue.send(&msg);
    requestExitAndWait();

    // Delete all streams
    for (unsigned int i = 0; i < mLocalStreams.size(); i++) {
        CameraStream *s = mLocalStreams.at(i);
        delete s;
    }

    mStreams.clear();
    mLocalStreams.clear();

    mWaitingRequest = nullptr;
    mBlockAction = REQBLK_NONBLOCKING;
    if (mInitialized) {
        mRequestsPool.deInit();
        mInitialized = false;
    }
    return NO_ERROR;
}

status_t
RequestThread::configureStreams(camera3_stream_configuration_t *stream_list)
{
    Message msg;
    msg.id = MESSAGE_ID_CONFIGURE_STREAMS;
    msg.data.streams.list = stream_list;
    return mMessageQueue.send(&msg, MESSAGE_ID_CONFIGURE_STREAMS);
}

status_t
RequestThread::handleConfigureStreams(Message & msg)
{
    LOG1("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    mLastSettings.clear();
    mWaitingRequest = nullptr;

    uint32_t streamsNum = msg.data.streams.list->num_streams;
    int inStreamsNum = 0;
    int outStreamsNum = 0;
    uint32_t operation_mode = msg.data.streams.list->operation_mode;
    camera3_stream_t *stream = nullptr;
    CameraStream * s = nullptr;
    LOG1("Received %d streams, operation mode %d :", streamsNum, operation_mode);
    // Check number and type of streams
    for (uint32_t i = 0; i < streamsNum; i++) {
        stream = msg.data.streams.list->streams[i];
        LOG1("Config stream (%s): %dx%d, fmt %s, usage %d, max buffers:%d, priv %p",
                METAID2STR(streamTypeValues, stream->stream_type),
                stream->width, stream->height,
                METAID2STR(android_scaler_availableFormats_values, stream->format),
                stream->usage,
                stream->max_buffers, stream->priv);
        if (stream->stream_type == CAMERA3_STREAM_OUTPUT)
            outStreamsNum++;
        else if (stream->stream_type == CAMERA3_STREAM_INPUT)
            inStreamsNum++;
        else if (stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
            inStreamsNum++;
            outStreamsNum++;
        } else {
            LOGE("Unknown stream type %d!", stream->stream_type);
            return BAD_VALUE;
        }
        if (inStreamsNum > 1) {
            LOGE("Too many input streams : %d !", inStreamsNum);
            return BAD_VALUE;
        }
    }

    if (!outStreamsNum) {
        LOGE("No output streams!");
        return BAD_VALUE;
    }

    // Mark all streams as NOT active
    for (unsigned int i = 0; i < mStreams.size(); i++) {
        s = (CameraStream *)(mStreams.at(i)->priv);
        s->setActive(false);
    }

    // Create for new streams
    for (uint32_t i = 0; i < streamsNum; i++) {
        stream = msg.data.streams.list->streams[i];
        if (!stream->priv) {
            mStreams.push_back(stream);
            CameraStream* localStream = new CameraStream(mStreamSeqNo, stream, mResultProcessor);
            mLocalStreams.push_back(localStream);
            localStream->setActive(true);
            stream->priv = localStream;
            mStreamSeqNo++;
        } else {
            static_cast<CameraStream *>(stream->priv)->setActive(true);
        }
     }

    // Delete inactive streams
    deleteStreams(true);

    status = mCameraHw->configStreams(mStreams, operation_mode);
    if (status != NO_ERROR) {
        LOGE("Error configuring the streams @%s:%d", __FUNCTION__, __LINE__);
        // delete all streams
        deleteStreams(false);
        return status;
    }

    std::vector<CameraStreamNode *> activeStreams;
    for (unsigned int i = 0; i < mStreams.size(); i++)
        activeStreams.push_back((CameraStreamNode *)(mStreams.at(i)->priv));

    status = mCameraHw->bindStreams(activeStreams);

    return status;

}

status_t
RequestThread::constructDefaultRequest(int type,
                                            camera_metadata_t** meta)
{
    Message msg;
    msg.id = MESSAGE_ID_CONSTRUCT_DEFAULT_REQUEST;
    msg.data.defaultRequest.type= type;
    msg.data.defaultRequest.request = meta;
    return mMessageQueue.send(&msg, MESSAGE_ID_CONSTRUCT_DEFAULT_REQUEST);
}

status_t
RequestThread::handleConstructDefaultRequest(Message & msg)
{
    LOG2("@%s", __FUNCTION__);
    int requestType = msg.data.defaultRequest.type;
    const camera_metadata_t* defaultRequest;
    defaultRequest = mCameraHw->getDefaultRequestSettings(requestType);
    *(msg.data.defaultRequest.request) = (camera_metadata_t*)defaultRequest;

    return (*(msg.data.defaultRequest.request)) ? NO_ERROR : NO_MEMORY;
}

status_t
RequestThread::processCaptureRequest(camera3_capture_request_t *request)
{
    Message msg;
    msg.id = MESSAGE_ID_PROCESS_CAPTURE_REQUEST;
    msg.data.request3.request3 = request;

    return mMessageQueue.send(&msg, MESSAGE_ID_PROCESS_CAPTURE_REQUEST);
}

// NO_ERROR: request process is OK (waiting for ISP mode change or shutter)
// BAD_VALUE: request is not correct
// else: request process failed due to device error
status_t
RequestThread::handleProcessCaptureRequest(Message & msg)
{
    LOG2("%s:", __FUNCTION__);
    status_t status = BAD_VALUE;

    Camera3Request *request;
    status = mRequestsPool.acquireItem(&request);
    if (status != NO_ERROR) {
        LOGE("Failed to acquire empty  Request from the pool (%d)", status);
        return status;
    }
    // Request counter
    mRequestsInHAL++;
    PERFORMANCE_HAL_ATRACE_PARAM1("mRequestsInHAL", mRequestsInHAL);

    /**
     * Settings may be nullptr in repeating requests but not in the first one
     * check that now.
     */
    if (msg.data.request3.request3->settings) {
        MetadataHelper::dumpMetadata(msg.data.request3.request3->settings);
        // This assignment implies a memcopy.
        // mLastSettings has a copy of the current settings
        mLastSettings = msg.data.request3.request3->settings;
    } else if (mLastSettings.isEmpty()) {
        status = BAD_VALUE;
        LOGE("ERROR: nullptr settings for the first request!");
        goto badRequest;
    }

    status = request->init(msg.data.request3.request3,
                           mResultProcessor,
                           mLastSettings, mCameraId);
    if (status != NO_ERROR) {
        LOGE("Failed to initialize Request (%d)", status);
        goto badRequest;
    }

    // HAL should block user to send this new request when:
    //   1. The count of requests in process reached the PSL capacity.
    //   2. When the request requires reconfiguring the ISP in a manner which
    //      requires stopping the pipeline and emptying the driver from buffers
    //   3. when any of the streams has all buffers in HAL

    // Send for capture
    status = captureRequest(request);
    if (status == REQBLK_WAIT_ALL_PREVIOUS_COMPLETED || status == REQBLK_WAIT_ONE_REQUEST_COMPLETED) {
        // Need ISP reconfiguration
        mWaitingRequest = request;
        mBlockAction = status;
        return NO_ERROR;
    } else if (status != NO_ERROR) {
        status =  UNKNOWN_ERROR;
        goto badRequest;
    }

    if (!areAllStreamsUnderMaxBuffers()) {
        // Request Q is full
        mBlockAction = REQBLK_WAIT_ONE_REQUEST_COMPLETED;
    }
    return NO_ERROR;

badRequest:
    request->deInit();
    mRequestsPool.releaseItem(request);
    mRequestsInHAL--;
    return status;
}

int
RequestThread::returnRequest(Camera3Request* req)
{
    Message msg;
    msg.id = MESSAGE_ID_REQUEST_DONE;
    msg.request = req;
    msg.data.streamOut.reqId = req->getId();
    mMessageQueue.send(&msg);

    return 0;
}

int
RequestThread::handleReturnRequest(Message & msg)
{
    Camera3Request* request = msg.request;
    status_t status = NO_ERROR;

    request->deInit();
    mRequestsPool.releaseItem(request);
    mRequestsInHAL--;
    // Check blocked request
    if (mBlockAction != REQBLK_NONBLOCKING) {
        if (mWaitingRequest != nullptr &&
                ((mBlockAction == REQBLK_WAIT_ONE_REQUEST_COMPLETED) ||
                (mBlockAction == REQBLK_WAIT_ALL_PREVIOUS_COMPLETED && mRequestsInHAL == 1))) {
            status = captureRequest(mWaitingRequest);
            if (status != NO_ERROR) {
                mWaitingRequest->deInit();
                mRequestsPool.releaseItem(mWaitingRequest);
                mRequestsInHAL--;
            }
            mWaitingRequest = nullptr;
        }
        if (mWaitingRequest == nullptr) {
            if (areAllStreamsUnderMaxBuffers()) {
                mBlockAction = REQBLK_NONBLOCKING;
                mMessageQueue.reply(MESSAGE_ID_PROCESS_CAPTURE_REQUEST, status);
            }
        }
    }

    if (mFlushing && !mRequestsInHAL) {
        mMessageQueue.reply(MESSAGE_ID_FLUSH, NO_ERROR);
        mFlushing = false;
    }

    return 0;
}

/**
 * flush
 *  if hal version >= CAMERA_DEVICE_API_VERSION_3_1, we need to support flush()
 *  this is the implement for the dummy flush, it will wait all the request to
 *  finish and then return.
 *  flush() should only return when there are no more outstanding buffers or
 *  requests left in the HAL.
 *  flush() must return within 1000ms
 */
status_t RequestThread::flush(void)
{
    // signal the PSL it should flush requests. PSL are free to complete the
    // results as they want to (with.

    mCameraHw->flush();

    nsecs_t startTime = systemTime();
    nsecs_t interval = 0;

    // wait 1000ms at most while there are requests in the HAL
    while (mRequestsInHAL > 0 && interval / 1000 <= 1000000) {
        usleep(10000); // wait 10ms
        interval = systemTime() - startTime;
    }

    LOG2("@%s, line:%d, mRequestsInHAL:%d, time spend:%" PRId64 "us",
            __FUNCTION__, __LINE__, mRequestsInHAL, interval / 1000);

    nsecs_t intervalTimeout = 1000000;
    if (interval / 1000 > intervalTimeout) {
        LOGE("@%s, the flush() >" PRId64 "ms, time spend:%" PRId64 "us",
            __FUNCTION__, intervalTimeout / 1000, interval / 1000);
        return 0; // TODO: after the performance issue is resolved, change it back to -ENODEV
    }

    return 0;
}

void
RequestThread::messageThreadLoop(void)
{
    LOG2("%s: Start", __func__);
    while (1) {
        status_t status = NO_ERROR;

        Message msg;
        mMessageQueue.receive(&msg);
        PERFORMANCE_HAL_ATRACE_PARAM1("msg", msg.id);
        if (msg.id == MESSAGE_ID_EXIT) {
            if (mBlockAction != REQBLK_NONBLOCKING) {
                mBlockAction = REQBLK_NONBLOCKING;
                LOG1("%s: exit - replying", __FUNCTION__);
                mMessageQueue.reply(MESSAGE_ID_PROCESS_CAPTURE_REQUEST, NO_INIT);
            }
            LOG1("%s: EXIT", __FUNCTION__);
            break;
        }

        if (mFlushing && msg.id != MESSAGE_ID_REQUEST_DONE) {
             mMessageQueue.reply(msg.id, INVALID_OPERATION);
        }

        LOG2("@%s, receive message id:%d", __FUNCTION__, msg.id);
        bool replyImmediately = true;
        switch (msg.id) {
        case MESSAGE_ID_CONFIGURE_STREAMS:
            status = handleConfigureStreams(msg);
            break;
        case MESSAGE_ID_CONSTRUCT_DEFAULT_REQUEST:
            status = handleConstructDefaultRequest(msg);
            break;
        case MESSAGE_ID_PROCESS_CAPTURE_REQUEST:
            status = handleProcessCaptureRequest(msg);
            replyImmediately = (mBlockAction == REQBLK_NONBLOCKING);
            break;
        case MESSAGE_ID_REQUEST_DONE:
            status = handleReturnRequest(msg);
            break;
        case MESSAGE_ID_FLUSH:
            break;
        default:
            LOGE("ERROR @%s: Unknow message %d", __FUNCTION__, msg.id);
            status = BAD_VALUE;
            break;
        }
        if (status != NO_ERROR)
            LOGE("    error %d in handling message: %d", status, (int)msg.id);

        LOG2("@%s, finish message id:%d", __FUNCTION__, msg.id);

        if (replyImmediately)
            mMessageQueue.reply(msg.id, status);

    }

    LOG2("%s: Exit", __FUNCTION__);
}

status_t
RequestThread::captureRequest(Camera3Request* request)
{
    status_t status;
    CameraStream *stream = nullptr;

    status = mResultProcessor->registerRequest(request);
    if (status != NO_ERROR) {
        LOGE("Error registering request to result Processor- bug");
        return status;
    }

    status = mCameraHw->processRequest(request,mRequestsInHAL);
    if (status == REQBLK_WAIT_ALL_PREVIOUS_COMPLETED
        || status == REQBLK_WAIT_ONE_REQUEST_COMPLETED) {
        return status;
    }

    // handle output buffers

    const std::vector<CameraStreamNode*>* outStreams = request->getOutputStreams();
    if (CC_UNLIKELY(outStreams == nullptr)) {
        LOGE("there is no output streams. this should not happen");
        return BAD_VALUE;
    }
    CameraStreamNode* streamNode = nullptr;
    for (unsigned int i = 0; i < outStreams->size(); i++) {
        streamNode = outStreams->at(i);
        stream = static_cast<CameraStream *>(streamNode);
        stream->processRequest(request);
    }

    const std::vector<CameraStreamNode*>* inStreams = request->getInputStreams();
    if (inStreams != nullptr) {
        for (unsigned int i = 0; i < inStreams->size(); i++) {
            streamNode = inStreams->at(i);
            stream = static_cast<CameraStream *>(streamNode);
            status = stream->processRequest(request);
            if (status != NO_ERROR) {
                LOGE("%s, fail to process stream request", __FUNCTION__);
                break;
             }
        }
    }

    return status;
}

bool RequestThread::areAllStreamsUnderMaxBuffers() const
{
    std::vector<CameraStream*>::const_iterator it = mLocalStreams.begin();
    while (it != mLocalStreams.end()) {
        if ((*it)->outBuffersInHal() ==
            (int32_t) (*it)->getStream()->max_buffers)
            return false;

        ++it;
    }
    return true;
}

void RequestThread::deleteStreams(bool inactiveOnly)
{
    CameraStream *s = nullptr;

    unsigned int i = 0;
    while (i < mStreams.size()) {
        s = static_cast<CameraStream*>(mStreams.at(i)->priv);

        if (!inactiveOnly || !s->isActive()) {
            delete s;
            mStreams.at(i)->priv = nullptr;
            mLocalStreams.erase(mLocalStreams.begin() + i);
            mStreams.erase(mStreams.begin() + i);
        } else {
            ++i;
        }
    }
}

void RequestThread::dump(int fd)
{
    LOG2("@%s", __FUNCTION__);
}

} NAMESPACE_DECLARATION_END
