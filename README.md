# NDN file transfer appliction with congestion control

## Development environment setup

### Clone git repository

```
git clone https://github.com/imsure/ndn_cc ndn_cc
```

### Setup virtual machine and install NDN packages

- Install [vagrant](https://www.vagrantup.com/) and bring up the
VM:

```
mkdir vm_dir
cp -r ndn_cc vm_dir
cd vm_dir
cp ndn_cc/scripts/Vagrantfile ./
cp ndn_cc/scripts/nodes.json ./
cp ndn_cc/scripts/ndn_install ./
vagrant up
```

- SSH into the VM and install NDN related software

```
vagrant ssh
cd /vagrant/
./ndn_install
```

### Compile file transfer application

```
cd /vagrant/ndn_cc
make
make install
```

### Run file transfer application without minindn

- start nfd
```
nfd-start
```

- run producer
```
ft-producer /ndn/name/prefix /path/to/file
```

- run consumer with AIMD scheme
```
ft-consumer-reno /ndn/name/prefix /path/to/file
```

- run consumer with AIMD+Hole scheme
```
ft-consumer-reno -o /ndn/name/prefix /path/to/file
```

### Run file transfer application on minindn

```
cp /vagrant/ndn_cc/scripts/minindn_experiments/*.py
/vagrant/mini-ndn/ndn/experiments

cp /vagrant/ndn_cc/scripts/minindn_topo/*.conf
/vagrant/mini-ndn/ndn_utils/topologies/

sudo /vagrant/mini-ndn/install.sh -i
sudo minindn --experiment=cc_reno
/vagrant/mini-ndn/ndn_utils/topologies/linear.conf
```

The results are kept at ```/tmp```.
