#ifndef NDN_FILE_TRANSFER_PRODUCER_HPP
#define NDN_FILE_TRANSFER_PRODUCER_HPP

#include "../common.hpp"
#include <fstream>

namespace ndn {
namespace file_transfer {
class Producer : noncopyable
{
public:
  /**
   * @brief Create the Producer
   *
   * @prefix name prefix used to publish data.
   */
  Producer(const Name& prefix, Face& face, KeyChain& keyChain,
           const security::SigningInfo& signingInfo,
           time::milliseconds freshnessPeriod,
           size_t maxSegmentSize, std::ifstream& ifs);

  /**
   * @brief Run the Producer
   */
  void run();

PUBLIC_WITH_TESTS_ELSE_PRIVATE:
  std::vector<shared_ptr<Data>> m_segments;

private:
  void populateSegments(std::ifstream& ifs);

  void
  onInterest(const Interest& interest);

  void
  onRegisterFailed(const Name& prefix, const std::string& reason);

private:
  Name m_prefix;
  Face& m_face;
  KeyChain& m_keyChain;
  security::SigningInfo m_signingInfo;
  time::milliseconds m_freshnessPeriod;
  size_t m_maxSegmentSize;
};

} // namespace file_transfer
} // namespace ndn

#endif // NDN_FILE_TRANSFER_PRODUCER_HPP
