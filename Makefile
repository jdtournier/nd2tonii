
all: nd2tonii

nd2tonii: nd2tonii.cpp nifti1.h
	g++ -std=c++11 -Wall -O2 nd2tonii.cpp -o nd2tonii

