from mininet.topo import Topo
from mininet.link import TCLink

class MyTopo( Topo ):
    "Simple topology example."

    def __init__( self ):
        "Create custom topo."

        # Initialize topology
        Topo.__init__( self )

        # Add hosts and switches
        leftHost = self.addHost( 'h1' )
        rightHost = self.addHost( 'h2' )
        leftSwitch = self.addSwitch( 's3' )
        rightSwitch = self.addSwitch( 's4' )

        # Add links
        linkopts = dict(bw=10, delay='10ms', loss=0)
        self.addLink( leftHost, leftSwitch, **linkopts )
        linkopts = dict(bw=5, delay='10ms', loss=0, max_queue_size=32)
        self.addLink( leftSwitch, rightSwitch, **linkopts)
        linkopts = dict(bw=10, delay='10ms', loss=0)
        self.addLink( rightSwitch, rightHost, **linkopts )


topos = { 'mytopo': ( lambda: MyTopo() ) }
