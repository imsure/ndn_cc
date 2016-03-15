#!/bin/bash

# install ndn-cxx and nfd dependencies
sudo apt-get install libsqlite3-dev libcrypto++-dev
sudo apt-get install libboost-all-dev
sudo apt-get install pkg-config
sudo apt-get install libpcap-dev
sudo apt-get install doxygen graphviz python-sphinx

# Download ndn-cxx
git clone https://github.com/named-data/ndn-cxx

# Download NFD
git clone --recursive https://github.com/named-data/NFD

# install ndn-cxx
cd ndn-cxx
./waf configure
./waf
sudo ./waf install

# install nfd
cd NFD
./waf configure
./waf
sudo ./waf install

sudo cp /usr/local/etc/ndn/nfd.conf.sample /usr/local/etc/ndn/nfd.conf

#install mininet
sudo apt-get install mininet

# install mini-ndn
git clone https://github.com/named-data/mini-ndn.git
cd mini-ndn
sudo ./install.sh -i
