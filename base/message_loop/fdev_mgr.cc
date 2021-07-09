#include "fdev_mgr.h"
#include <glog/logging.h>
#include <sys/resource.h>
#include "base/memory/lazy_instance.h"

namespace {

struct FdEventMgrImpl : public base::FdEventMgr {};

base::LazyInstance<FdEventMgrImpl> g_mgr;
}  // namespace

namespace base {

FdEventMgr& FdEventMgr::Get() {
  return g_mgr.get();
}

FdEventMgr::~FdEventMgr() {}

FdEventMgr::FdEventMgr() {
  struct rlimit limit;
  CHECK(0 == getrlimit(RLIMIT_NOFILE, &limit));
  evs_ = std::move(std::vector<FdEvent*>(limit.rlim_cur, nullptr));
}

FdEventMgr::Result FdEventMgr::Add(FdEvent* fdev) {
  int fd = fdev->GetFd();
  if (fd < 0 || fd >= evs_.size()) {
    LOG(ERROR) << "invalid fd:" << fd;
    return InvalidFD;
  }

  FdEvent* installed  = evs_[fd];
  if (nullptr != installed) {
    return Duplicated;
  }
  evs_[fd] = fdev;
  return Success;
}

FdEventMgr::Result FdEventMgr::Remove(FdEvent* fdev) {
  int fd = fdev->GetFd();
  if (fd < 0 || fd >= evs_.size()) {
    LOG(ERROR) << "remove fd:" << fd << " fails, out of bound";
    return InvalidFD;
  }
  FdEvent* installed  = evs_[fd];
  if (nullptr == installed) {
    LOG(ERROR) << "fd: " << fd << " not installed";
    return NotExist;
  }
  if (installed != fdev) {
    LOG(ERROR) << "fd:" << fd << " bind two fdevent object?";
    return Duplicated;
  }
  evs_[fd] = nullptr;
  return Success;
}

FdEvent* FdEventMgr::GetFdEvent(int fd) const {
  if (fd < 0 || fd >= evs_.size()) {
    LOG(ERROR) << "invalid fd:" << fd;
    return nullptr;
  }
  return evs_[fd];
}

}  // namespace base
