CXX=g++
CXX_FLAGS= --std=c++11 -lboost_program_options -lboost_system -lndn-cxx

all: ft-consumer-reno

ft-consumer-reno: file-transfer-consumer.cpp consumer.cpp
	$(CXX) -o $@ $^ $(CXX_FLAGS)

clean:
	rm ft-consumer-reno
