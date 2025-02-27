#ifndef OPTITRACK_H
#define OPTITRACK_H

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <cinttypes>
#include <map>
#include <iostream>
#include <fstream>
#endif

// stl
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <memory>
using namespace std;

#include <NatNetTypes.h>
#include <NatNetCAPI.h>
#include <NatNetClient.h>



class Optitrack{
private:
    // Connection variables
    NatNetClient* g_pClient;
    sNatNetClientConnectParams g_connectParams;
    sServerDescription g_serverDescription;

    std::string Server_IP;
    std::string Local_IP;

    // To stop the run() method, we use signals
    static void SignalHandler(int sig); 
    const std::string fileName;
    std::ofstream *fileRecorder;
    const std::chrono::milliseconds pollPeriodSpan;

    // DataDescriptions to Frame Data Lookup maps
    sDataDescriptions* g_pDataDefs;
    map<int, int> g_AssetIDtoAssetDescriptionOrder;
    map<int, string> g_AssetIDtoAssetName;

    static bool isStop;
    static bool isStart;

    int ConnectClient();
    bool UpdateDataDescriptions(bool printToConsole=false);
    void UpdateDataToDescriptionMaps(sDataDescriptions* pDataDefs);

public:
    Optitrack(std::string ="10.42.1.100", std::string ="10.42.1.100", const std::string ="optotrack");
    bool connect();
    bool run();
    bool disConnect();
    
};


#endif