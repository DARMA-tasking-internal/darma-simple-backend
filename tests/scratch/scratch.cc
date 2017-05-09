
#include <random>
#include <darma.h>
#include <darma/impl/task_collection/handle_collection.h>
#include <darma/impl/task_collection/task_collection.h>
#include <darma/impl/array/index_range.h>
using namespace darma_runtime;
using namespace darma_runtime::keyword_arguments_for_access_handle_collection;
using darma_runtime::keyword_arguments_for_publication::version;
using namespace darma_runtime::experimental::backend_hint;
using namespace darma_runtime::keyword_arguments_for_task_creation;

using colind_t = std::map<size_t,std::vector<size_t>>;
using data_t = std::map<size_t,std::vector<double>>;
using range_t = Range1D<size_t>;



class dmapSimple
{
  private:
    using VecInt = std::vector<size_t>;
    using pieceID = size_t;
    std::string gigi;
    size_t NumGlobalElements_;
    std::map<pieceID,size_t> NumElementsPerPiece_;
    std::map<pieceID,VecInt> globalIndices_;
    std::map<pieceID,VecInt> localIndices_;
  public:
   using Archive = darma_runtime::serialization::SimplePackUnpackArchive;
   void serialize(Archive& ar) {
      ar | gigi | NumGlobalElements_ | NumElementsPerPiece_ |
        globalIndices_ | localIndices_;
    }
  public:
    dmapSimple(){}
    ~dmapSimple(){}

  //-------------
  void initialize(size_t numImages, size_t NumGlobalElements)
  {
    if ( numImages > NumGlobalElements ){
      std::cerr << "***ERROR***: # of processes > # of elements to distribute! " << '\n';
      std::cerr << " " __FILE__ << ":" << __LINE__ << '\n';
      exit( EXIT_FAILURE );
    }
    NumGlobalElements_ = NumGlobalElements;
    // remainder will be fitted into the last process
    size_t remaind = (size_t) (NumGlobalElements % numImages);

    for (size_t i = 0; i < numImages; ++i){
      size_t numElementsPerRank = (size_t) NumGlobalElements/numImages;
      size_t start_index = i * (numElementsPerRank + 1);
      if (i < remaind)
        numElementsPerRank++;
      else
        start_index -= (i - remaind);
      // create the global IDs
      VecInt MyGlobalIndices(numElementsPerRank);
      std::iota(MyGlobalIndices.begin(), MyGlobalIndices.end(), start_index);
      // store local info
      size_t NumMyElements = MyGlobalIndices.size();

      // create the local IDs
      VecInt MyLocalIndices(NumMyElements);
      std::iota(MyLocalIndices.begin(), MyLocalIndices.end(), 0);

      // store
      NumElementsPerPiece_[i] = NumMyElements;
      globalIndices_[i] = MyGlobalIndices;
      localIndices_[i] = MyLocalIndices;
    }
  }
  VecInt getLocalIndicesOfPieceID(size_t index) const{
    return localIndices_.find(index)->second;
  }
  VecInt getGlobalIndicesOfPieceID(size_t index) const{
    return globalIndices_.find(index)->second;
  }
  size_t getNumberOfElementsOfPieceID(size_t index) const{
    return NumElementsPerPiece_.find(index)->second;
  }
  size_t getNumberOfGlobalElements() const{
    return NumGlobalElements_;
  }
};

template<typename T, typename range_t>
struct tridiagFillOnes{
    void operator()(
            Index1D<size_t> index,
            dmapSimple const& myMap,
            AccessHandleCollection<std::vector<size_t>, range_t> nonZeroPerRow,
            AccessHandleCollection<std::map<size_t,std::vector<size_t>>, range_t> colIndices,
            AccessHandleCollection<std::map<size_t,std::vector<T>>, range_t> data) const
    {
      const size_t me = index.value;
      const size_t cntxtSize = index.max_value + 1;
      //------
      auto & myNonZeroPerRow = *(nonZeroPerRow[index].local_access());
      auto & myColInd = *(colIndices[index].local_access());
      auto & myData = *(data[index].local_access());

      /* tridiagonal matrix of size NxN has:
          2 entries for row 0,
          3 entries for rows 1 ... N-2
          2 entries for row N-1
      */
      const size_t myNumRows = myMap.getNumberOfElementsOfPieceID(me);
      const size_t numGlobalElements = myMap.getNumberOfGlobalElements();
      const std::vector<size_t> MyGlobalElements = myMap.getGlobalIndicesOfPieceID(me);

      myNonZeroPerRow.clear();
      myColInd.clear();
      myData.clear();
      // Add rows one-at-a-time
      std::vector<size_t> Indices;
      std::vector<T> Values;
      size_t jj=0;
      for (auto const & it : MyGlobalElements){
        if (it == 0){
          Indices = {it,it+1};
          Values = {1.,1.};
          myNonZeroPerRow.push_back(2);
        }
        else if (it == numGlobalElements-1){
          Indices = {it-1,it};
          Values = {1.,1.};
          myNonZeroPerRow.push_back(2);
        }
        else{
          Indices = {it-1, it, it+1};
          Values = {1.,1.,1.};
          myNonZeroPerRow.push_back(3);
        }
        myColInd[jj] = Indices;
        myData[jj] = Values;
        jj++;
      }
    }
};
//---------------------------------------------


void darma_main_task(std::vector<std::string> args)
{
  const size_t NN = std::atoi(args[1].c_str());           //number of repetitions
  const size_t sizePerRank = std::atoi(args[2].c_str());  //size per rank
  const size_t decompSize = std::atoi(args[3].c_str());   //decomp size
  auto r1d = Range1D<size_t>(decompSize);

  auto mapObj = initial_access<dmapSimple>();
  size_t matSize = sizePerRank * decompSize;
  create_work([=]{
    mapObj->initialize(decompSize, matSize);
  });

  for (size_t i = 0; i < NN; ++i)
  {
    auto nonZeroPerRow = initial_access_collection<std::vector<size_t>>(index_range=r1d);
    auto colIndices = initial_access_collection<colind_t>(index_range=r1d);
    auto data = initial_access_collection<data_t>(index_range=r1d);

    create_concurrent_work<tridiagFillOnes<double,range_t>>(
      mapObj.shared_read(), nonZeroPerRow, colIndices, data,
      index_range=r1d
    );
  }
}
DARMA_REGISTER_TOP_LEVEL_FUNCTION(darma_main_task);

