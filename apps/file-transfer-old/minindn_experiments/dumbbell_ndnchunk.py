#!/usr/bin/python

from ndn.experiments.experiment import Experiment
import time

class DumbbellNdnchunkExperiment(Experiment):
    def __init__(self, args):
        Experiment.__init__(self, args)

    def setup(self):
        for host in self.net.hosts:
            # Set strategy
            host.nfd.setStrategy("/ndn/edu", self.strategy)

            # Start ping server
            # host.cmd("ndnpingserver /ndn/edu/" + str(host) + " > ping-server &")

            if host.name.startswith('producer'):
                print "run %s" % host.name
                host.cmd('ndnputchunks -v /ndn/edu/%s/spmcat.dat < /vagrant/cs460/spring16/extendible_hashing/spmcat.dat 2> stderr.log &' % host.name)

        # Wait for convergence time period
        print "Waiting " + str(self.convergenceTime) + " seconds for convergence..."
        time.sleep(self.convergenceTime)
        print "...done"

        # To check whether all the nodes of NLSR have converged
        didNlsrConverge = True

        # Checking for convergence
        for host in self.net.hosts:
            statusRouter = host.cmd("nfd-status -b | grep /ndn/edu/%C1.Router/cs/")
            statusPrefix = host.cmd("nfd-status -b | grep /ndn/edu/")
            didNodeConverge = True
            for node in self.nodes.split(","):
                    if ("/ndn/edu/%C1.Router/cs/" + node) not in statusRouter:
                        didNodeConverge = False
                        didNlsrConverge = False
                    if str(host) != node and ("/ndn/edu/" + node) not in statusPrefix:
                        didNodeConverge = False
                        didNlsrConverge = False

            host.cmd("echo " + str(didNodeConverge) + " > convergence-result &")

        if didNlsrConverge:
            print("NLSR has successfully converged.")
        else:
            print("NLSR has not converged. Exiting...")
            for host in self.net.hosts:
                host.nfd.stop()
            sys.exit(1)


    def run(self):
        for host in self.net.hosts:
            if host.name.startswith('consumer'):
                print "run %s" % host.name
                host.cmd("ndncatchunks -v -d iterative /ndn/edu/producer%s/spmcat.dat 2> stderr.log &" % host.name[-1])
        time.sleep(2)


Experiment.register("dumbell_ndnchunk", DumbbellNdnchunkExperiment)
