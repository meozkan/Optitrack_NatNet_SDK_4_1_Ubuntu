#include "Optitrack.h"


bool Optitrack::isStop=false;
bool Optitrack::isStart=false;

// NatNet Callbacks
//void NATNET_CALLCONV ServerDiscoveredCallback(const sNatNetDiscoveredServer* pDiscoveredServer, void* pUserContext);
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData);    // receives data from the server
void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg);      // receives NatNet error messages

// Frame Queue
typedef struct MocapFrameWrapper
{
    shared_ptr<sFrameOfMocapData> data;
    double transitLatencyMillisec;
    double clientLatencyMillisec;
} MocapFrameWrapper;
std::timed_mutex gNetworkQueueMutex;
//std::deque<MocapFrameWrapper> gNetworkQueue;
//const int kMaxQueueSize = 500;
MocapFrameWrapper gNetworkFrame;


Optitrack::Optitrack(std::string server_ip, std::string local_ip, const std::string _filename)
    : g_pClient(NULL), Server_IP(server_ip), Local_IP(local_ip), g_pDataDefs(NULL), fileName(_filename), pollPeriodSpan((unsigned)200u)  
{
    // Install logging callback
    NatNet_SetLogCallback( MessageHandler );

    // Create NatNet client
    g_pClient = new NatNetClient();

    // Manually specify Motive server IP/connection type
    g_connectParams.connectionType = ConnectionType_Multicast;
    g_connectParams.localAddress = Local_IP.c_str();
    g_connectParams.serverAddress = Server_IP.c_str();
    g_connectParams.serverCommandPort = 1510;
    g_connectParams.serverDataPort = 1511;
    char g_discoveredMulticastGroupAddr[kNatNetIpv4AddrStrLenMax] = NATNET_DEFAULT_MULTICAST_ADDRESS;
    g_connectParams.multicastAddress = g_discoveredMulticastGroupAddr;

}

bool Optitrack::connect()
{
    // Print NatNet client version info
    unsigned char ver[4];
    NatNet_GetVersion( ver );
    printf( "NatNet Sample Client (NatNet ver. %d.%d.%d.%d)\n", ver[0], ver[1], ver[2], ver[3] );
    
    // Set the frame callback handler
    g_pClient->SetFrameReceivedCallback( DataHandler, g_pClient );	// this function will receive data from the server

    // Connect to Motive
    int iResult = ConnectClient();
    if (iResult != ErrorCode_OK){
        printf("Error initializing client. See log for details. Exiting.\n");
        return false;
    }
    else
    {
        printf("Client initialized and ready.\n");
    }

    // Get latest asset list from Motive
     if (!UpdateDataDescriptions(true)){
         printf("ERROR : Unable to retrieve Data Descriptions from Motive.\n");
         return false;
     }

     //-------------------------
    //Configure the signal
    fd_set readfds;
    struct sigaction sa;

    sa.sa_handler = Optitrack::SignalHandler;     /* Establish signal handler */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);


    //Open File for records
    fileRecorder=new std::ofstream(fileName); 
    if (!fileRecorder->is_open()){
        throw std::runtime_error("Could not open file for writing: " + fileName);
        return false;
    }
    else{
        /// the timestamp in device format (date/time format).
        /// Bits of the devices timestamp:
        ///   - 5 unused
        ///   - 12 Year
        ///   - 4 Month
        ///   - 5 Day
        ///   - 11 Timezone
        ///   - 5 Hour
        ///   - 6 Minute
        ///   - 6 Seconds
        ///   - 10 Milliseconds
        /// .....YYYYYYYYYYYYMMMMDDDDDTTTTTTTTTTTHHHHHMMMMMMSSSSSSmmmmmmmmmm
        /// the timestampMS in milliseconds (UTC).
        (*fileRecorder) << "frameNumber, " << "systemtime_ms, " << "device_timestamp, " << "device_timecode, " << "Motive_SW_Latency(ms), " << "Transit_Latency(ms), "
                        << "mean_error(mm), " << "X(m), " << "Y(m), " << "Z(m), " << "QX(rad), " << "QY(rad), " << "QZ(rad), " << "QW(rad)" << '\n';

    }

    return true;
}

bool Optitrack::run()
{

    int64_t prevtimePeriodForTimeStamp=0;

    // acquire a single snapshot
    while(!isStop) {
        if(isStart){
            // make sure that capture is implemented periodically
            auto lastSnapTime = std::chrono::steady_clock::now();
            
            
            // If Motive Asset list has changed, update our lookup maps
            UpdateDataDescriptions(false);

            // Add data from the network frame into our record frame in order to quickly
            // free up access to the network frame.
            MocapFrameWrapper recordFrame;
            //std::deque<MocapFrameWrapper> displayQueue;
    
            if (gNetworkQueueMutex.try_lock_for(std::chrono::milliseconds(5))){
                recordFrame=gNetworkFrame;
                gNetworkQueueMutex.unlock();
            }


            // Now we can take our time displaying our data without
            // worrying about interfering with the network processing queue.
            sFrameOfMocapData* data = recordFrame.data.get();


            if (fileRecorder->is_open()){
    
                (*fileRecorder)<<data->iFrame<<", ";  //Record FrameNumber
                
                // Get the current time from the system clock
                auto now = std::chrono::system_clock::now();
                // Convert the current time to time since epoch
                auto duration = now.time_since_epoch();
                // Convert duration to milliseconds
                auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                (*fileRecorder)<<milliseconds<<", "; //Record system time ms

                (*fileRecorder)<<data->fTimestamp<<", "; //Record device_timestamp

                // timecode - for systems with an eSync and SMPTE timecode generator - decode to values
                int hour, minute, second, frame, subframe;
                NatNet_DecodeTimecode(data->Timecode, data->TimecodeSubframe, &hour, &minute, &second, &frame, &subframe);
                char szTimecode[128] = "";
                NatNet_TimecodeStringify(data->Timecode, data->TimecodeSubframe, szTimecode, 128);

                (*fileRecorder)<<szTimecode<<", "; //Record device_timecode

                // Latency Metrics
                // 
                // Software latency here is defined as the span of time between:
                //   a) The reception of a complete group of 2D frames from the camera system (CameraDataReceivedTimestamp)
                // and
                //   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
                //
                // This figure may appear slightly higher than the "software latency" reported in the Motive user interface,
                // because it additionally includes the time spent preparing to stream the data via NatNet.
                const uint64_t softwareLatencyHostTicks = data->TransmitTimestamp - data->CameraDataReceivedTimestamp;
                const double softwareLatencyMillisec = (softwareLatencyHostTicks * 1000) / static_cast<double>(g_serverDescription.HighResClockFrequency);
                (*fileRecorder)<<softwareLatencyMillisec<<", "; //Record Motive_SW_Latency(ms)
               
                (*fileRecorder)<<recordFrame.transitLatencyMillisec<<", "; //Record Motive_SW_Latency(ms)

                (*fileRecorder)<<data->RigidBodies[0].MeanError*1000.0f<<", "; //Record Mean error(mm)

                (*fileRecorder)<<data->RigidBodies[0].x<<", "; //Record X(rad)
                (*fileRecorder)<<data->RigidBodies[0].y<<", "; //Record Y(rad)
                (*fileRecorder)<<data->RigidBodies[0].z<<", "; //Record Z(rad)
                (*fileRecorder)<<data->RigidBodies[0].qx<<", "; //Record QX(rad)
                (*fileRecorder)<<data->RigidBodies[0].qy<<", "; //Record QY(rad)
                (*fileRecorder)<<data->RigidBodies[0].qz<<", "; //Record QZ(rad)
                (*fileRecorder)<<data->RigidBodies[0].qw<<", "; //Record QW(rad)

            }

            //Wait for keeping the period
            const auto timeSinceLastSnap = std::chrono::steady_clock::now() - lastSnapTime;

            if (timeSinceLastSnap < pollPeriodSpan){
                auto timeToWait = pollPeriodSpan - timeSinceLastSnap;
                std::this_thread::sleep_for(timeToWait);
            }

            NatNet_FreeFrame(recordFrame.data.get());
            
        } //if(isStart){
    } //while(!isStop) {

    return true;

}

bool Optitrack::disConnect()
{
    // Exiting - clean up
	if (g_pClient)
	{
		g_pClient->Disconnect();
		delete g_pClient;
		g_pClient = NULL;
	}

    if (g_pDataDefs)
    {
        NatNet_FreeDescriptions(g_pDataDefs);
        g_pDataDefs = NULL;
    }

    //Close File for records
    if (!(*fileRecorder)){
        throw std::runtime_error("Error writing to file: " + fileName);
        return false;
    }
    fileRecorder->close();
    delete fileRecorder;

    return true;
}

void Optitrack::SignalHandler(int sig)
{
    if(sig==SIGINT)
        isStop=true;
    if(sig==SIGUSR1)
        isStart=true;
}

//--------------------------------------------------
// NatNet Sample Client functions
//--------------------------------------------------
 
int Optitrack::ConnectClient()
{
    // Disconnect from any previous server (if connected)
    g_pClient->Disconnect();

    // Connect to NatNet server (e.g. Motive)
    int retCode = g_pClient->Connect( g_connectParams );
    if (retCode != ErrorCode_OK){
        // Connection failed - print connection error code
        printf("[SampleClinet] Unable to connect to server.  Error code: %d. Exiting.\n", retCode);
        return ErrorCode_Internal;
    }
    else{
        // Connection succeeded
        void* pResult;
        int nBytes = 0;
        ErrorCode ret = ErrorCode_OK;
    
        // example : print server info
        memset(&g_serverDescription, 0, sizeof(g_serverDescription));
        ret = g_pClient->GetServerDescription(&g_serverDescription);
        if (ret != ErrorCode_OK || !g_serverDescription.HostPresent){
            printf("[SampleClient] Unable to connect to server. Host not present. Exiting.\n");
            return 1;
        }
        printf("\n[SampleClient] Server application info:\n");
        printf("Application: %s (ver. %d.%d.%d.%d)\n", g_serverDescription.szHostApp, g_serverDescription.HostAppVersion[0],
        g_serverDescription.HostAppVersion[1], g_serverDescription.HostAppVersion[2], g_serverDescription.HostAppVersion[3]);
        printf("NatNet Version: %d.%d.%d.%d\n", g_serverDescription.NatNetVersion[0], g_serverDescription.NatNetVersion[1],
            g_serverDescription.NatNetVersion[2], g_serverDescription.NatNetVersion[3]);
        printf("Client IP:%s\n", g_connectParams.localAddress);
        printf("Server IP:%s\n", g_connectParams.serverAddress);
        printf("Server Name:%s\n", g_serverDescription.szHostComputerName);
    
        // example : get mocap frame rate
        ret = g_pClient->SendMessageAndWait("FrameRate", &pResult, &nBytes);
        if (ret == ErrorCode_OK){
            float fRate = *((float*)pResult);
            printf("Mocap Framerate : %3.2f\n", fRate);
        }
        else{
            printf("Error getting frame rate.\n");
        }
    
    }
    
    return ErrorCode_OK;
}

bool Optitrack::UpdateDataDescriptions(bool printToConsole)
{
    // release memory allocated by previous in previous GetDataDescriptionList()
    if (g_pDataDefs){
        NatNet_FreeDescriptions(g_pDataDefs);
    }

    // Retrieve Data Descriptions from Motive
    printf("\n\n[SampleClient] Requesting Data Descriptions...\n");
    int iResult = g_pClient->GetDataDescriptionList(&g_pDataDefs);
    if (iResult != ErrorCode_OK || g_pDataDefs == NULL){
        return false;
    }
    //else{
    //    if (printToConsole)
    //   {
    //        PrintDataDescriptions(g_pDataDefs);
    //    }
    //}
    UpdateDataToDescriptionMaps(g_pDataDefs);

    return true;

}


void Optitrack::UpdateDataToDescriptionMaps(sDataDescriptions* pDataDefs)
{
    g_AssetIDtoAssetDescriptionOrder.clear();
    g_AssetIDtoAssetName.clear();
    int assetID = 0;
    std::string assetName = "";
    int index = 0;
    int cameraIndex = 0;

    if (pDataDefs == nullptr || pDataDefs->nDataDescriptions <= 0)
        return;

    for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        assetID = -1;
        assetName = "";

        if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody){
            sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
            assetID = pRB->ID;
            assetName = std::string(pRB->szName);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
            assetID = pSK->skeletonID;
            assetName = std::string(pSK->szName);

            // Add individual bones
            // skip for now since id could clash with non-skeleton RigidBody ids in our RigidBody lookup table
            /*
            if (insertResult.second == true)
            {
                for (int j = 0; j < pSK->nRigidBodies; j++)
                {
                    // Note:
                    // In the DataCallback packet (sFrameOfMocapData) skeleton bones (rigid bodies) ids are of the form:
                    //   parent skeleton ID   : high word (upper 16 bits of int)
                    //   rigid body id        : low word  (lower 16 bits of int)
                    //
                    // In DataDescriptions packet (sDataDescriptions) they are not, so apply the data id format here
                    // for correct lookup during data callback
                    std::pair<std::map<int, std::string>::iterator, bool> insertBoneResult;
                    sRigidBodyDescription rb = pSK->RigidBodies[j];
                    int id = (rb.parentID << 16) | rb.ID;
                    std::string skeletonBoneName = string(pSK->szName) + (":") + string(rb.szName) + string(pSK->szName);
                    insertBoneResult = g_AssetIDtoAssetName.insert(id, skeletonBoneName);
                }
            }
            */
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet)
        {
            // Skip markersets for now as they dont have unique id's, but do increase the index
            // as they are in the data packet
            index++;
            continue;
            /*
            sMarkerSetDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
            assetID = index;
            assetName = pDesc->szName;
            */
        }

        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_ForcePlate)
        {
            sForcePlateDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.ForcePlateDescription;
            assetID = pDesc->ID;
            assetName = pDesc->strSerialNo;
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device)
        {
            sDeviceDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
            assetID = pDesc->ID;
            assetName = std::string(pDesc->strName);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Camera)
        {
            // skip cameras as they are not in the data packet
            continue;
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Asset)
        {
            sAssetDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.AssetDescription;
            assetID = pDesc->AssetID;
            assetName = std::string(pDesc->szName);
        }

        if (assetID == -1)
        {
            printf("\n[SampleClient] Warning : Unknown data type in description list : %d\n", pDataDefs->arrDataDescriptions[i].type);
        }
        else 
        {
            // Add to Asset ID to Asset Name map
            std::pair<std::map<int, std::string>::iterator, bool> insertResult;
            insertResult = g_AssetIDtoAssetName.insert(std::pair<int,std::string>(assetID, assetName));
            if (insertResult.second == false)
            {
                printf("\n[SampleClient] Warning : Duplicate asset ID already in Name map (Existing:%d,%s\tNew:%d,%s\n)",
                    insertResult.first->first, insertResult.first->second.c_str(), assetID, assetName.c_str());
            }
        }

        // Add to Asset ID to Asset Description Order map
        if (assetID != -1)
        {
            std::pair<std::map<int, int>::iterator, bool> insertResult;
            insertResult = g_AssetIDtoAssetDescriptionOrder.insert(std::pair<int, int>(assetID, index++));
            if (insertResult.second == false)
            {
                printf("\n[SampleClient] Warning : Duplicate asset ID already in Order map (ID:%d\tOrder:%d\n)", insertResult.first->first, insertResult.first->second);
            }
        }
    }
}


/**
 * DataHandler is called by NatNet on a separate network processing thread
 * when a frame of mocap data is available
 * 
 * \param data
 * \param pUserData
 * \return 
 */
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData)
{
    NatNetClient* pClient = (NatNetClient*) pUserData;
    if (!pClient)
        return;

    // Note : This function is called every 1 / mocap rate ( e.g. 100 fps = every 10 msecs )
    // We don't want to do too much here and cause the network processing thread to get behind,
    // so let's just safely add this frame to our shared  'network' frame queue and return.
    
    // Note : The 'data' ptr passed in is managed by NatNet and cannot be used outside this function.
    // Since we are keeping the data, we need to make a copy of it.
    shared_ptr<sFrameOfMocapData> pDataCopy = make_shared<sFrameOfMocapData>();
    NatNet_CopyFrame(data, pDataCopy.get());

    
    
    
    MocapFrameWrapper f;
    f.data = pDataCopy;
    f.clientLatencyMillisec = pClient->SecondsSinceHostTimestamp(data->CameraMidExposureTimestamp) * 1000.0;
    f.transitLatencyMillisec = pClient->SecondsSinceHostTimestamp(data->TransmitTimestamp) * 1000.0;

    if (gNetworkQueueMutex.try_lock_for(std::chrono::milliseconds(5)))
    {
        //Just keep the last data
        NatNet_FreeFrame(gNetworkFrame.data.get());
        gNetworkFrame=f;


        //gNetworkQueue.push_back(f);

        // Maintain a cap on the queue size, removing oldest as necessary
        //while ((int)gNetworkQueue.size() > kMaxQueueSize)
        //{
        //    f = gNetworkQueue.front();
        //    NatNet_FreeFrame(f.data.get());
        //    gNetworkQueue.pop_front();
        //}
        gNetworkQueueMutex.unlock();
    }
    else
    {
        // Unable to lock the frame queue and we chose not to wait - drop the frame and notify
        NatNet_FreeFrame(pDataCopy.get());
        printf("\nFrame dropped (Frame : %d)\n", f.data->iFrame);
    }

    return;
}

/**
 * MessageHandler receives NatNet error/debug messages.
 * 
 * \param msgType
 * \param msg
 * \return 
 */
void NATNET_CALLCONV MessageHandler( Verbosity msgType, const char* msg )
{
    // Optional: Filter out debug messages
    if ( msgType < Verbosity_Info )
    {
        return;
    }

    printf( "\n[NatNetLib]" );

    switch ( msgType )
    {
        case Verbosity_Debug:
            printf( " [DEBUG]" );
            break;
        case Verbosity_Info:
            printf( "  [INFO]" );
            break;
        case Verbosity_Warning:
            printf( "  [WARN]" );
            break;
        case Verbosity_Error:
            printf( " [ERROR]" );
            break;
        default:
            printf( " [?????]" );
            break;
    }

    printf( ": %s\n", msg );
}
