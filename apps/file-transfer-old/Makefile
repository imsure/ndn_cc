all:
	cd consumer_fixed_cwnd/; make; cd ..;
	cd consumer_aimd/; make; cd ..;
	cd consumer_aimd_hole/; make; cd ..;
	cd producer/; make; cd ..;

clean:
	cd consumer_fixed_cwnd/; make clean; cd ..;
	cd consumer_aimd/; make clean; cd ..;
	cd consumer_aimd_hole/; make clean; cd ..;
	cd producer/; make clean; cd ..;

install:
	cd consumer_fixed_cwnd/; sudo cp ft-consumer-fixed /usr/local/bin/; cd ..;
	cd consumer_aimd/; sudo cp ft-consumer-aimd /usr/local/bin/; cd ..;
	cd consumer_aimd_hole/; sudo cp ft-consumer-reno /usr/local/bin/; cd ..;
	cd producer/; sudo cp ft-producer /usr/local/bin/; cd ..;
