include=-I../../include
libpath=-L/home/lattepanda/git/RIS_122E408_Project/Optitrack_NatNet_SDK_4_1_Ubuntu/lib/
libs=-lNatNet

all:build/Optitrack_Process

build/Optitrack_Process: 
	g++ Optitrack_Process.cpp Optitrack.cpp $(include) $(libpath) $(libs) -o build/Optitrack_Process 
	#g++ SampleClient.cpp  $(include) $(libpath) $(libs) -o build/SampleClient
.PHONY: clean
clean:
	@rm -f ./build/Optitrack_Process
