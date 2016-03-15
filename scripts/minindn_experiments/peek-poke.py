from ndn.experiments.experiment import  Experiment

class Experiment1(Experiment):
    def __init__(self,args):
        Experiment.__init__(self, args)
    def run(self):
        for host in self.net.hosts:
            if host.name == "producer":
                host.cmd("echo test1 | ndnpoke /ndn/edu/producer &")

        for host in self.net.hosts:
            if host.name == "consumer":
                print host.cmd("ndnpeek -p /ndn/edu/producer")

Experiment.register("peek-poke", Experiment1)
