#!/usr/bin/python

from ndn.experiments.experiment import Experiment
import time

class FtpExperiment(Experiment):
    def __init__(self, args):
        Experiment.__init__(self, args)

    def setup(self):
        pass

    def run(self):
        for host in self.net.hosts:
            if host.name.startswith('producer'):
                print "run ftp server on producer"
                host.cmd('sudo vsftpd &')

Experiment.register("ftp-over-tcp", FtpExperiment)
