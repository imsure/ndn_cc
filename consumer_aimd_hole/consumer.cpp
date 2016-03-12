#include "consumer.hpp"
#include <algorithm>
#include <ctime>
#include <cmath>

namespace ndn {
namespace file_transfer {

Consumer::Consumer(const Name& prefix, const std::string file_name,
                   struct Parameters& params, bool is_verbose)
  : m_prefix(prefix)
  , m_face(m_ioService) // Create face with io_service object
  , m_scheduler(m_ioService)
  , m_params(params)
  , m_inFlight(0)
  , m_isVerbose(is_verbose)
  , m_timeoutCount(0)
  , m_holeCount(0)
  , m_dataCount(0)
  , m_retxEvent(m_scheduler)
  , m_recordCwndEvent(m_scheduler)
{
  m_ofs.open(file_name, std::ios::out | std::ios::binary);
  if (!m_ofs.is_open()) {
    std::cerr << "Open file: " << file_name << " failed!" << std::endl;
  }

  m_cwnd = m_params.init_cwnd;
  m_cwndTimeSeries.push_back(std::make_pair(time::steady_clock::now(), m_cwnd));
  //recordCwndSize();
  m_ssthresh = m_params.init_ssthresh;
}

void
Consumer::run()
{
  // Consumer starts by sending out the interest for the first segment.
  // e.g., /name/prefix/0
  beforeSendingIntrest(0, false);
  m_nextSegNum = 1;

  Interest interest(Name(m_prefix).appendSegment(0));
  interest.setInterestLifetime(time::milliseconds(10000));
  interest.setMustBeFresh(true);
  m_face.expressInterest(interest,
                         bind(&Consumer::onDataFirstTime, this, _1, _2,
                              time::steady_clock::now()));

  // schedule the event to record size of congestion window
  m_recordCwndEvent =
    m_scheduler.scheduleEvent(time::milliseconds(m_params.cwnd_record_interval),
                              bind(&Consumer::recordCwndSize, this));

  // schedule the event to check retransmission timer
  m_retxEvent = m_scheduler.scheduleEvent(time::milliseconds(m_params.retxtimer_check_interval),
                                          bind(&Consumer::checkRetxTimer, this));

  // m_ioService.run() will block until all events finished or m_ioService.stop() is called
  std::time_t start = std::time(NULL);
  m_ioService.run();
  std::time_t stop = std::time(NULL);

  std::cout << "Number of timedout packets: " << m_timeoutCount << std::endl;
  std::cout << "Number of holes detected: " << m_holeCount << std::endl;
  std::cout << "Packet loss rate: " << ((double)m_timeoutCount+(double)m_holeCount)/(m_lastSegNum+1) << std::endl;
  std::cout << "Number of data received: " << m_dataCount << std::endl;
  std::cout << "It takes " << stop - start << " seconds to run consumer." << std::endl;
  std::cout << "Throughput: " << (m_dataCount*8*1024)/((stop-start)*1000) << " Kbps" << std::endl;

  writeInOrderData();
  writeStats();
}

void
Consumer::beforeSendingIntrest(uint64_t segno, bool retx)
{
  if (retx) {
    m_retxList.push_back(segno);
  } else {
    m_sentList.push_back(segno); // add segment # at the end of the list
  }
  m_inFlight++; // increment # of interests in the current window
  m_timeSent[segno] = time::steady_clock::now(); // update timer
  m_sentPacketsTimeSeries.push_back(std::make_pair(m_timeSent[segno], segno));
}

void
Consumer::sendInterest(uint64_t segno)
{
  Interest interest(Name(m_prefix).appendSegment(segno));
  interest.setInterestLifetime(time::milliseconds(10000));
  interest.setMustBeFresh(true);

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
  //time::nanoseconds rtt = time::steady_clock::now() - timeSent;

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

  m_rto = m_sRtt + m_params.k * m_rttVar;
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
  std::map<uint64_t, time::steady_clock::TimePoint>::iterator it;
  for (it = m_timeSent.begin(); it != m_timeSent.end(); ++it) {
    Rtt elapsed = time::steady_clock::now() - it->second; // time elapsed
    if (elapsed.count() > m_rto) { // timer expired?
      uint64_t timedout_seg = it->first;
      if (m_isVerbose)
        std::cout << "Segment " << timedout_seg << " timed out." << std::endl;
      m_timeSent.erase(timedout_seg); // remove checked entry
      //auto it = find(m_retxList.begin(), m_retxList.end(), timedout_seg);
      //if (it == m_retxList.end()) { // not found in retx list, sample RTT
        m_retxQueue.push(timedout_seg); // put on retx queue
        //}

      timeout_found = true;
      timeout_count++;
    }
  }

  if (timeout_found) {
    onTimeout(timeout_count);
  }

  m_scheduler.scheduleEvent(time::milliseconds(2*(int)m_rto),
                            bind(&Consumer::checkRetxTimer, this));
}

void
Consumer::onData(const Interest& interest, const Data& data,
                 const time::steady_clock::TimePoint& timeSent)
{
  uint64_t recv_segno = data.getName()[-1].toSegment();
  uint64_t expected_segno = getExpectedSegmentNum();
  if (m_isVerbose) {
    std::cout << "Segment received: " << recv_segno << std::endl;
    std::cout << "Segment expected: " << expected_segno << std::endl;
  }

  afterReceivingData(recv_segno);

  // if ((m_recvList.size() - 1) >= m_lastSegNum) { // all interests have been acked
  //   if (m_isVerbose)
  //     std::cout << "Stopping the consumer..." << std::endl;
  //   stop(); // stop the consumer
  //   return;
  // }

  std::list<uint64_t>::iterator it;
  it = find(m_retxList.begin(), m_retxList.end(), recv_segno);
  if (it == m_retxList.end()) { // not found in retx list, sample RTT
    // Calculate SRTT and RTTVAR
    Rtt measured_rtt = time::steady_clock::now() - timeSent;
    double time_elapsed = measured_rtt.count();
    m_retxTimerPerformance.push_back(std::make_pair(time::steady_clock::now(),
                                                    std::make_pair(time_elapsed, m_rto)));
    rttEstimator(time_elapsed);
  } else { // received segment number was retransmitted
    //m_retxList.remove(recv_segno);
    if (m_isVerbose)
      std::cout << "Retransmitted segment received: " << recv_segno << std::endl;
  }

  bool hole_detected = false;
  if (expected_segno != 0 && expected_segno != recv_segno) { // out of order segment received
    m_outOfOrderList.push_back(recv_segno);
    if (m_outOfOrderList.size() >= m_params.max_num_ood) {
      // congestion signal(expected may loss)
      //std::cout << "Sequence hole detected!!!"<< std::endl;

      m_ssthresh = std::max(5.0, m_cwnd * m_params.md_coef);
      m_cwnd = m_ssthresh; // fast recovery
      m_holeCount++;
      m_retxQueue.push(expected_segno); // fast retransmission
      m_sentList.pop_front(); // move on to the next expected segment
      m_outOfOrderList.clear(); // reset for next hole detection
      hole_detected = true;
    }
  } else {
    if (m_cwnd < m_ssthresh) {
      m_cwnd += m_params.ai_step; // additive increase
    } else {
      m_cwnd += m_params.ai_step/m_cwnd; // congestion avoidance
    }
    m_outOfOrderList.clear(); // reset since we got the expected segment
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
    if (m_isVerbose)
      std::cout << "A duplicate data packet received, segment # = " << recv_segno << std::endl;
  }

  if (m_inFlight > 0) {
    m_inFlight--;
  }

  // remove the entry for the recived segment # in m_timeSent
  //m_retxEvent.cancel();
  int ret = m_timeSent.erase(recv_segno);

  // re-schedule the event to check retransmission timer.
  // note if consumer keep receiving data packets, the event may be reschduled
  // again and again without the actual checkRetxtimer function being called.
  // this makes sense because when network is in good condition, we should keep
  // receiving data packets (may be in order).
  // however, we do need to detect if there is any hole in the received packet sequence
  // to retransmit necessary interest packets.
  // m_retxEvent = m_scheduler.scheduleEvent(time::milliseconds(m_params.retxtimer_check_interval),
  //                                         bind(&Consumer::checkRetxTimer, this));
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

      if (m_isVerbose)
        std::cout << "Retransmitting segment: " << retx_segno << std::endl;
      beforeSendingIntrest(retx_segno, true);
      sendInterest(retx_segno);
    } else { // send in order interest
      if (m_nextSegNum > m_lastSegNum) {
        break;
      }
      beforeSendingIntrest(m_nextSegNum, false);
      if (m_isVerbose)
        std::cout << "Sending out next segment: "<< m_nextSegNum << std::endl;
      sendInterest(m_nextSegNum);
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
Consumer::retxPackets(Interest& retxed_interest)
{
}

void
Consumer::onTimeout(int timeout_count)
{
  m_timeoutCount += timeout_count;
  m_ssthresh = std::max(5.0, m_cwnd * m_params.md_coef); // multiplicative decrease
  m_cwnd = m_params.init_cwnd;
  //m_cwnd = 2.0;
  m_rto *= m_params.rto_backoff_multiplier; // backoff RTO

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
  std::ofstream fs_sent_packet("sent_packet_timeseries_"+t+".txt");

  // header
  fs_sent_packet << "segment#";
  fs_sent_packet << '\t';
  fs_sent_packet << "send-time\n";

  // time when the first segment was sent
  time::steady_clock::TimePoint time_sent_seg0 = m_sentPacketsTimeSeries[0].first;
  fs_sent_packet << m_sentPacketsTimeSeries[0].second;
  fs_sent_packet << '\t';
  fs_sent_packet << "0\n";
  Rtt time_since_seg0;
  for (int i = 1; i < m_sentPacketsTimeSeries.size(); ++i) {
    fs_sent_packet << m_sentPacketsTimeSeries[i].second;
    fs_sent_packet << '\t';
    time_since_seg0 = m_sentPacketsTimeSeries[i].first - time_sent_seg0;
    fs_sent_packet << time_since_seg0.count();
    fs_sent_packet << '\n';
  }

  std::ofstream fs_retx_timer("retx_timer_performance_"+t+".txt");
  fs_retx_timer << "time";
  fs_retx_timer << '\t';
  fs_retx_timer << "rtt";
  fs_retx_timer << '\t';
  fs_retx_timer << "rto\n";

  time::steady_clock::TimePoint time_1st = m_retxTimerPerformance[0].first;
  fs_retx_timer << "0";
  fs_retx_timer << '\t';
  fs_retx_timer << m_retxTimerPerformance[0].second.first;
  fs_retx_timer << '\t';
  fs_retx_timer << m_retxTimerPerformance[0].second.second;
  fs_retx_timer << '\n';

  Rtt duration;
  for (int i = 1; i < m_retxTimerPerformance.size(); ++i) {
    duration = m_retxTimerPerformance[i].first - time_1st;
    fs_retx_timer << duration.count();
    fs_retx_timer << '\t';
    fs_retx_timer << m_retxTimerPerformance[i].second.first;
    fs_retx_timer << '\t';
    fs_retx_timer << m_retxTimerPerformance[i].second.second;
    fs_retx_timer << '\n';
  }

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
