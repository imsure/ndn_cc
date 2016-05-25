import sys

if __name__ == "__main__":
    print('Ndndump log file: {}'.format(sys.argv[1]))
    ndndump_log = open(sys.argv[1], 'r')
    #print('Total # of lines: {}'.format(len(ndndump_log.readlines())))

    total = 0;
    data_couter = 0
    interest_counter = 0
    for line in ndndump_log.readlines():
        total += 1
        if line.find('DATA:') != -1:
            data_couter += 1
        elif line.find('INTEREST:') != -1:
            interest_counter += 1

    print('Total # of packets: {}'.format(total))
    print('# of Data: {}'.format(data_couter))
    print('# of Interest: {}'.format(interest_counter))
