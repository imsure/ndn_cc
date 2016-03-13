#include "../common.hpp"
#include "producer.hpp"
#include <fstream>

namespace ndn {
namespace file_transfer {

static int
main(int argc, const char *argv[])
{
  std::string prog_name = argv[0];
  std::string prefix;
  std::string file_name;
  std::string signing_str;
  uint64_t freshness_period = 10000;
  //size_t max_seg_size = MAX_NDN_PACKET_SIZE >> 4;
  size_t max_seg_size = 1024;
  std::ifstream ifs;

  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
    ("help,h", "Help screen")
    ("prefix,p", po::value<std::string>(&prefix), "NDN name prefix for the served content")
    ("file,f", po::value<std::string>(&file_name), "Name of the file to be served")
    ("freshness,f", po::value<uint64_t>(&freshness_period)->default_value(freshness_period),
     "freshness period, in milliseconds")
    ("segment_size,s", po::value<size_t>(&max_seg_size)->default_value(max_seg_size),
     "maximum segment size, in bytes");

  po::positional_options_description pos_desc;
  pos_desc.add("prefix", 1);
  pos_desc.add("file", 2);

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(desc).positional(pos_desc).run(), vm);
    po::notify(vm);
  }
  catch (const po::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return -1;
  }
  catch (const boost::bad_any_cast& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return -1;
  }

  if (vm.count("help") > 0) {
    std::cout << "Usage: " << prog_name <<
      " [options] /ndn/name/prefix /path/to/file" << std::endl;
    std::cout << desc;
    return 0;
  }

  if (prefix.empty() || file_name.empty()) {
    std::cerr << "Usage: " << prog_name <<
      " [options] /ndn/name/prefix /path/to/file" << std::endl;
    std::cerr << desc;
    return -1;
  }

  ifs.open(file_name, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    std::cerr << file_name << " does not exists!" << std::endl;
    return -1;
  }

  std::cout << "prefix: " << prefix << std::endl;
  std::cout << "file: " << file_name << std::endl;

  security::SigningInfo signing_info;
  try {
    signing_info = security::SigningInfo(signing_str);
  }
  catch (const std::invalid_argument& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 2;
  }

  try {
    Face face;
    KeyChain key_chain;
    Producer producer(prefix, face, key_chain, signing_info,
                      time::milliseconds(freshness_period),
                      max_seg_size, ifs);
    producer.run();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return -1;
  }
}

} // namespace file_transfer
} // namespace ndn

int
main(int argc, const char *argv[])
{
  ndn::file_transfer::main(argc, argv);
}
