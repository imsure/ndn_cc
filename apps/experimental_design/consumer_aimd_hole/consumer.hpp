#include "../common.hpp"
#include <ndn-cxx/security/validator-null.hpp>
#include <fstream>
#include <utility> // std::pair
#include <queue>

namespace ndn {
namespace file_transfer {

struct Parameters {
  double init_cwnd; // initial congestion window size
  double init_ssthresh; // initial slow start threshold
  double md_coef; // multiplicative decrease coefficient
  double ai_step; // additive increase step
  int max_num_ood; // max number of consecutively out-of-order data
  int rto_backoff_multiplier;
  double min_rto; // min of retransmission timeout
  double max_rto;
  double alpha; // filter gain
  double beta; // filter gain
  int k; // coefficient for calculating RTO
  double G; // clock granularity
  int cwnd_record_interval; // time interval(in ms) to record cwnd size
  int retxtimer_check_interval; // time interval(in ms) to check retx timer
};

// RTT in milliseconds
typedef time::duration<double, time::milliseconds::period> Rtt;

// Ethernet header (16) + IP header (20) + UDP header (8)
// + NDN overhead (381, name, signature...)
const int header_overhead = 425;
const int content_size = 1024; // size of real content

class Consumer : noncopyable
{
public:
  Consumer(const Name& prefix, const std::string file_name,
           struct Parameters& params, bool is_verbose = false,
           bool hole_detection = false);

  /**
   * Run consumer.
   */
  void run();

private:
  /**
   * Callback to be called when a matching 'data' packet
   * for 'interest' is received.
   */
  void
  onData(const Interest& interest, const Data& data,
         const time::steady_clock::TimePoint& timeSent);

  void
  onTimeout(int timeout_count);

  /**
   * Send out an interest packet with segment # 'segno'.
   */
  void
  sendInterest(uint64_t segno, bool retx);

  /**
   * Return the next segment # to be sent out in order.
   * This number will be increasing monotonically (0,1,2...N).
   */
  uint64_t
  getNextSegmentNumber() const;

  /**
   * Return the segment # we expect to be returned.
   * Assume that data will be returned in order.
   * e.g., if we sent out interest I1,I2,I3, then we would
   * expect data to be returned in the order of D1,D2,D3.
   */
  uint64_t
  getExpectedSegmentNum() const;

  void
  onDataValidated(shared_ptr<const Data> data);

  void
  onFailure(const std::string& reason);

  /**
   * Write the received data to file.
   */
  void
  writeInOrderData();

  /**
   * Called before sending out an interest packet.
   * Perform necessary bookkeeping.
   */
  void
  beforeSendingInterest(uint64_t segno, bool retx);

  void
  schedulePackets();

  bool
  dataReceived(uint64_t segno);

  void
  checkRetxTimer();

  /**
   * Only get called when the first data packet comes back so we can
   * set up some necessary parameters, such as smoothed RTT, RTT variance
   * and last segment number of the transferred file, etc.
   */
  void
  onDataFirstTime(const Interest& interest, const Data& data,
                  const time::steady_clock::TimePoint& timeSent);

  /**
   * Called once a data packet recived.
   */
  void
  afterReceivingData(uint64_t recv_segno);

  /**
   * Estimate RTT based on current measurement 'rtt', in milliseconds.
   * This function also update RTO.
   */
  void
  rttEstimator(double rtt);

  /**
   * Write statistics data to files.
   */
  void
  writeStats();

  void
  recordCwndSize();

  /**
   * Stop the consumer.
   */
  void
  stop();

private:
  /* I/O */
  boost::asio::io_service m_ioService; // shared between Face and Scheduler
  Name m_prefix;
  Face m_face;
  ValidatorNull m_validator;
  Scheduler m_scheduler;
  scheduler::ScopedEventId m_retxEvent;
  scheduler::ScopedEventId m_recordCwndEvent;

  /* For congestion control */
  double m_cwnd; // Congestion window size
  double m_ssthresh; // slow start threshold
  int m_inFlight; // # of interests currently in congestion windows

  /* RTT & RTO */
  double m_sRtt; // smoothed round-trip time, in milliseconds
  double m_rttVar; // round-trip time variation, in milliseconds
  double m_rto; // retransmission timeout, in milliseconds

  // maps a segment number to <time when it was sent, rto for the segment>
  std::map<uint64_t, std::pair<time::steady_clock::TimePoint, double>> m_timeSent;

  std::map<uint64_t, uint32_t> m_nonceMap;

  /* For interests pipeline */
  uint64_t m_nextSegNum; // next segment number to be send in order, not include retxed ones
  uint64_t m_expectedSegNum; // the expected segment number, assuming arriving in order
  uint64_t m_lastSegNum; // last expected segment number

  // it holds the segement# of sent interests in order (0,1,2,...,N)
  // (not include retransmitted interests).
  // once interests get acked by data, remove it from the vector.
  // the first element of the list is the expected segment# of data packet
  // to be returned (assuming that data comes back in order).
  std::list<uint64_t> m_sentList;

  // it holds the segement# of interests that have been retransmitted.
  // once acked with data, the entry will be removed.
  std::list<uint64_t> m_retxList;

  // it holds the segment# that arrived out of order.
  // used for packet sequence hole detection (for fast retransmission/recovery)
  std::list<uint64_t> m_outOfOrderList;

  // a queue of segement# that need to be retransmitted.
  // Retransmission could happen when an interest timed out or a hole
  // in packet sequence detected.
  std::queue<uint64_t> m_retxQueue;

  // it holds the list of received segment #.
  // we use it to determine when file transfer has been completed.
  std::vector<uint64_t> m_recvList;

  /* For writing data to file */
  int m_nextToPrint;
  std::map<uint64_t, shared_ptr<const Data>> m_bufferedData;
  std::fstream m_ofs; // output file stream

  /* Options, paramters */
  struct Parameters m_params;
  bool m_isVerbose;
  bool m_holeDetection;
  bool m_inSlowStartPhase;

  /* For statistics */
  int m_timeoutCount; // number of timed out packets
  int m_dataCount; // number of data packets received
  int m_holeCount;
  int m_duplicatesCount; // number of duplicated data packets received

  // Time series for congestion window
  std::vector<std::pair<time::steady_clock::TimePoint, double>> m_cwndTimeSeries;

  time::steady_clock::TimePoint m_startTime;
  std::vector<std::pair<double, std::pair<double, double>>> m_rttrto;
  std::vector<std::pair<double, int>> m_timeoutRec;

  time::steady_clock::TimePoint start_time;
};

} // namespace examples
} // namespace ndn
