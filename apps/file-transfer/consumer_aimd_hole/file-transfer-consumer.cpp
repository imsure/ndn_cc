#include "../common.hpp"
#include "consumer.hpp"
#include <fstream>

namespace ndn {
namespace file_transfer {

static int
main(int argc, const char *argv[])
{
  std::string prog_name = argv[0];
  std::string prefix;
  std::string file_name;
  std::fstream ofs;
  struct Parameters params;
  bool is_verbose = false;
  bool hole_detection = false;

  // setup parameters
  // TODO: make them command line options
  params.init_cwnd = 1.0;
  params.init_ssthresh = 200.0;
  params.md_coef = 0.5;
  params.ai_step = 1.0; // additive increase step (unit: segment)
  params.max_num_ood = 5;
  params.rto_backoff_multiplier = 2;
  params.min_rto = 1000; // 1000 milliseconds(1 second)
  params.max_rto = 2000; // ms
  params.alpha = 0.125;
  params.beta = 0.25;
  params.k = 4;
  params.G = 0.1;
  params.cwnd_record_interval = 10; // ms
  params.retxtimer_check_interval = 10; // ms

  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
    ("help,h", "Help screen")
    ("prefix,p", po::value<std::string>(&prefix), "NDN name prefix for the requested content")
    ("file,f", po::value<std::string>(&file_name),
     "Name of the file where the requested content will be stored")
    ("hole,o", po::bool_switch(&hole_detection), "turn on sequence hole detection")
    ("verbose,v", po::bool_switch(&is_verbose), "turn on verbose output");

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

  std::cout << "prefix: " << prefix << std::endl;
  std::cout << "file: " << file_name << std::endl;

  try {
    Consumer consumer(prefix, file_name, params, is_verbose, hole_detection);
    consumer.run();
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
