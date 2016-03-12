#include "producer.hpp"
#include <ctime>

namespace ndn {
namespace file_transfer {

Producer::Producer(const Name& prefix,
                   Face& face,
                   KeyChain& keyChain,
                   const security::SigningInfo& signingInfo,
                   time::milliseconds freshnessPeriod,
                   size_t maxSegmentSize,
                   std::ifstream& ifs)
  : m_prefix(prefix)
  , m_face(face)
  , m_keyChain(keyChain)
  , m_signingInfo(signingInfo)
  , m_freshnessPeriod(freshnessPeriod)
  , m_maxSegmentSize(maxSegmentSize)
{
  std::time_t start = std::time(NULL);
  std::cout << "Populating segments" << std::endl;
  populateSegments(ifs);
  std::cout << "End of populating segments" << std::endl;

  m_face.setInterestFilter(m_prefix,
                           bind(&Producer::onInterest, this, _2),
                           RegisterPrefixSuccessCallback(),
                           bind(&Producer::onRegisterFailed, this, _1, _2));

  std::time_t end = std::time(NULL);
  std::cout << "It takes " << end - start << " seconds to initiliaze producer." << std::endl;
  std::cout << "Data published with name: " << m_prefix << std::endl;
}

void
Producer::run()
{
  m_face.processEvents();
}

void
Producer::onInterest(const Interest& interest)
{
  BOOST_ASSERT(m_segments.size() > 0);

  //  std::cout << "Interest: " << interest << std::endl;

  const Name& name = interest.getName();
  shared_ptr<Data> data;

  // is this a discovery Interest or a sequence retrieval?
  if (name.size() == m_prefix.size() + 1 && m_prefix.isPrefixOf(name) &&
      name[-1].isSegment()) {
    const auto segmentNo = static_cast<size_t>(interest.getName()[-1].toSegment());
    // specific segment retrieval
    if (segmentNo < m_segments.size()) {
      data = m_segments[segmentNo];
    }
  }
  else if (interest.matchesData(*m_segments[0])) {
    // Interest has version and is looking for the first segment or has no version
    data = m_segments[0];
  }

  if (data != nullptr) {
    // std::cout << "Data: " << *data << std::endl;

    m_face.put(*data);
  }
}

void
Producer::onRegisterFailed(const Name& prefix, const std::string& reason)
{
  std::cerr << "ERROR: Failed to register prefix '"
            << prefix << "' (" << reason << ")" << std::endl;
  m_face.shutdown();
}

void
Producer::populateSegments(std::ifstream& ifs)
{
  BOOST_ASSERT(m_segments.size() == 0);

  // calculate how many segments are needed
  std::streampos begin, end;
  begin = ifs.tellg();
  ifs.seekg(0, std::ios::end);
  end = ifs.tellg();

  int num_segments = (end-begin) / m_maxSegmentSize;
  if ((end-begin) % m_maxSegmentSize != 0)
    num_segments++;

  std::cout << "Size of the file: " << (end-begin) << " bytes." << std::endl;
  std::cout << "Maximum size of a segment: " << m_maxSegmentSize << " bytes." << std::endl;
  std::cout << "Number of segments: " << num_segments << std::endl;

  std::vector<uint8_t> buffer(m_maxSegmentSize);
  ifs.seekg(0, std::ios::beg);
  for (int i = 0; i < (end-begin) / m_maxSegmentSize; ++i) {
    ifs.read(reinterpret_cast<char*>(buffer.data()), m_maxSegmentSize);
    auto data = make_shared<Data>(Name(m_prefix).appendSegment(i));
    data->setFreshnessPeriod(m_freshnessPeriod);
    data->setContent(&buffer[0], m_maxSegmentSize);
    //std::cout << *data << std::endl;
    m_segments.push_back(data);
  }

  if ((end-begin) % m_maxSegmentSize != 0) {
    ifs.read(reinterpret_cast<char*>(buffer.data()), (end-begin) % m_maxSegmentSize);
    auto data = make_shared<Data>(Name(m_prefix).appendSegment(m_segments.size()));
    data->setFreshnessPeriod(m_freshnessPeriod);
    data->setContent(&buffer[0], (end-begin) % m_maxSegmentSize);
    //std::cout << *data << std::endl;
    m_segments.push_back(data);
  }

  auto finalBlockId = name::Component::fromSegment(m_segments.size() - 1);
  for (const auto& data : m_segments) {
    data->setFinalBlockId(finalBlockId);
    m_keyChain.sign(*data, m_signingInfo);
  }
}

} // namespace file_transfer
} // namespace ndn
