#include "Optitrack.h"
#include <iostream>

Optitrack *tracker;

int main(){
    tracker = new Optitrack();
    std::cout<<"Connecting..."<<std::endl;
    if(!tracker->connect()){
        std::cout<<"not connect... Exiting..."<<std::endl;
        return 0;
    } 
    std::cout<<"Connected..."<<std::endl;


    sleep(1);
    
    std::cout<<"Running..."<<std::endl;
    if(!tracker->run()){
        std::cout<<"not stop run correctly... Exiting..."<<std::endl;
        std::cout<<"disconnecting..."<<std::endl;
        if(!tracker->disConnect()){
            std::cout<<"not diconnect... Exiting..."<<std::endl;
            return 0;
        }
        return 0;
    } 
    std::cout<<"Stopped..."<<std::endl;

    std::cout<<"disconnecting..."<<std::endl;
    if(!tracker->disConnect()){
        std::cout<<"not diconnect... Exiting..."<<std::endl;
        return 0;
    }

    std::cout<<"Optitrack process has finished....Exiting..."<<std::endl;

    return 0; 
}