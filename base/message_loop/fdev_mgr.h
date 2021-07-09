#ifndef LT_BASE_FDEV_MGR_H_
#define LT_BASE_FDEV_MGR_H_

#include <vector>
#include "base/message_loop/fd_event.h"

namespace base {

class FdEventMgr {
public:
  enum Result {
    Success,
    NotExist,
    InvalidFD,
    Duplicated,
  };
  ~FdEventMgr();

  static FdEventMgr& Get();

  Result Add(FdEvent* fdev);

  Result Remove(FdEvent* fdev);

  FdEvent* GetFdEvent(int fd) const; 
protected:
  FdEventMgr();

private:
  std::vector<FdEvent*> evs_; 
};

}  // namespace base
#endif
