
#include <string>
#include <darma.h>

using namespace darma_runtime;
using namespace darma_runtime::experimental;

void darma_main_task(std::vector<std::string> args) {

  // create handle to string variable
  auto i = initial_access<int>();

  create_work_while([=]{
    return *i < 10;
  }).do_([=]{
    std::cout << "i = " << *i << std::endl;
    *i += 1;
  });

  create_work_if([=]{
    return *i == 10;
  }).then_(reads(i), [=]{
    std::cout << "Hello World!  i = " << *i << std::endl;
  });

  create_work_if([=]{
    return *i == 0;
  }).then_(reads(i), [=]{
    *i = 0;
    assert(false); // shouldn't get here
  });

}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
