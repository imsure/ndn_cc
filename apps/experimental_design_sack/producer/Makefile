CXX=g++
CXX_FLAGS= --std=c++11 -lboost_program_options -lboost_system -lndn-cxx

all: ft-producer

ft-producer: file-transfer-producer.cpp producer.cpp
	$(CXX) -o $@ $^ $(CXX_FLAGS)

clean:
	rm ft-producer
