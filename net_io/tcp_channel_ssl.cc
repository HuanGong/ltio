#include <algorithm>

#include "base/utils/sys_error.h"
#include "net_io/socket_utils.h"
#include "tcp_channel_ssl.h"

namespace lt {
namespace net {

namespace {
const int32_t kBlockSize = 8 * 1024;

enum class SSLAction {
  Close,
  WaitIO,
  FastRetry,
};

// handle openssl error afater ssl operation
// return value mean wheather should handle close event
void print_openssl_err() {
  int err;
  char err_str[128] = {0};
  while ((err = ERR_get_error())) {
    ERR_error_string_n(err, &err_str[0], arraysize(err_str));
    LOG(ERROR) << "ssl err:" << err_str;
  }
}

SSLAction handle_openssl_err(base::FdEvent* ev, int err) {
  bool dump_err = false;
  SSLAction action = SSLAction::Close;
  switch (err) {
    case SSL_ERROR_WANT_READ: {
      ev->EnableReading();
      action = SSLAction::WaitIO;
    } break;
    case SSL_ERROR_WANT_WRITE: {
      ev->EnableWriting();
      action = SSLAction::WaitIO;
    } break;
    case SSL_ERROR_ZERO_RETURN: {  // peer close write
      VLOG(google::GLOG_INFO) << "peer closed" << base::StrError();
      action = SSLAction::Close;
    } break;
    case SSL_ERROR_SYSCALL: {
      if (errno == EAGAIN) {
        action = SSLAction::WaitIO;
      } else if (errno == EINTR) {
        action = SSLAction::FastRetry;
      } else {
        dump_err = true;
        action = SSLAction::Close;
      }
    } break;
    default: {
      dump_err = true;
      action = SSLAction::Close;
    } break;
  }

  if (dump_err) {
    LOG(ERROR) << "ssl err:" << err << ", errno:" << base::StrError();
    print_openssl_err();
  }
  VLOG_IF(google::GLOG_INFO, action == SSLAction::Close) << "ssl ask for close";
  return action;
}

}  // namespace

// static
TCPSSLChannelPtr TCPSSLChannel::Create(int socket_fd,
                                       const IPEndPoint& local,
                                       const IPEndPoint& peer) {
  return TCPSSLChannelPtr(new TCPSSLChannel(socket_fd, local, peer));
}

TCPSSLChannel::TCPSSLChannel(int socket_fd,
                             const IPEndPoint& loc,
                             const IPEndPoint& peer)
  : SocketChannel(socket_fd, loc, peer) {
  fd_event_->EnableWriting();
  fd_event_->SetEdgeTrigger(true);
  socketutils::KeepAlive(socket_fd, true);
}

TCPSSLChannel::~TCPSSLChannel() {
  VLOG(GLOG_VTRACE) << __FUNCTION__ << "@" << this << " Gone";
  SSL_free(ssl_);
}

void TCPSSLChannel::InitSSL(SSLImpl* ssl) {
  CHECK(ssl);
  ssl_ = ssl;
  int fd = SSL_get_fd(ssl);
  if (fd <= 0) {
    SSL_set_fd(ssl, fd_event_->GetFd());
  }
}

void TCPSSLChannel::StartChannel() {
  fd_event_->EnableReading();
  pump_->InstallFdEvent(fd_event_.get());

  SetChannelStatus(Status::CONNECTED);
  reciever_->OnChannelReady(this);

  if (reciever_->IsServerSide()) {
    SSL_set_accept_state(ssl_);
  } else {
    SSL_set_connect_state(ssl_);
  }
  OnHandshake(fd_event_.get());
}

int32_t TCPSSLChannel::Send(const char* data, const int32_t len) {
  DCHECK(pump_->IsInLoop());

  if (out_buffer_.CanReadSize() > 0) {
    out_buffer_.WriteRawData(data, len);
    HandleWrite(fd_event_.get());
    return 0;  // all write to buffer, zero write to fd
  }

  size_t n_write = 0;
  size_t n_remain = len;
  SSLAction action = SSLAction::WaitIO;
  do {
    int batch_size = std::min(size_t(kBlockSize), n_remain);

    ERR_clear_error();
    int ret = SSL_write(ssl_, data + n_write, batch_size);
    if (ret > 0) {
      n_write += ret;
      n_remain = n_remain - ret;

      VLOG(GLOG_VTRACE) << "ssl writer:" << ret << " bytes";
      DCHECK((n_write + n_remain) == size_t(len));
      continue;
    }

    int err = SSL_get_error(ssl_, ret);
    action = handle_openssl_err(fd_event_.get(), err);
    if (action == SSLAction::FastRetry) {
      VLOG(GLOG_VTRACE) << "nintr, fast try again";
      continue;
    }
    LOG_IF(ERROR, action == SSLAction::Close)
      << "ssl write lib need close, " << ChannelInfo();
    break;
  } while (1);

  if (action == SSLAction::Close) {
    schedule_shutdown_ = true;
    SetChannelStatus(Status::CLOSING);
  }
  return action == SSLAction::Close ? -1 : n_write;
}

bool TCPSSLChannel::OnHandshake(base::FdEvent* event) {
again:
  ERR_clear_error();
  int ret = SSL_do_handshake(ssl_);
  if (ret > 0) {  // handshake finish
    SetChannelStatus(Status::CONNECTED);
    reciever_->OnChannelReady(this);
    VLOG(GLOG_VTRACE) << "ssl finish handshake";
    return true;
  }
  int err = SSL_get_error(ssl_, ret);
  SSLAction action = handle_openssl_err(event, err);
  VLOG_IF(GLOG_VTRACE, action == SSLAction::Close) << "ssl failed handshake";
  VLOG_IF(GLOG_VTRACE, action == SSLAction::WaitIO) << "ssl continue handshake";

  if (action == SSLAction::Close) {
    return HandleClose(event);
  } else if (action == SSLAction::FastRetry) {
    goto again;
  }
  return true;
}

bool TCPSSLChannel::HandleWrite(base::FdEvent* event) {
  VLOG(GLOG_VTRACE) << ChannelInfo() << " handle write";

  SSLAction action = SSLAction::WaitIO;
  while (out_buffer_.CanReadSize()) {
    ERR_clear_error();
    int batch_size = std::min(uint64_t(kBlockSize), out_buffer_.CanReadSize());
    ssize_t retv = SSL_write(ssl_, out_buffer_.GetRead(), batch_size);
    if (retv > 0) {
      out_buffer_.Consume(retv);
      VLOG(GLOG_VTRACE) << ChannelInfo() << " write:" << retv;
      continue;
    }

    int err = SSL_get_error(ssl_, retv);
    action = handle_openssl_err(event, err);
    if (action == SSLAction::FastRetry) {
      continue;
    }
    break;
  };
  VLOG(GLOG_VTRACE) << "after write, out buf size:" << out_buffer_.CanReadSize();

  if (action == SSLAction::Close) {
    return HandleClose(event);
  }

  if (out_buffer_.CanReadSize() == 0) {
    reciever_->OnDataFinishSend(this);
    if (schedule_shutdown_) {
      return HandleClose(event);
    }
  }
  return true;
}

bool TCPSSLChannel::HandleRead(base::FdEvent* event) {
  VLOG(GLOG_VTRACE) << ChannelInfo() << " handle read";

  int bytes_read;
  VLOG(GLOG_VTRACE) << "before bytes in buffer:" << in_buffer_.CanReadSize();

  SSLAction action = SSLAction::WaitIO;
  do {
    in_buffer_.EnsureWritableSize(2048);

    ERR_clear_error();
    bytes_read = SSL_read(ssl_, in_buffer_.GetWrite(), in_buffer_.CanWriteSize());
    VLOG(GLOG_VTRACE) << "ssl_read ret:" << bytes_read;
    if (bytes_read > 0) {
      in_buffer_.Produce(bytes_read);
      continue;
    }

    int err = SSL_get_error(ssl_, bytes_read);
    action = handle_openssl_err(event, err);
    if (action == SSLAction::FastRetry) {
      continue;
    }
    break;
  } while (1);
  VLOG(GLOG_VTRACE) << "after read bytes in buffer:" << in_buffer_.CanReadSize();

  if (in_buffer_.CanReadSize()) {
    VLOG(GLOG_VTRACE) << "ssl read size:" << in_buffer_.CanReadSize()
     << ", content:" << in_buffer_.AsString();
    reciever_->OnDataReceived(this, &in_buffer_);
  }

  if (action == SSLAction::Close) {
    SetChannelStatus(Status::CLOSING);
    return HandleClose(event);
  }
  return true;
}

}  // namespace net
}  // namespace lt
