all:
	cd consumer_aimd_hole/; make; cd ..;
	cd producer/; make; cd ..;

clean:
	cd consumer_aimd_hole/; make clean; cd ..;
	cd producer/; make clean; cd ..;

install:
	cd consumer_aimd_hole/; sudo cp ft-consumer-reno /usr/local/bin/; cd ..;
	cd producer/; sudo cp ft-producer /usr/local/bin/; cd ..;
