
#include <string>
#include <darma.h>
#include <chrono>
#include <random>

using namespace darma_runtime;

void darma_main_task(std::vector<std::string> args) {

  auto i_comm = commutative_access<int>();

  for(int j = 1; j <= 10; ++j) {

    auto j_computed = initial_access<int>();
    create_work([=]{
      *j_computed = j;

      // Simulate some work of variable time
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(100, 1000);
      std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
    });

    create_work(reads(j_computed), [=]{
      *i_comm += *j_computed;
      std::cout << "Adding " << *j_computed << ", total is now " << *i_comm << std::endl;
    });
  }

  auto i = noncommutative_access_to_handle(std::move(i_comm));

  create_work([=]{
    std::cout << "Total: " << *i << std::endl;
    assert(*i == 55);
  });

}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
