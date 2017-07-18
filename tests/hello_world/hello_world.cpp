
#include <string>
#include <darma.h>

using namespace darma_runtime;

void darma_main_task(std::vector<std::string> args) {

  // create handle to string variable
  auto i = initial_access<int>();

  std::cout << "Hello World from top-level task" << std::endl;
  create_work([=]{
    i.set_value(0xFEEDFACE);
  });

  create_work([=]{
    *i = 42;
    create_work([=]{
      std::cout << "Nested Hello World, should be 42: " << *i << std::endl;
      assert(*i==42);
      *i = 35;
    });
    create_work([=]{
      std::cout << "Nested Hello World, should be 35: " << *i << std::endl;
      assert(*i==35);
      *i = 27;
    });
    create_work(reads(i), [=]{
      std::cout << "Nested Hello World, should be 27: " << *i << std::endl;
      assert(*i==27);
    });
  });

  create_work([=]{
    std::cout << "Hello World, should be 27: " << *i << std::endl;
    assert(*i==27);
    *i = 33;
    std::cout << "Hello World, should be 33: " << *i << std::endl;
    assert(*i==33);
  });

  create_work([=]{
    *i = 1;
    create_work([=]{
      create_work([=]{
        create_work([=]{
          create_work([=]{
            create_work(reads(i), [=]{
              std::cout << "Nested Hello World, should be 1: " << *i << std::endl;
              assert(*i==1);
            });
            create_work([=]{
              std::cout << "Nested Hello World, should be 1: " << *i << std::endl;
              assert(*i==1);
              *i = 0xFEEDFACE;
            });
          });
        });
      });
    });
  });

  create_work([=]{
    assert(*i==0xFEEDFACE);
    *i = 2;
    std::cout << "Hello World, should be 2: " << *i << std::endl;
    assert(*i==2);
  });



}

DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
