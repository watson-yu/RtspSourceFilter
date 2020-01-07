#include "stdafx.h"
#include "RtspManager.h"
#include "MyUsageEnvironment.h"
#include "MyTaskScheduler.h"
#include "GroupsockHelper.hh"
#include "Trace.h"
#include "openRTSP.h"
#include "ProxyMediaSink.h"

void continueAfterOPTIONS(RTSPClient*, int resultCode, char* resultString);
void continueAfterDESCRIBE(RTSPClient*, int resultCode, char* resultString);
void setupStreams();
void continueAfterPLAY(RTSPClient*, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* client, int resultCode, char* resultString);
void beginQOSMeasurement();

Authenticator* ourAuthenticator = NULL;
portNumBits tunnelOverHTTPPortNum = 0;
MyUsageEnvironment* env = NULL;
MediaSession* session = NULL;
unsigned short desiredPortNum = 0;
Boolean createReceivers = True;
int simpleRTPoffsetArg = -1;
unsigned socketInputBufferSize = 0;
unsigned fileSinkBufferSize = 100000;
Boolean streamUsingTCP = False;
Boolean forceMulticastOnUnspecified = False;
MediaSubsession* subsession;
Boolean madeProgress = False;
unsigned fileOutputInterval = 0; // seconds
double duration = 0;
double initialSeekTime = 0.0f;
char* initialAbsoluteSeekTime = NULL;
char* initialAbsoluteSeekEndTime = NULL;
float scale = 1.0f;
double endTime;
unsigned sessionTimeoutParameter = 0;
unsigned qosMeasurementIntervalMS = 0; // 0 means: Don't output QOS data
double durationSlop = -1.0; // extra seconds to play at the end
TaskToken sessionTimerTask = NULL;
MediaPacketQueue _videoMediaQueue;
const int recvBufferVideo = 256 * 1024; // 256KB - H.264 IDR frames can be really big
MyTaskScheduler* taskScheduler = NULL;
MediaPacketSample mediaSample;

RtspManager::RtspManager() {
	m_frameBuffer = (FrameBuffer*) malloc(sizeof(FrameBuffer));
	m_frameBuffer->FrameType = 2;
	m_frameBuffer->TimeStamp = 0;
	m_frameBuffer->pData = NULL;
	m_streamUrl = NULL;
}

RtspManager::~RtspManager() {
}

FrameBuffer* RtspManager::getFrameBuffer() {
	if (!_videoMediaQueue.empty()) {
		_videoMediaQueue.pop(mediaSample);
	}

	if (!mediaSample.invalid()) {
		size_t size = mediaSample.size();
		unsigned char* buffer = mediaSample.data();

		m_frameBuffer->FrameLen = size;
		if (m_frameBuffer->pData == NULL) m_frameBuffer->pData = (unsigned char*) malloc(size);
		memcpy_s(m_frameBuffer->pData, size, buffer, size);

		return m_frameBuffer;
	}
	return NULL;
}

void RtspManager::doSingleStep() {
	if (taskScheduler != NULL) taskScheduler->doSingleStep(100);
}

void RtspManager::start(char* streamUrl) {
	m_streamUrl = streamUrl;
	//const char* streamURL = "rtsp://172.20.10.2/pusher";
	int verbosityLevel = 1;
	const char* progName = "VirtualCamFilter";

	taskScheduler = MyTaskScheduler::createNew();
	env = MyUsageEnvironment::createNew(*taskScheduler);

	Medium* medium = createClient(*env, m_streamUrl, verbosityLevel, progName);
	TRACE_DEBUG("medium=0x%x", medium);

	if (medium == NULL) {
		//
	}

	const char* userAgent = "";
	setUserAgentString(userAgent);
	getOptions(continueAfterOPTIONS);

	taskScheduler->doSingleStep(100);// doEventLoop();
}

void shutdown(int exitCode = 1) {
	/*
	if (areAlreadyShuttingDown) return; // in case we're called after receiving a RTCP "BYE" while in the middle of a "TEARDOWN".
	areAlreadyShuttingDown = True;

	shutdownExitCode = exitCode;
	if (env != NULL) {
		env->taskScheduler().unscheduleDelayedTask(periodicFileOutputTask);
		env->taskScheduler().unscheduleDelayedTask(sessionTimerTask);
		env->taskScheduler().unscheduleDelayedTask(sessionTimeoutBrokenServerTask);
		env->taskScheduler().unscheduleDelayedTask(arrivalCheckTimerTask);
		env->taskScheduler().unscheduleDelayedTask(interPacketGapCheckTimerTask);
		env->taskScheduler().unscheduleDelayedTask(qosMeasurementTimerTask);
	}

	if (qosMeasurementIntervalMS > 0) {
		printQOSData(exitCode);
	}

	// Teardown, then shutdown, any outstanding RTP/RTCP subsessions
	Boolean shutdownImmediately = True; // by default
	if (session != NULL) {
		RTSPClient::responseHandler* responseHandlerForTEARDOWN = NULL; // unless:
		if (waitForResponseToTEARDOWN) {
			shutdownImmediately = False;
			responseHandlerForTEARDOWN = continueAfterTEARDOWN;
		}
		tearDownSession(session, responseHandlerForTEARDOWN);
	}

	if (shutdownImmediately) continueAfterTEARDOWN(NULL, 0, NULL);
	*/
}

void continueAfterOPTIONS(RTSPClient*, int resultCode, char* resultString) {
	TRACE_DEBUG("rc=%d, res=%s", resultCode, resultString);
	delete[] resultString;
	if (resultCode != 0) {
		shutdown();
		return;
	}
	getSDPDescription(continueAfterDESCRIBE);
}

void continueAfterDESCRIBE(RTSPClient*, int resultCode, char* resultString) {
	TRACE_DEBUG("rc=%d, res=%s", resultCode, resultString);
	if (resultCode != 0) {
		shutdown();
	}

	char* sdpDescription = resultString;

	// Create a media session object from this SDP description:
	session = MediaSession::createNew(*env, sdpDescription);
	delete[] sdpDescription;

	TRACE_DEBUG("session=0x%x", session);
	if (session == NULL) {
		shutdown();
	}
	else if (!session->hasSubsessions()) {
		TRACE_DEBUG("No subsessions");
		shutdown();
	}

	// Then, setup the "RTPSource"s for the session:
	MediaSubsessionIterator iter(*session);
	Boolean madeProgress = False;
	char const* singleMediumToTest = NULL;
	while ((subsession = iter.next()) != NULL) {
		// If we've asked to receive only a single medium, then check this now:
		if (singleMediumToTest != NULL) {
			if (strcmp(subsession->mediumName(), singleMediumToTest) != 0) {
				TRACE_DEBUG("ignore subsession: %s", subsession->mediumName());
				continue;
			}
			else {
				// Receive this subsession only
				singleMediumToTest = "xxxxx";
				// this hack ensures that we get only 1 subsession of this type
			}
		}

		if (desiredPortNum != 0) {
			subsession->setClientPortNum(desiredPortNum);
			desiredPortNum += 2;
		}

		if (createReceivers) {
			if (!subsession->initiate(simpleRTPoffsetArg)) {
				*env << "Unable to create receiver for \"" << subsession->mediumName()
					<< "/" << subsession->codecName()
					<< "\" subsession: " << env->getResultMsg() << "\n";
			}
			else {
				*env << "Created receiver for \"" << subsession->mediumName()
					<< "/" << subsession->codecName() << "\" subsession (";
				if (TRUE) {//subsession->rtcpIsMuxed()) {
					*env << "client port " << subsession->clientPortNum();
				}
				else {
					*env << "client ports " << subsession->clientPortNum()
						<< "-" << subsession->clientPortNum() + 1;
				}
				*env << ")\n";
				madeProgress = True;

				if (subsession->rtpSource() != NULL) {
					// Because we're saving the incoming data, rather than playing
					// it in real time, allow an especially large time threshold
					// (1 second) for reordering misordered incoming packets:
					unsigned const thresh = 1000000; // 1 second
					subsession->rtpSource()->setPacketReorderingThresholdTime(thresh);

					// Set the RTP source's OS socket buffer size as appropriate - either if we were explicitly asked (using -B),
					// or if the desired FileSink buffer size happens to be larger than the current OS socket buffer size.
					// (The latter case is a heuristic, on the assumption that if the user asked for a large FileSink buffer size,
					// then the input data rate may be large enough to justify increasing the OS socket buffer size also.)
					int socketNum = subsession->rtpSource()->RTPgs()->socketNum();
					unsigned curBufferSize = getReceiveBufferSize(*env, socketNum);
					if (socketInputBufferSize > 0 || fileSinkBufferSize > curBufferSize) {
						unsigned newBufferSize = socketInputBufferSize > 0 ? socketInputBufferSize : fileSinkBufferSize;
						newBufferSize = setReceiveBufferTo(*env, socketNum, newBufferSize);
						if (socketInputBufferSize > 0) { // The user explicitly asked for the new socket buffer size; announce it:
							*env << "Changed socket receive buffer size for the \""
								<< subsession->mediumName()
								<< "/" << subsession->codecName()
								<< "\" subsession from "
								<< curBufferSize << " to "
								<< newBufferSize << " bytes\n";
						}
					}
				}
			}
		}
		else {
			if (subsession->clientPortNum() == 0) {
				*env << "No client port was specified for the \""
					<< subsession->mediumName()
					<< "/" << subsession->codecName()
					<< "\" subsession.  (Try adding the \"-p <portNum>\" option.)\n";
			}
			else {
				madeProgress = True;
			}
		}
	}
	if (!madeProgress) shutdown();

	// Perform additional 'setup' on each subsession, before playing them:
	setupStreams();
}

void subsessionAfterPlaying(void* clientData) {
	// Begin by closing this media subsession's stream:
	MediaSubsession* subsession = (MediaSubsession*)clientData;
	Medium::close(subsession->sink);
	subsession->sink = NULL;

	// Next, check whether *all* subsessions' streams have now been closed:
	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) return; // this subsession is still active
	}

	// All subsessions' streams have now been closed
	//sessionAfterPlaying();
}

void createOutputs() {
	MediaSubsessionIterator iter(*session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->readSource() == NULL) continue; // was not initiated

		if (strcmp(subsession->mediumName(), "video") == 0) {
			if (strcmp(subsession->codecName(), "H264") == 0) {
				// For H.264 video stream, we use a special sink that adds 'start codes',
				// and (at the start) the SPS and PPS NAL units:
				subsession->sink = new ProxyMediaSink(*env, *subsession,
					_videoMediaQueue, recvBufferVideo);
				subsession->sink->startPlaying(*(subsession->readSource()),
					subsessionAfterPlaying,
					subsession);
			}
		}

		// Also set a handler to be called if a RTCP "BYE" arrives
		// for this subsession:
		if (subsession->rtcpInstance() != NULL) {
			//subsession->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler, subsession);
		}
	}
}

void setupStreams() {
	static MediaSubsessionIterator* setupIter = NULL;
	if (setupIter == NULL) setupIter = new MediaSubsessionIterator(*session);
	while ((subsession = setupIter->next()) != NULL) {
		TRACE_DEBUG("subsession: %s", subsession->mediumName());
		// We have another subsession left to set up:
		if (subsession->clientPortNum() == 0) continue; // port # was not set
		setupSubsession(subsession, streamUsingTCP, forceMulticastOnUnspecified, continueAfterSETUP);
		return;
	}

	// We're done setting up subsessions.
	delete setupIter;
	if (!madeProgress) shutdown();

	// Create output files:
	if (createReceivers) {
		createOutputs();
		if (fileOutputInterval > 0) {
			//createPeriodicOutputFiles();
		}
		else {
			//createOutputFiles("");
		}
	}

	// Finally, start playing each subsession, to start the data flow:
	if (duration == 0) {
		if (scale > 0) duration = session->playEndTime() - initialSeekTime; // use SDP end time
		else if (scale < 0) duration = initialSeekTime;
	}
	if (duration < 0) duration = 0.0;

	endTime = initialSeekTime;
	if (scale > 0) {
		if (duration <= 0) endTime = -1.0f;
		else endTime = initialSeekTime + duration;
	}
	else {
		endTime = initialSeekTime - duration;
		if (endTime < 0) endTime = 0.0f;
	}

	char const* absStartTime = initialAbsoluteSeekTime != NULL ? initialAbsoluteSeekTime : session->absStartTime();
	char const* absEndTime = initialAbsoluteSeekEndTime != NULL ? initialAbsoluteSeekEndTime : session->absEndTime();
	if (absStartTime != NULL) {
		// Either we or the server have specified that seeking should be done by 'absolute' time:
		startPlayingSession(session, absStartTime, absEndTime, scale, continueAfterPLAY);
	}
	else {
		// Normal case: Seek by relative time (NPT):
		startPlayingSession(session, initialSeekTime, endTime, scale, continueAfterPLAY);
	}
}

void continueAfterSETUP(RTSPClient* client, int resultCode, char* resultString) {
	TRACE_DEBUG("rc=%d, res=%s", resultCode, resultString);
	if (resultCode == 0) {
		*env << "Setup \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession (";
		if (TRUE) {//subsession->rtcpIsMuxed()) {
			*env << "client port " << subsession->clientPortNum();
		}
		else {
			*env << "client ports " << subsession->clientPortNum()
				<< "-" << subsession->clientPortNum() + 1;
		}
		*env << ")\n";
		madeProgress = True;
	}
	else {
		*env << "Failed to setup \"" << subsession->mediumName()
			<< "/" << subsession->codecName()
			<< "\" subsession: " << resultString << "\n";
	}
	delete[] resultString;

	if (client != NULL) sessionTimeoutParameter = client->sessionTimeoutParameter();

	// Set up the next subsession, if any:
	setupStreams();
}

void sessionTimerHandler(void* /*clientData*/) {
	sessionTimerTask = NULL;

	//sessionAfterPlaying();
}

void beginQOSMeasurement() {
}

void continueAfterPLAY(RTSPClient*, int resultCode, char* resultString) {
	TRACE_DEBUG("rc=%d, res=%s", resultCode, resultString);
	if (resultCode != 0) {
		*env << "Failed to start playing session: " << resultString << "\n";
		delete[] resultString;
		shutdown();
		return;
	}
	else {
		*env << "Started playing session\n";
	}
	delete[] resultString;

	if (qosMeasurementIntervalMS > 0) {
		// Begin periodic QOS measurements:
		beginQOSMeasurement();
	}

	// Figure out how long to delay (if at all) before shutting down, or
	// repeating the playing
	Boolean timerIsBeingUsed = False;
	double secondsToDelay = duration;
	if (duration > 0) {
		// First, adjust "duration" based on any change to the play range (that was specified in the "PLAY" response):
		double rangeAdjustment = (session->playEndTime() - session->playStartTime()) - (endTime - initialSeekTime);
		if (duration + rangeAdjustment > 0.0) duration += rangeAdjustment;

		timerIsBeingUsed = True;
		double absScale = scale > 0 ? scale : -scale; // ASSERT: scale != 0
		secondsToDelay = duration / absScale + durationSlop;

		int64_t uSecsToDelay = (int64_t)(secondsToDelay * 1000000.0);
		sessionTimerTask = env->taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*)sessionTimerHandler, (void*)NULL);
	}

	char const* actionString
		= createReceivers ? "Receiving streamed data" : "Data is being streamed";
	if (timerIsBeingUsed) {
		*env << actionString
			<< " (for up to " << secondsToDelay
			<< " seconds)...\n";
	}
	else {
#ifdef USE_SIGNALS
		pid_t ourPid = getpid();
		*env << actionString
			<< " (signal with \"kill -HUP " << (int)ourPid
			<< "\" or \"kill -USR1 " << (int)ourPid
			<< "\" to terminate)...\n";
#else
		* env << actionString << "...\n";
#endif
	}
	/*
	sessionTimeoutBrokenServerTask = NULL;

	// Watch for incoming packets (if desired):
	checkForPacketArrival(NULL);
	checkInterPacketGaps(NULL);
	checkSessionTimeoutBrokenServer(NULL);
	*/
}
