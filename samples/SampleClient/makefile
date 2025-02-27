include=-I../../include
libpath=-L../../lib
libs=-lNatNet

all:build/Optitrack_Process

build/Optitrack_Process: 
	g++ Optitrack_Process.cpp Optitrack.cpp $(include) $(libpath) $(libs) -o build/Optitrack_Process 

.PHONY: clean
clean:
	@rm -f ./build/Optitrack_Process
