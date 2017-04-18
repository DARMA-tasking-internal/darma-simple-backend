
#include <string>
#include <darma.h>

using namespace darma_runtime;

void darma_main_task(std::vector<std::string> args) {

  // create handle to string variable
  auto i = initial_access<int>();

  std::cout << "Hello World from top-level task" << std::endl;

  create_work([=]{
    *i = 42;
  });

  create_work([=]{
    std::cout << "Hello World " << *i << std::endl;
  });

}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
