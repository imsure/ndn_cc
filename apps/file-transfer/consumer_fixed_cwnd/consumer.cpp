#include "consumer.hpp"
#include <algorithm>
#include <ctime>
#include <cmath>

namespace ndn {
namespace file_transfer {

Consumer::Consumer(const Name& prefix, const std::string file_name,
                   int cwnd_size, int interest_lifetime, bool is_verbose)
  : m_prefix(prefix)
  , m_face(m_ioService) // Create face with io_service object
  , m_scheduler(m_ioService)
  , m_cwnd(cwnd_size)
  , m_inFlight(0)
  , m_isVerbose(is_verbose)
  , m_interestLifeTime(interest_lifetime)
  , m_timeoutCount(0)
  , m_dataCount(0)
  , m_duplicateCount(0)
  , m_nextToPrint(0)
{
  m_ofs.open(file_name, std::ios::out | std::ios::binary);
  if (!m_ofs.is_open()) {
    std::cerr << "Open file: " << file_name << " failed!" << std::endl;
  }
}

void
Consumer::run()
{
  std::cout << "Run consumer with window size " << m_cwnd << std::endl;

  // Consumer starts by sending out the interest for the first segment.
  // e.g., /name/prefix/0
  beforeSendingIntrest(0, false);
  m_nextSegNum = 1;

  Interest interest(Name(m_prefix).appendSegment(0));
  interest.setInterestLifetime(time::milliseconds(m_interestLifeTime));
  interest.setMustBeFresh(true);
  m_face.expressInterest(interest,
                         bind(&Consumer::onDataFirstTime, this, _1, _2,
                              time::steady_clock::now()),
                         bind(&Consumer::onTimeout, this, _1, time::steady_clock::now()));

  // m_ioService.run() will block until all events finished or m_ioService.stop() is called
  time::steady_clock::TimePoint start = time::steady_clock::now();
  m_ioService.run();
  time::steady_clock::TimePoint end = time::steady_clock::now();
  Rtt time_elapsed = end - start;

  std::cout << "Packet loss rate: " << (double)m_timeoutCount/(m_lastSegNum+1) << std::endl;
  std::cout << "Number of data received: " << m_dataCount << std::endl;
  std::cout << "It takes " << time_elapsed.count() << " milliseconds to run consumer." << std::endl;

  int data_packet_size = content_size + header_overhead;

  double throughput1 =
    (m_dataCount * 8 * data_packet_size) / time_elapsed.count();
  double throughput2 =
    ((m_dataCount-m_duplicatesCount) * 8 * data_packet_size) / time_elapsed.count();

  std::cout << "Throughput with duplicates: " << throughput1 << " kbps" << std::endl;
  std::cout << "Throughput without duplicates: " << throughput2 << " kbps" << std::endl;

  writeInOrderData();
}

void
Consumer::beforeSendingIntrest(uint64_t segno, bool retx)
{
  m_inFlight++; // increment # of interests in the current window
}

void
Consumer::sendInterest(uint64_t segno)
{
  //std::cout << "sending out segment " << segno << std::endl;
  Interest interest(Name(m_prefix).appendSegment(segno));
  interest.setInterestLifetime(time::milliseconds(m_interestLifeTime));
  interest.setMustBeFresh(true);

  m_face.expressInterest(interest,
                         bind(&Consumer::onData, this, _1, _2,
                              time::steady_clock::now()),
                         bind(&Consumer::onTimeout, this, _1,
                              time::steady_clock::now()));
}

void
Consumer::onDataFirstTime(const Interest& interest, const Data& data,
                          const time::steady_clock::TimePoint& timeSent)
{
  uint64_t recv_segno = data.getName()[-1].toSegment();
  m_lastSegNum = data.getFinalBlockId().toSegment();
  if (m_isVerbose) {
    std::cout << "Segment received: " << recv_segno << std::endl;
    std::cout << "Expected last segment #: " << m_lastSegNum << std::endl;
  }

  afterReceivingData(recv_segno);

  m_validator.validate(data,
                       bind(&Consumer::onDataValidated, this, _1),
                       bind(&Consumer::onFailure, this, _2));

  schedulePackets();
}

void
Consumer::onData(const Interest& interest, const Data& data,
                 const time::steady_clock::TimePoint& timeSent)
{
  uint64_t recv_segno = data.getName()[-1].toSegment();
  if (m_isVerbose)
    std::cout << "Segment received: " << recv_segno << std::endl;

  afterReceivingData(recv_segno);

  m_validator.validate(data, bind(&Consumer::onDataValidated, this, _1),
                       bind(&Consumer::onFailure, this, _2));

  if ((m_recvList.size() - 1) >= m_lastSegNum) { // all interests have been acked
    if (m_isVerbose) {
      std::cout << "Stopping the consumer..." << std::endl;
    }
    stop(); // stop the consumer
  }

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
    std::cout << "A duplicate data packet received, segment # = " << recv_segno << std::endl;
    m_duplicateCount++;
  }

  if (m_inFlight > 0) {
    m_inFlight--;
  }
}

void
Consumer::schedulePackets()
{
  int avail_window_sz = m_cwnd - m_inFlight;

  while (avail_window_sz > 0) {
    if (m_nextSegNum > m_lastSegNum) {
      break;
    }
    beforeSendingIntrest(m_nextSegNum, false);
    if (m_isVerbose)
      std::cout << "Sending out next segment: "<< m_nextSegNum << std::endl;
    sendInterest(m_nextSegNum);
    m_nextSegNum += 1; // only increase for in order segement, not for retxed ones
    avail_window_sz--;
  }
}

void
Consumer::scheduleRetx()
{
  int avail_window_sz = m_cwnd - m_inFlight;

  while (avail_window_sz > 0) {
    if (m_retxQueue.size()) {
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
      avail_window_sz--;
    } else {
      break;
    }
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
  //writeInOrderData();
}

void
Consumer::onFailure(const std::string& reason)
{
  throw std::runtime_error(reason);
}

void
Consumer::onTimeout(const Interest& interest,
                    const time::steady_clock::TimePoint& timeSent)
{
  uint64_t segno = interest.getName()[-1].toSegment();
  Rtt time_elapsed = time::steady_clock::now() - timeSent;

  if (m_isVerbose)
    std::cout << "Timeout " << segno << ", time elapsed: "
              << time_elapsed.count() << " ms" << std::endl;

  m_timeoutCount++;
  if (m_inFlight > 0) {
    m_inFlight--;
  }

  m_retxQueue.push(segno);
  scheduleRetx();
}

void
Consumer::writeInOrderData()
{
  for (auto it = m_bufferedData.begin();
       it != m_bufferedData.end(); it = m_bufferedData.erase(it)) {

    const Block& content = it->second->getContent();
    m_ofs.write(reinterpret_cast<const char*>(content.value()), content.value_size());
  }
}

void
Consumer::stop()
{
  m_ioService.stop(); // stop I/O service
}

} // namespace examples
} // namespace ndn
