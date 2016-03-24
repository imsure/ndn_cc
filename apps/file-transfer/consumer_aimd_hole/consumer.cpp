#include "consumer.hpp"
#include <algorithm>
#include <ctime>
#include <cmath>

namespace ndn {
namespace file_transfer {

Consumer::Consumer(const Name& prefix, const std::string file_name,
                   struct Parameters& params, bool is_verbose, bool hole_detection)
  : m_prefix(prefix)
  , m_face(m_ioService) // Create face with io_service object
  , m_scheduler(m_ioService)
  , m_params(params)
  , m_inFlight(0)
  , m_isVerbose(is_verbose)
  , m_holeDetection(hole_detection)
  , m_timeoutCount(0)
  , m_holeCount(0)
  , m_dataCount(0)
  , m_duplicatesCount(0)
  , m_retxEvent(m_scheduler)
  , m_recordCwndEvent(m_scheduler)
{
  m_ofs.open(file_name, std::ios::out | std::ios::binary);
  if (!m_ofs.is_open()) {
    std::cerr << "Open file: " << file_name << " failed!" << std::endl;
  }

  m_cwnd = m_params.init_cwnd;
  m_cwndTimeSeries.push_back(std::make_pair(time::steady_clock::now(), m_cwnd));
  m_ssthresh = m_params.init_ssthresh;
}

void
Consumer::run()
{
  std::cout << m_holeDetection << std::endl;
  if (m_holeDetection) {
    std::cout << "Running consumer with sequence hole detection..." << std::endl;
  }

  m_nextSegNum = 0;
  // Consumer starts by sending out the interest for the first segment.
  // e.g., /name/prefix/0
  m_inFlight++; // increment # of interests in the current window
  m_rto = 100;
  m_timeSent[m_nextSegNum] = std::make_pair(time::steady_clock::now(), m_rto); // update timer
  m_sentList.push_back(m_nextSegNum); // add segment # at the end of the list
  //  beforeSendingInterest(m_nextSegNum, false);
  Interest interest(Name(m_prefix).appendSegment(m_nextSegNum));
  interest.setInterestLifetime(time::milliseconds(1000));
  interest.setMustBeFresh(true);
  m_face.expressInterest(interest, bind(&Consumer::onDataFirstTime, this, _1, _2,
                                        time::steady_clock::now()));
  //  m_nonceMap[m_nextSegNum] = interest.getNonce();

  // schedule the event to check retransmission timer.
  // for the first interest, since we don't know RTO yet, check it after 1 second.
  m_retxEvent = m_scheduler.scheduleEvent(time::milliseconds(m_params.retxtimer_check_interval),
                                          bind(&Consumer::checkRetxTimer, this));

  // schedule the event to record size of congestion window
  m_recordCwndEvent =
    m_scheduler.scheduleEvent(time::milliseconds(m_params.cwnd_record_interval),
                              bind(&Consumer::recordCwndSize, this));

  m_nextSegNum += 1;

  // m_ioService.run() will block until all events finished or m_ioService.stop() is called
  time::steady_clock::TimePoint start = time::steady_clock::now();
  m_ioService.run();
  time::steady_clock::TimePoint end = time::steady_clock::now();
  Rtt time_elapsed = end - start;

  std::cout << "Number of timedout packets: " << m_timeoutCount << std::endl;
  std::cout << "Number of holes detected: " << m_holeCount << std::endl;
  std::cout << "Packet loss rate: " <<
    ((double)m_timeoutCount+(double)m_holeCount)/(m_lastSegNum+1) << std::endl;
  std::cout << "Total number of data received: " << m_dataCount << std::endl;
  std::cout << "Number of duplicated data received: " << m_duplicatesCount << std::endl;
  std::cout << "It takes " << time_elapsed.count()
            << " millisseconds to run consumer." << std::endl;

  int data_packet_size = content_size + header_overhead;
  double throughput1 =
    (m_dataCount * 8 * data_packet_size) / time_elapsed.count();
  double throughput2 =
    ((m_dataCount-m_duplicatesCount) * 8 * data_packet_size) / time_elapsed.count();

  std::cout << "Throughput with duplicates: " << throughput1 << " kbps" << std::endl;
  std::cout << "Throughput without duplicates: " << throughput2 << " kbps" << std::endl;

  writeInOrderData();
  writeStats();
}

void
Consumer::beforeSendingInterest(uint64_t segno, bool retx)
{
  if (retx) {
    m_retxList.push_back(segno);
  } else {
    m_sentList.push_back(segno); // add segment # at the end of the list
  }
  m_inFlight++; // increment # of interests in the current window
  m_timeSent[segno] = std::make_pair(time::steady_clock::now(), m_rto); // update timer
}

void
Consumer::sendInterest(uint64_t segno, bool retx)
{
  Interest interest(Name(m_prefix).appendSegment(segno));

  // if (retx) {
  //   interest.setNonce(m_nonceMap[segno]);
  // } else {
  //   m_nonceMap[segno] = interest.getNonce();
  // }
  interest.setInterestLifetime(time::milliseconds(2000));
  //interest.setInterestLifetime(time::milliseconds((int)m_rto));
  interest.setMustBeFresh(true);

  std::cout << "Sending interest " << segno << ", nonce: "
            << interest.getNonce() << std::endl;

  m_face.expressInterest(interest, bind(&Consumer::onData, this, _1, _2,
                                        time::steady_clock::now()));
}

uint64_t
Consumer::getExpectedSegmentNum() const
{
  if (m_sentList.size() > 0)
    return m_sentList.front();
  else {
    return 0;
  }
}

void
Consumer::onDataFirstTime(const Interest& interest, const Data& data,
                          const time::steady_clock::TimePoint& timeSent)
{
  // initialize SRTT and RTTVAR
  Rtt measured_rtt = time::steady_clock::now() - timeSent;
  m_sRtt = measured_rtt.count(); // in milliseconds
  m_rttVar = m_sRtt / 2;
  m_rto = m_sRtt + m_params.k * m_rttVar;

  uint64_t recv_segno = data.getName()[-1].toSegment();
  m_lastSegNum = data.getFinalBlockId().toSegment();

  if (m_isVerbose) {
    std::cout << "Measured RTT: " << measured_rtt.count() << "ms" << std::endl;
    std::cout << "Segment received: " << data.getName()[-1].toSegment() << std::endl;
    std::cout << "Expected last segment #: " << m_lastSegNum << std::endl;
  }

  afterReceivingData(recv_segno);

  if (m_cwnd < m_ssthresh) {
    m_cwnd += m_params.ai_step; // additive increase
  }

  m_validator.validate(data,
                       bind(&Consumer::onDataValidated, this, _1),
                       bind(&Consumer::onFailure, this, _2));

  m_sentList.remove(recv_segno);
  schedulePackets();
}

void
Consumer::rttEstimator(double rtt)
{
  m_rttVar = (1-m_params.beta) * m_rttVar + m_params.beta * std::abs(m_sRtt-rtt);
  m_sRtt = (1-m_params.alpha) * m_sRtt + m_params.alpha * rtt;

  //m_rto = m_sRtt + std::max(20.0, m_params.k * m_rttVar);
  m_rto = m_sRtt + m_params.k * m_rttVar;
  //if (m_rto < 500) m_rto = 500.0;
  if (m_rto > 2000) m_rto = 2000.0;
}

bool
Consumer::dataReceived(uint64_t segno)
{
  auto it = find(m_recvList.begin(), m_recvList.end(), segno);
  if (it == m_recvList.end()) { // not found in retx list
    return false;
  } else {
    return true;
  }
}

void
Consumer::checkRetxTimer()
{
  if (m_isVerbose) {
    std::cout << "Checking retx timer..." << std::endl;
    std::cout << "current window size: " << m_cwnd << ", in flight size: "
              << m_inFlight << ", RTO: " << m_rto
              << ", ssthresh: " << m_ssthresh << std::endl;
  }

  int timeout_count = 0;
  bool timeout_found = false;
  for (auto it = m_timeSent.begin(); it != m_timeSent.end(); ++it) {
    time::steady_clock::TimePoint time_sent = it->second.first;
    double rto = it->second.second;
    Rtt elapsed = time::steady_clock::now() - time_sent; // time elapsed
    if (elapsed.count() > rto) { // timer expired?
      uint64_t timedout_seg = it->first;
      //if (m_isVerbose) {
      std::cout << "Segment " << timedout_seg << " timed out."
                << "time elapsed: " << elapsed << ", rto: " << rto << std::endl;
        //}
      m_timeSent.erase(timedout_seg); // remove checked entry
      m_retxQueue.push(timedout_seg); // put on retx queue
      timeout_found = true;
      timeout_count++;
    }
  }

  if (timeout_found) {
    onTimeout(timeout_count);
  }

  m_scheduler.scheduleEvent(time::milliseconds(m_params.retxtimer_check_interval),
                            bind(&Consumer::checkRetxTimer, this));
}

void
Consumer::onData(const Interest& interest, const Data& data,
                 const time::steady_clock::TimePoint& timeSent)
{
  uint64_t recv_segno = data.getName()[-1].toSegment();

  std::vector<uint64_t>::iterator _it;
  _it = find(m_recvList.begin(), m_recvList.end(), recv_segno);
  // auto _it = m_timeSent.find(recv_segno);
  if (_it == m_recvList.end()) { // not found
  //if (_it != m_timeSent.end()) { // found
    m_dataCount++;
    m_recvList.push_back(recv_segno);
    m_timeSent.erase(recv_segno);
  } else { // found
    // it means we've received duplicate data packets, probably due to retransmission
    m_duplicatesCount++;
    //if (m_isVerbose)
    std::cout << "A duplicate data packet received, segment # = " << recv_segno << std::endl;
    if (m_inFlight > 0) {
      m_inFlight--;
    }
    m_timeSent.erase(recv_segno);
    schedulePackets();
    return;
  }

  if (m_inFlight > 0) {
    m_inFlight--;
  }

  //  afterReceivingData(recv_segno);

  Rtt measured_rtt = time::steady_clock::now() - timeSent;
  std::cout << "Segment received: " << recv_segno
            << ", RTT: " << measured_rtt.count() << "ms" << std::endl;
  //std::cout << "RTO: " << m_rto << "ms" << std::endl;

  std::list<uint64_t>::iterator it;
  it = find(m_retxList.begin(), m_retxList.end(), recv_segno);
  if (it == m_retxList.end()) { // not found in retx list, sample RTT
    // Calculate SRTT and RTTVAR
    double time_elapsed = measured_rtt.count();
    rttEstimator(time_elapsed);
    // std::cout << "Measured RTT: " << measured_rtt.count() << "ms" << std::endl;
  } else { // received segment number was retransmitted
    //if (m_isVerbose)
    std::cout << "Retransmitted segment received: " << recv_segno << std::endl;

    //m_retxList.remove(recv_segno);
  }

  if (m_holeDetection) {
    uint64_t expected_segno = getExpectedSegmentNum();
    if (expected_segno != 0 && expected_segno != recv_segno) { // out of order segment received
      m_outOfOrderList.push_back(recv_segno);
      if (m_outOfOrderList.size() >= m_params.max_num_ood) {
        // congestion signal(expected may loss)
        //std::cout << "Sequence hole detected!!!"<< std::endl;

        m_ssthresh = std::max(2.0, m_cwnd * m_params.md_coef);
        m_cwnd = m_ssthresh; // fast recovery
        m_holeCount++;
        m_retxQueue.push(expected_segno); // fast retransmission
        m_sentList.pop_front(); // move on to the next expected segment
        m_outOfOrderList.clear(); // reset for next hole detection
      }
    } else {
      if (m_cwnd < m_ssthresh) {
        m_cwnd += m_params.ai_step; // additive increase
      } else {
        m_cwnd += m_params.ai_step/m_cwnd; // congestion avoidance
      }
      m_outOfOrderList.clear(); // reset since we got the expected segment
    }
  } else {
    if (m_cwnd < m_ssthresh) {
      m_cwnd += m_params.ai_step; // additive increase
    } else {
      m_cwnd += m_params.ai_step/m_cwnd; // congestion avoidance
    }
  }

  m_validator.validate(data, bind(&Consumer::onDataValidated, this, _1),
                       bind(&Consumer::onFailure, this, _2));

  if ((m_recvList.size() - 1) >= m_lastSegNum) { // all interests have been acked
    if (m_isVerbose)
      std::cout << "Stopping the consumer..." << std::endl;
    stop(); // stop the consumer
  }

  m_sentList.remove(recv_segno);
  schedulePackets();
}

void
Consumer::afterReceivingData(uint64_t recv_segno)
{
  m_dataCount++;
  std::vector<uint64_t>::iterator it;
  it = find(m_recvList.begin(), m_recvList.end(), recv_segno);
  if (it == m_recvList.end()) { // not found
    m_recvList.push_back(recv_segno);
  } else { // found
    // it means we've received duplicate data packets, probably due to retransmission
    m_duplicatesCount++;
    //if (m_isVerbose)
    std::cout << "A duplicate data packet received, segment # = " << recv_segno << std::endl;
  }

  if (m_inFlight > 0) {
    m_inFlight--;
  }

  m_timeSent.erase(recv_segno);
}

void
Consumer::schedulePackets()
{
  int avail_window_sz = m_cwnd - m_inFlight;

  while (avail_window_sz > 0) {
    // first check if there is any packets need to be retransmitted
    if (m_retxQueue.size()) { // they have higher priority
      uint64_t retx_segno = m_retxQueue.front();
      m_retxQueue.pop();

      std::vector<uint64_t>::iterator it;
      it = find(m_recvList.begin(), m_recvList.end(), retx_segno);
      if (it != m_recvList.end()) { // already received, no need to retransmit
        std::cout << "segment: " << retx_segno
                  << "aready received, no need for retransmission" << std::endl;
        continue;
      }

      beforeSendingInterest(retx_segno, true);
      sendInterest(retx_segno, true);

      if (m_isVerbose)
        std::cout << "Retransmitting segment: " << retx_segno << std::endl;

    } else { // send in order interest
      if (m_nextSegNum > m_lastSegNum) {
        break;
      }
      beforeSendingInterest(m_nextSegNum, false);
      sendInterest(m_nextSegNum, false);

      if (m_isVerbose)
        std::cout << "Sending out next segment: "<< m_nextSegNum << std::endl;

      m_nextSegNum += 1; // only increase for in order segement, not for retxed ones
    }
    avail_window_sz--;
  }
}

void
Consumer::onDataValidated(shared_ptr<const Data> data)
{
  if (data->getContentType() == ndn::tlv::ContentType_Nack) {
    std::cout << "Application level NACK: " << *data << std::endl;
    return;
  }

  m_bufferedData[data->getName()[-1].toSegment()] = data;
}

void
Consumer::onFailure(const std::string& reason)
{
  throw std::runtime_error(reason);
}

void
Consumer::onTimeout(int timeout_count)
{
  m_ssthresh = std::max(2.0, m_cwnd * m_params.md_coef); // multiplicative decrease
  m_cwnd = m_params.init_cwnd;
  m_rto = std::min(m_params.max_rto, m_rto * m_params.rto_backoff_multiplier); // backoff RTO

  m_timeoutCount += timeout_count;
  m_inFlight = std::max(0, m_inFlight - timeout_count);

  if (m_isVerbose) {
    std::cout << "On time out..." << std::endl;
    std::cout << "current window size: " << m_cwnd << ", in flight size: "
              << m_inFlight << ", RTO: " << m_rto
              << ", ssthresh: " << m_ssthresh << std::endl;
  }

  schedulePackets();
}

void
Consumer::writeInOrderData()
{
  for (auto it = m_bufferedData.begin();
       it != m_bufferedData.end();
       it = m_bufferedData.erase(it)) {

    const Block& content = it->second->getContent();
    m_ofs.write(reinterpret_cast<const char*>(content.value()), content.value_size());
  }
}

void
Consumer::stop()
{
  // cancel the running events
  m_retxEvent.cancel();
  m_recordCwndEvent.cancel();

  m_ioService.stop(); // stop I/O service
}

void
Consumer::recordCwndSize()
{
  m_cwndTimeSeries.push_back(std::make_pair(time::steady_clock::now(), m_cwnd));
  m_scheduler.scheduleEvent(time::milliseconds(m_params.cwnd_record_interval),
                            bind(&Consumer::recordCwndSize, this));
}

void
Consumer::writeStats()
{
  std::string t = time::toIsoString(time::system_clock::now());
  std::ofstream fs_cwnd("cwnd_size_"+t+".txt");

  // header
  fs_cwnd << "time";
  fs_cwnd << '\t';
  fs_cwnd << "cwndsize\n";

  // time when the first segment was sent
  time::steady_clock::TimePoint time_1st_rec = m_cwndTimeSeries[0].first;
  fs_cwnd << "0\t";
  fs_cwnd << m_cwndTimeSeries[0].second;
  fs_cwnd << '\n';
  Rtt time_since_1strec;
  for (int i = 1; i < m_cwndTimeSeries.size(); ++i) {
    time_since_1strec = m_cwndTimeSeries[i].first - time_1st_rec;
    fs_cwnd << time_since_1strec.count() / 1000; // in seconds
    fs_cwnd << '\t';
    fs_cwnd << m_cwndTimeSeries[i].second;
    fs_cwnd << '\n';
  }
}

} // namespace examples
} // namespace ndn
