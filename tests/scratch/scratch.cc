
#include <random>
#include <darma.h>
#include <darma/impl/task_collection/handle_collection.h>
#include <darma/impl/task_collection/task_collection.h>
#include <darma/impl/array/index_range.h>
using namespace darma_runtime;
using namespace darma_runtime::keyword_arguments_for_access_handle_collection;
using namespace darma_runtime::experimental;


//-----------------------------------------------------------------
template<typename T, typename range_t = Range1D<int>>
class distVectorGhost
{
  private:
    range_t myRange_;
    darma_runtime::AccessHandleCollection<std::vector<T>, range_t> data_;
  public:
    distVectorGhost(distVectorGhost const&) = default;
    distVectorGhost(distVectorGhost&&) = default;
    distVectorGhost(const range_t & range)
    {
      myRange_ = range;
      data_ = initial_access_collection<std::vector<T>>(index_range=range);
    }
    ~distVectorGhost()
    {}

    AccessHandleCollection<std::vector<T>, range_t> getData(){
      return data_;
    }
};
//-----------------------------------------------------------------

template<typename T, typename range_t>
struct product{
  void operator()(
    Index1D<size_t> index,
    AccessHandleCollection<std::map<size_t,std::vector<T>>, range_t> dataAHC,
    AccessHandleCollection<std::vector<T>, range_t> xAHC) const
  {
    //...
  }
};
//-----------------------------------------------------------------

template<typename T, typename range_t = Range1D<size_t>>
class distMatrix
{
  private:
    using data_t = std::map<size_t,std::vector<T>>;
    range_t myRange_;
    AccessHandleCollection<data_t, range_t> data_;
    AccessHandleCollection<size_t, range_t> collecCount_;
  public:
    distMatrix(distMatrix const&) = default;
    distMatrix(distMatrix&&) = default;
    distMatrix(const range_t & range)
    {
      myRange_ = range;
      data_ = initial_access_collection<data_t>(index_range=range);
      collecCount_ = initial_access_collection<size_t>(index_range=range);
    }
    ~distMatrix(){}
  public:
    void multiply(distVectorGhost<T,range_t> x) const
    {
      create_concurrent_work<product<T,range_t>>(data_,x.getData(),index_range=myRange_);
    }
};
//-----------------------------------------------------------------


void darma_main_task(std::vector<std::string> args)
{
  auto r1d = Range1D<size_t>(4);

  distMatrix<double,Range1D<size_t>> A(r1d);
  distVectorGhost<double,Range1D<size_t>> p(r1d);

  auto converged = initial_access<bool>();
  create_work([=]{
    converged.set_value(false);
  });
  create_work_while([=]{
    return converged.get_value() == false;
  }).do_([=]
  {
    A.multiply(p);
    create_work([=]{
      converged.set_value(true);
    });
  });
  // ---------------------------------------------------

}
DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);
