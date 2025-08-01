/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file rpc_session.cc
 * \brief RPC session for remote function call.
 */
#include "rpc_endpoint.h"

#include <tvm/ffi/function.h>
#include <tvm/runtime/base.h>
#include <tvm/runtime/device_api.h>
#include <tvm/runtime/serializer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../support/arena.h"
#include "../../support/ring_buffer.h"
#include "../../support/utils.h"
#include "rpc_local_session.h"

namespace tvm {
namespace runtime {

/*!
 * Event-driven state-machine based handlers for RPCEndpoint.
 *
 * Key functions:
 *
 * - SendPackedSeq: send the arguments over to the peer
 * - HandleNextEvent: handle the next request from the peer(RPCCode followed by per code protocol).
 */
class RPCEndpoint::EventHandler : public dmlc::Stream {
 public:
  EventHandler(support::RingBuffer* reader, support::RingBuffer* writer, std::string name,
               std::string* remote_key, std::function<void()> flush_writer)
      : reader_(reader),
        writer_(writer),
        name_(name),
        remote_key_(remote_key),
        flush_writer_(flush_writer) {
    this->Clear();

    if (*remote_key == "%toinit") {
      state_ = kInitHeader;
      remote_key_->resize(0);
      pending_request_bytes_ = sizeof(int32_t);
    }
  }

  /*!
   * \brief Bytes needed to fulfill current request
   */
  size_t BytesNeeded() const {
    if (reader_->bytes_available() < pending_request_bytes_) {
      return pending_request_bytes_ - reader_->bytes_available();
    } else {
      return 0;
    }
  }

  /*!
   * \brief Request number of bytes from the reader.
   * \param nbytes The number of bytes
   */
  void RequestBytes(size_t nbytes) {
    pending_request_bytes_ += nbytes;
    reader_->Reserve(pending_request_bytes_);
  }

  /*! \return Whether we are ready to handle next request. */
  bool Ready() const { return reader_->bytes_available() >= pending_request_bytes_; }

  /*! \return Whether we can perform a clean shutdown */
  bool CanCleanShutdown() const { return state_ == kRecvPacketNumBytes; }

  /*! \brief Finish the copy ack stage. */
  void FinishCopyAck() { this->SwitchToState(kRecvPacketNumBytes); }

  /*!
   * \brief Enter the io loop until the next event.
   * \param client_mode Whether we are in the client.
   * \param async_server_mode Whether we are in the async server mode.
   * \param setreturn The function to set the return value encoding.
   * \return The function to set return values when there is a return event.
   */
  RPCCode HandleNextEvent(bool client_mode, bool async_server_mode,
                          RPCSession::FEncodeReturn setreturn) {
    std::swap(client_mode_, client_mode);
    std::swap(async_server_mode_, async_server_mode);

    RPCCode status = RPCCode::kNone;

    while (status == RPCCode::kNone && state_ != kWaitForAsyncCallback && this->Ready()) {
      switch (state_) {
        case kInitHeader:
          HandleInitHeader();
          break;
        case kRecvPacketNumBytes: {
          uint64_t packet_nbytes;
          ICHECK(this->Read(&packet_nbytes));
          if (packet_nbytes != 0) {
            this->SwitchToState(kProcessPacket);
            this->RequestBytes(packet_nbytes);
          } else {
            this->SwitchToState(kRecvPacketNumBytes);
          }
          break;
        }
        case kProcessPacket: {
          this->HandleProcessPacket(setreturn);
          break;
        }
        case kWaitForAsyncCallback: {
          break;
        }
        case kReturnReceived: {
          this->SwitchToState(kRecvPacketNumBytes);
          status = RPCCode::kReturn;
          break;
        }
        case kCopyAckReceived: {
          status = RPCCode::kCopyAck;
          break;
        }
        case kShutdownReceived: {
          status = RPCCode::kShutdown;
        }
      }
    }

    std::swap(async_server_mode_, async_server_mode);
    std::swap(client_mode_, client_mode);
    return status;
  }

  /*! \brief Clear all the states in the Handler.*/
  void Clear() {
    state_ = kRecvPacketNumBytes;
    pending_request_bytes_ = sizeof(uint64_t);
  }

  /*!
   * \brief Validate that the arguments can be sent through RPC.
   * \param args The arguments.
   */
  void ValidateArguments(ffi::PackedArgs args) {
    for (int i = 0; i < args.size(); ++i) {
      if (args[i] == nullptr) continue;
      if (args[i].type_index() == ffi::TypeIndex::kTVMFFIModule) continue;
      if (const Object* obj = args[i].as<Object>()) {
        if (!obj->IsInstance<RPCObjectRefObj>()) {
          LOG(FATAL) << "ValueError: Cannot pass argument " << i << ", type " << obj->GetTypeKey()
                     << " (type_index = " << obj->type_index() << ")";
        }
      } else if (auto opt_device = args[i].as<DLDevice>()) {
        DLDevice dev = opt_device.value();
        ICHECK(!IsRPCSessionDevice(dev)) << "InternalError: cannot pass RPC device in the channel";
      }
    }
  }

  void ThrowError(RPCServerStatus code, RPCCode info = RPCCode::kNone) {
    LOG(FATAL) << "RPCServerError:" << RPCServerStatusToString(code);
  }

  uint64_t PackedSeqGetNumBytes(const ffi::AnyView* packed_args, int num_args, bool client_mode) {
    return RPCReference::PackedSeqGetNumBytes(reinterpret_cast<const TVMFFIAny*>(packed_args),
                                              num_args, client_mode, this);
  }

  void SendPackedSeq(const ffi::AnyView* packed_args, int num_args, bool client_mode) {
    RPCReference::SendPackedSeq(reinterpret_cast<const TVMFFIAny*>(packed_args), num_args,
                                client_mode, this);
  }

  // Endian aware IO handling
  using Stream::Read;
  using Stream::ReadArray;
  using Stream::Write;
  using Stream::WriteArray;

  void MessageStart(uint64_t packet_nbytes) {
    // Unused here, implemented for microTVM framing layer.
  }

  bool Read(RPCCode* code) {
    int32_t cdata;
    if (!this->Read(&cdata)) return false;
    *code = static_cast<RPCCode>(cdata);
    return true;
  }
  void Write(RPCCode code) {
    int32_t cdata = static_cast<int>(code);
    this->Write(cdata);
  }

  void WriteFFIAny(const TVMFFIAny* in) {
    // NOTE: for now all remote object are encoded as RPCObjectRef
    // follow the same disco protocol in case we would like to upgrade later
    //
    // Rationale note: Only handle remote object allows the same mechanism to work for minRPC
    // which is needed for wasm and other env that goes through C API
    const AnyView* any_view_ptr = reinterpret_cast<const AnyView*>(in);
    if (const auto* ref = any_view_ptr->as<RPCObjectRefObj>()) {
      this->template Write<uint32_t>(runtime::TypeIndex::kRuntimeRPCObjectRef);
      uint64_t handle = reinterpret_cast<uint64_t>(ref->object_handle());
      this->template Write<int64_t>(handle);
    } else {
      LOG(FATAL) << "ValueError: Object type is not supported in RPC calling convention: "
                 << any_view_ptr->GetTypeKey() << " (type_index = " << any_view_ptr->type_index()
                 << ")";
    }
  }
  uint64_t GetFFIAnyProtocolBytes(const TVMFFIAny* in) {
    const AnyView* any_view_ptr = reinterpret_cast<const AnyView*>(in);
    if (any_view_ptr->as<RPCObjectRefObj>()) {
      return sizeof(uint32_t) + sizeof(int64_t);
    } else {
      LOG(FATAL) << "ValueError: Object type is not supported in RPC calling convention: "
                 << any_view_ptr->GetTypeKey() << " (type_index = " << any_view_ptr->type_index()
                 << ")";
      TVM_FFI_UNREACHABLE();
    }
  }

  void ReadFFIAny(TVMFFIAny* out) {
    // NOTE: for now all remote object are encoded as RPCObjectRef
    // follow the same disco protocol in case we would like to upgrade later
    //
    // Rationale note: Only handle remote object allows the same mechanism to work for minRPC
    // which is needed for wasm and other env that goes through C API
    uint32_t type_index;
    this->template Read<uint32_t>(&type_index);
    if (type_index == runtime::TypeIndex::kRuntimeRPCObjectRef) {
      uint64_t handle;
      this->template Read<uint64_t>(&handle);
      // Always wrap things back in RPCObjectRef
      // this is because we want to enable multi-hop RPC
      // and next hop would also need to check the object index
      RPCObjectRef rpc_obj(make_object<RPCObjectRefObj>(reinterpret_cast<void*>(handle), nullptr));
      // Legacy ABI translation
      // TODO(tqchen): remove this once we have upgraded to new ABI
      *reinterpret_cast<AnyView*>(out) = rpc_obj;
      object_arena_.push_back(rpc_obj);
    } else {
      LOG(FATAL) << "ValueError: Object type is not supported in Disco calling convention: "
                 << Object::TypeIndex2Key(type_index) << " (type_index = " << type_index << ")";
    }
  }

  void MessageDone() {
    // Unused here, implemented for microTVM framing layer.
  }

  template <typename T>
  T* ArenaAlloc(int count) {
    static_assert(std::is_pod<T>::value, "need to be trival");
    return arena_.template allocate_<T>(count);
  }

  /*! \brief Recycle all the memory used in the arena */
  void RecycleAll() {
    this->object_arena_.clear();
    this->arena_.RecycleAll();
  }

 protected:
  enum State {
    kInitHeader,
    kRecvPacketNumBytes,
    kProcessPacket,
    kWaitForAsyncCallback,
    kReturnReceived,
    kCopyAckReceived,
    kShutdownReceived
  };
  // Current state;
  State state_;
  // Initialize remote header
  int init_header_step_{0};
  // Whether current handler is client or server mode.
  bool client_mode_{false};
  // Whether current handler is in the async server mode.
  bool async_server_mode_{false};
  // Internal arena
  support::Arena arena_;
  // internal arena for temp objects
  std::vector<ObjectRef> object_arena_;

  // State switcher
  void SwitchToState(State state) {
    // invariant
    if (state != kCopyAckReceived) {
      ICHECK_EQ(pending_request_bytes_, 0U) << "state=" << state;
    }
    // need to actively flush the writer
    // so the data get pushed out.
    if (state_ == kWaitForAsyncCallback) {
      flush_writer_();
    }
    state_ = state;
    ICHECK(state != kInitHeader) << "cannot switch to init header";
    if (state == kRecvPacketNumBytes) {
      this->RequestBytes(sizeof(uint64_t));
      // recycle arena for the next session.
      this->RecycleAll();
    }
  }

  // handler for initial header read
  void HandleInitHeader() {
    if (init_header_step_ == 0) {
      int32_t len;
      this->Read(&len);
      remote_key_->resize(len);
      init_header_step_ = 1;
      this->RequestBytes(len);
      return;
    } else {
      ICHECK_EQ(init_header_step_, 1);
      this->ReadArray(remote_key_->data(), remote_key_->length());
      this->SwitchToState(kRecvPacketNumBytes);
    }
  }

  // Handler for read code.
  void HandleProcessPacket(RPCSession::FEncodeReturn setreturn) {
    RPCCode code = RPCCode::kNone;
    this->Read(&code);
    if (code >= RPCCode::kSyscallCodeStart) {
      this->HandleSyscall(code);
    } else {
      switch (code) {
        case RPCCode::kInitServer: {
          this->HandleInitServer();
          break;
        }
        case RPCCode::kCallFunc: {
          this->HandleNormalCallFunc();
          break;
        }
        case RPCCode::kCopyFromRemote: {
          this->HandleCopyFromRemote();
          break;
        }
        case RPCCode::kCopyToRemote: {
          this->HandleCopyToRemote();
          break;
        }
        case RPCCode::kException:
        case RPCCode::kReturn: {
          this->HandleReturn(code, setreturn);
          break;
        }
        case RPCCode::kCopyAck: {
          this->SwitchToState(kCopyAckReceived);
          break;
        }
        case RPCCode::kShutdown: {
          this->SwitchToState(kShutdownReceived);
          break;
        }
        default:
          LOG(FATAL) << "Unknown event " << static_cast<int>(code);
      }
    }
  }

  /*!
   * \brief Receive incoming packed seq from the stream.
   * \return The received argments.
   * \note The ffi::PackedArgs is available until we switchstate.
   */
  ffi::PackedArgs RecvPackedSeq() {
    ffi::AnyView* packed_args;
    int num_args;
    RPCReference::RecvPackedSeq(reinterpret_cast<TVMFFIAny**>(&packed_args), &num_args, this);
    return ffi::PackedArgs(packed_args, num_args);
  }

  /*!
   * \brief Return exception to the remote.
   * \param err_msg The error message.
   */
  void ReturnException(const char* err_msg) { RPCReference::ReturnException(err_msg, this); }

  /*!
   * \brief Return nullptr to the remote.
   * \param err_msg The error message.
   */
  void ReturnVoid() { RPCReference::ReturnVoid(this); }

  /*!
   * \brief Return a packed sequence to the remote.
   * \param args The arguments.
   */
  void ReturnPackedSeq(ffi::PackedArgs args) {
    RPCReference::ReturnPackedSeq(reinterpret_cast<const TVMFFIAny*>(args.data()), args.size(),
                                  this);
  }

  /*!
   * \brief Handle the case when return/exception value is received.
   * \param code The RPC code.
   * \param setreturn The function to encode return.
   */
  void HandleReturn(RPCCode code, RPCSession::FEncodeReturn setreturn) {
    ffi::PackedArgs args = RecvPackedSeq();
    if (code == RPCCode::kException) {
      // switch to the state before sending exception.
      this->SwitchToState(kRecvPacketNumBytes);
      String msg = args[0].cast<String>();
      if (!support::StartsWith(msg, "RPCSessionTimeoutError: ")) {
        msg = "RPCError: Error caught from RPC call:\n" + msg;
      }
      LOG(FATAL) << msg;
    }

    ICHECK(setreturn != nullptr) << "fsetreturn not available";
    setreturn(args);

    this->SwitchToState(kReturnReceived);
  }

  void HandleSyscall(RPCCode code);

  void HandleCopyFromRemote() {
    DLTensor* arr = RPCReference::ReceiveDLTensor(this);
    uint64_t data_bytes;
    this->Read(&data_bytes);
    size_t elem_bytes = (arr->dtype.bits * arr->dtype.lanes + 7) / 8;
    auto* sess = GetServingSession();
    // Return Copy Ack with the given data
    auto fcopyack = [this](char* dptr, size_t num_bytes) {
      RPCCode code = RPCCode::kCopyAck;
      uint64_t packet_nbytes = sizeof(code) + num_bytes;

      this->Write(packet_nbytes);
      this->Write(code);
      this->WriteArray(dptr, num_bytes);
      this->SwitchToState(kRecvPacketNumBytes);
    };

    // When session is local, we can directly treat handle
    // as the cpu pointer without allocating a temp space.
    if (arr->device.device_type == kDLCPU && sess->IsLocalSession() && DMLC_IO_NO_ENDIAN_SWAP) {
      char* data_ptr = reinterpret_cast<char*>(arr->data) + arr->byte_offset;
      fcopyack(data_ptr, data_bytes);
    } else {
      char* temp_data = this->ArenaAlloc<char>(data_bytes);
      auto on_copy_complete = [this, elem_bytes, data_bytes, temp_data, fcopyack](
                                  RPCCode status, ffi::PackedArgs args) {
        if (status == RPCCode::kException) {
          this->ReturnException(args[0].cast<const char*>());
          this->SwitchToState(kRecvPacketNumBytes);
        } else {
          // endian aware handling
          if (!DMLC_IO_NO_ENDIAN_SWAP) {
            dmlc::ByteSwap(temp_data, elem_bytes, data_bytes / elem_bytes);
          }
          fcopyack(temp_data, data_bytes);
        }
      };

      this->SwitchToState(kWaitForAsyncCallback);
      sess->AsyncCopyFromRemote(arr, static_cast<void*>(temp_data), data_bytes, on_copy_complete);
    }
  }

  void HandleCopyToRemote() {
    DLTensor* arr = RPCReference::ReceiveDLTensor(this);
    uint64_t data_bytes;
    this->Read(&data_bytes);
    size_t elem_bytes = (arr->dtype.bits * arr->dtype.lanes + 7) / 8;
    auto* sess = GetServingSession();

    // When session is local, we can directly treat handle
    // as the cpu pointer without allocating a temp space.
    if (arr->device.device_type == kDLCPU && sess->IsLocalSession()) {
      char* dptr = reinterpret_cast<char*>(arr->data) + arr->byte_offset;
      this->ReadArray(dptr, data_bytes);

      if (!DMLC_IO_NO_ENDIAN_SWAP) {
        dmlc::ByteSwap(dptr, elem_bytes, data_bytes / elem_bytes);
      }
      this->ReturnVoid();
      this->SwitchToState(kRecvPacketNumBytes);
    } else {
      char* temp_data = this->ArenaAlloc<char>(data_bytes);
      this->ReadArray(temp_data, data_bytes);

      if (!DMLC_IO_NO_ENDIAN_SWAP) {
        dmlc::ByteSwap(temp_data, elem_bytes, data_bytes / elem_bytes);
      }

      auto on_copy_complete = [this](RPCCode status, ffi::PackedArgs args) {
        if (status == RPCCode::kException) {
          this->ReturnException(args[0].cast<const char*>());
          this->SwitchToState(kRecvPacketNumBytes);
        } else {
          this->ReturnVoid();
          this->SwitchToState(kRecvPacketNumBytes);
        }
      };

      this->SwitchToState(kWaitForAsyncCallback);
      sess->AsyncCopyToRemote(static_cast<void*>(temp_data), arr, data_bytes, on_copy_complete);
    }
  }

  // Handle for packed call.
  void HandleNormalCallFunc() {
    uint64_t call_handle;

    this->Read(&call_handle);
    ffi::PackedArgs args = RecvPackedSeq();

    this->SwitchToState(kWaitForAsyncCallback);
    GetServingSession()->AsyncCallFunc(reinterpret_cast<void*>(call_handle), args,
                                       [this](RPCCode status, ffi::PackedArgs args) {
                                         if (status == RPCCode::kException) {
                                           this->ReturnException(args[0].cast<const char*>());
                                         } else {
                                           ValidateArguments(args);
                                           this->ReturnPackedSeq(args);
                                         }
                                         this->SwitchToState(kRecvPacketNumBytes);
                                       });
  }

  void HandleInitServer() {
    std::string client_protocol_ver;

    uint64_t len;
    this->Read(&len);
    client_protocol_ver.resize(len);
    this->Read(dmlc::BeginPtr(client_protocol_ver), len);

    ffi::PackedArgs args = RecvPackedSeq();

    try {
      ICHECK(serving_session_ == nullptr) << "Server has already been initialized";

      std::string server_protocol_ver = kRPCProtocolVer;
      ICHECK_EQ(client_protocol_ver, server_protocol_ver)
          << "Server[" << name_ << "]: Client protocol version mismatch with the server "
          << " server protocol=" << server_protocol_ver
          << ", client protocol=" << client_protocol_ver;

      std::string constructor_name;
      ffi::PackedArgs constructor_args = ffi::PackedArgs(nullptr, 0);

      if (args.size() == 0) {
        constructor_name = "rpc.LocalSession";
        serving_session_ = std::make_shared<LocalSession>();
      } else {
        constructor_name = args[0].cast<std::string>();
        constructor_args = args.Slice(1);
      }

      auto fconstructor = tvm::ffi::Function::GetGlobal(constructor_name);
      ICHECK(fconstructor.has_value()) << " Cannot find session constructor " << constructor_name;
      ffi::Any con_ret;

      try {
        fconstructor->CallPacked(constructor_args, &con_ret);
      } catch (const Error& e) {
        LOG(FATAL) << "Server[" << name_ << "]:"
                   << " Error caught from session constructor " << constructor_name << ":\n"
                   << e.what();
      }
      auto opt_con_ret = con_ret.as<runtime::Module>();
      // Legacy ABI translation
      ICHECK(opt_con_ret.has_value())
          << "Server[" << name_ << "]:"
          << " Constructor " << constructor_name << " need to return an RPCModule";
      Module mod = opt_con_ret.value();
      std::string tkey = mod->type_key();
      ICHECK_EQ(tkey, "rpc") << "Constructor " << constructor_name << " to return an RPCModule";
      serving_session_ = RPCModuleGetSession(mod);
      this->ReturnVoid();
    } catch (const std::exception& e) {
      this->ReturnException(e.what());
    }

    this->SwitchToState(kRecvPacketNumBytes);
  }

  void HandleSyscallStreamSync() {
    ffi::PackedArgs args = RecvPackedSeq();
    try {
      auto dev = args[0].cast<Device>();
      TVMStreamHandle handle = args[1].cast<void*>();

      this->SwitchToState(kWaitForAsyncCallback);
      GetServingSession()->AsyncStreamWait(dev, handle,
                                           [this](RPCCode status, ffi::PackedArgs args) {
                                             if (status == RPCCode::kException) {
                                               this->ReturnException(args[0].cast<const char*>());
                                             } else {
                                               this->ReturnVoid();
                                             }
                                             this->SwitchToState(kRecvPacketNumBytes);
                                           });
    } catch (const std::exception& e) {
      this->ReturnException(e.what());
      this->SwitchToState(kRecvPacketNumBytes);
    }
  }

  // Handler for special syscalls that have a specific RPCCode.
  template <typename F>
  void SysCallHandler(F f) {
    ffi::PackedArgs args = RecvPackedSeq();
    try {
      ffi::Any rv;
      f(GetServingSession(), args, &rv);
      AnyView packed_args[1];
      packed_args[0] = rv;
      this->ReturnPackedSeq(ffi::PackedArgs(packed_args, 1));
    } catch (const std::exception& e) {
      this->ReturnException(e.what());
    }
    this->SwitchToState(kRecvPacketNumBytes);
  }

 private:
  RPCSession* GetServingSession() const {
    ICHECK(serving_session_ != nullptr)
        << "Need to call InitRemoteSession first before any further actions";
    ICHECK(!serving_session_->IsAsync() || async_server_mode_)
        << "Cannot host an async session in a non-Event driven server";

    return serving_session_.get();
  }
  // Utility functions
  // Internal read function, update pending_request_bytes_
  size_t Read(void* data, size_t size) final {
    ICHECK_LE(size, pending_request_bytes_);
    reader_->Read(data, size);
    pending_request_bytes_ -= size;
    return size;
  }
  // write the data to the channel.
  size_t Write(const void* data, size_t size) final {
    writer_->Write(data, size);
    return size;
  }

  // Number of pending bytes requests
  size_t pending_request_bytes_{0};
  // The ring buffer to read data from.
  support::RingBuffer* reader_;
  // The ringr buffer to write reply to.
  support::RingBuffer* writer_;
  // The session used to serve the RPC requests.
  std::shared_ptr<RPCSession> serving_session_;
  // Name of endpoint.
  std::string name_;
  // remote key
  std::string* remote_key_;
  // function to flush the writer.
  std::function<void()> flush_writer_;
};

RPCCode RPCEndpoint::HandleUntilReturnEvent(bool client_mode, RPCSession::FEncodeReturn setreturn) {
  RPCCode code = RPCCode::kCallFunc;

  CHECK(channel_) << "Expected connection to server " << name_
                  << " to be active, but the connection was previously closed";
  while (code != RPCCode::kReturn && code != RPCCode::kShutdown && code != RPCCode::kCopyAck) {
    while (writer_.bytes_available() != 0) {
      writer_.ReadWithCallback(
          [this](const void* data, size_t size) { return channel_->Send(data, size); },
          writer_.bytes_available());
    }
    size_t bytes_needed = handler_->BytesNeeded();
    if (bytes_needed != 0) {
      size_t n = reader_.WriteWithCallback(
          [this](void* data, size_t size) { return channel_->Recv(data, size); }, bytes_needed);
      if (n == 0) {
        if (handler_->CanCleanShutdown()) {
          return RPCCode::kShutdown;
        } else {
          LOG(FATAL) << "Channel closes before we get needed bytes";
        }
      }
    }
    code = handler_->HandleNextEvent(client_mode, false, setreturn);
  }
  return code;
}

void RPCEndpoint::Init() {
  // callback to flush the writer.
  auto flush_writer = [this]() {
    while (writer_.bytes_available() != 0) {
      size_t n = writer_.ReadWithCallback(
          [this](const void* data, size_t size) { return channel_->Send(data, size); },
          writer_.bytes_available());
      if (n == 0) break;
    }
  };

  // Event handler
  handler_ = std::make_shared<EventHandler>(&reader_, &writer_, name_, &remote_key_, flush_writer);

  // Quick function to for syscall remote.
  syscall_remote_ = ffi::Function([this](ffi::PackedArgs all_args, ffi::Any* rv) {
    std::lock_guard<std::mutex> lock(mutex_);
    RPCCode code = static_cast<RPCCode>(all_args[0].cast<int>());
    ffi::PackedArgs args = all_args.Slice(1);

    // run transmission
    uint64_t packet_nbytes =
        sizeof(code) + handler_->PackedSeqGetNumBytes(args.data(), args.size(), true);

    // All packet begins with packet nbytes
    handler_->Write(packet_nbytes);
    handler_->Write(code);
    handler_->SendPackedSeq(args.data(), args.size(), true);

    code = HandleUntilReturnEvent(true, [rv](ffi::PackedArgs args) {
      ICHECK_EQ(args.size(), 1);
      *rv = args[0];
    });
    ICHECK(code == RPCCode::kReturn) << "code=" << static_cast<int>(code);
  });
}

/*!
 * \brief Create a new RPCEndpoint instance.
 * \param channel RPCChannel used to communicate.
 * \param name Name of this session, used to identify log messages from this RPCEndpoint instance.
 * \param remote_key The remote key reported during protocol initialization, or "%toinit" if the
 * RPCEndpoint should handle this phase of the protocol for you. Some servers may prefer to access
 * parts of the key to modify their behavior.
 * \param fcleanup The cleanup Packed function.
 */
std::shared_ptr<RPCEndpoint> RPCEndpoint::Create(std::unique_ptr<RPCChannel> channel,
                                                 std::string name, std::string remote_key,
                                                 ffi::TypedFunction<void()> fcleanup) {
  std::shared_ptr<RPCEndpoint> endpt = std::make_shared<RPCEndpoint>();
  endpt->channel_ = std::move(channel);
  endpt->name_ = std::move(name);
  endpt->remote_key_ = std::move(remote_key);
  endpt->fcleanup_ = fcleanup;
  endpt->Init();
  return endpt;
}

RPCEndpoint::~RPCEndpoint() { this->Shutdown(); }

void RPCEndpoint::Shutdown() {
  if (channel_ != nullptr) {
    RPCCode code = RPCCode::kShutdown;
    uint64_t packet_nbytes = sizeof(code);

    handler_->Write(packet_nbytes);
    handler_->Write(code);

    // flush all writing buffer to output channel.
    try {
      while (writer_.bytes_available() != 0) {
        size_t n = writer_.ReadWithCallback(
            [this](const void* data, size_t size) { return channel_->Send(data, size); },
            writer_.bytes_available());
        if (n == 0) break;
      }
    } catch (const Error& e) {
    }
    channel_.reset(nullptr);
  }
}

void RPCEndpoint::ServerLoop() {
  if (const auto f = tvm::ffi::Function::GetGlobal("tvm.rpc.server.start")) {
    (*f)();
  }
  ffi::Any rv;
  ICHECK(HandleUntilReturnEvent(false, [](ffi::PackedArgs) {}) == RPCCode::kShutdown);
  if (const auto f = tvm::ffi::Function::GetGlobal("tvm.rpc.server.shutdown")) {
    (*f)();
  }
  channel_.reset(nullptr);
  if (fcleanup_ != nullptr) fcleanup_();
}

int RPCEndpoint::ServerAsyncIOEventHandler(const std::string& in_bytes, int event_flag) {
  RPCCode code = RPCCode::kNone;
  if (in_bytes.length() != 0) {
    reader_.Write(in_bytes.c_str(), in_bytes.length());
    code = handler_->HandleNextEvent(false, true, [](ffi::PackedArgs) {});
  }
  if ((event_flag & 2) != 0 && writer_.bytes_available() != 0) {
    writer_.ReadWithCallback(
        [this](const void* data, size_t size) { return channel_->Send(data, size); },
        writer_.bytes_available());
  }
  ICHECK(code != RPCCode::kReturn && code != RPCCode::kCopyAck);
  // if the code is kShutdown, return 0 to indicate the server should exit
  if (code == RPCCode::kShutdown) return 0;
  // if the writer has bytes available, return 2 to indicate the server should send data
  // usually by calling the handler again
  if (writer_.bytes_available() != 0) return 2;
  // otherwise, return 1 to indicate the server should and read
  return 1;
}

void RPCEndpoint::InitRemoteSession(ffi::PackedArgs args) {
  std::lock_guard<std::mutex> lock(mutex_);
  RPCCode code = RPCCode::kInitServer;
  std::string protocol_ver = kRPCProtocolVer;
  uint64_t length = protocol_ver.length();

  // run transmission
  uint64_t packet_nbytes = sizeof(code) + sizeof(length) + length +
                           handler_->PackedSeqGetNumBytes(args.data(), args.size(), true);

  // All packet begins with packet nbytes
  handler_->Write(packet_nbytes);
  handler_->Write(code);
  handler_->Write(length);
  handler_->WriteArray(protocol_ver.data(), length);
  handler_->SendPackedSeq(args.data(), args.size(), true);

  code = HandleUntilReturnEvent(true, [](ffi::PackedArgs args) {});
  ICHECK(code == RPCCode::kReturn) << "code=" << static_cast<int>(code);
}

// Get remote function with name
void RPCEndpoint::CallFunc(RPCSession::PackedFuncHandle h, ffi::PackedArgs args,
                           RPCSession::FEncodeReturn encode_return) {
  std::lock_guard<std::mutex> lock(mutex_);

  handler_->ValidateArguments(args);
  RPCCode code = RPCCode::kCallFunc;
  uint64_t handle = reinterpret_cast<uint64_t>(h);

  // run transmission
  uint64_t packet_nbytes = sizeof(code) + sizeof(handle) +
                           handler_->PackedSeqGetNumBytes(args.data(), args.size(), true);

  handler_->Write(packet_nbytes);
  handler_->Write(code);
  handler_->Write(handle);
  handler_->SendPackedSeq(args.data(), args.size(), true);

  code = HandleUntilReturnEvent(true, encode_return);
  ICHECK(code == RPCCode::kReturn) << "code=" << RPCCodeToString(code);
}

void RPCEndpoint::CopyToRemote(void* from_bytes, DLTensor* to, uint64_t nbytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  RPCCode code = RPCCode::kCopyToRemote;

  uint64_t tensor_total_size_bytes = static_cast<uint64_t>(GetDataSize(*to));
  ICHECK_LE(to->byte_offset + nbytes, tensor_total_size_bytes)
      << "CopyToRemote: overflow in tensor size: (byte_offset=" << to->byte_offset
      << ", nbytes=" << nbytes << ", tensor_total_size=" << tensor_total_size_bytes << ")";

  uint64_t overhead = RemoteCopyCalculatePacketOverheadSize(to, code, nbytes);
  uint64_t packet_nbytes = overhead + nbytes;

  handler_->Write(packet_nbytes);
  handler_->Write(code);
  RPCReference::SendDLTensor(handler_, to);
  handler_->Write(nbytes);
  handler_->WriteArray(reinterpret_cast<char*>(from_bytes), nbytes);
  ICHECK(HandleUntilReturnEvent(true, [](ffi::PackedArgs) {}) == RPCCode::kReturn);
}

void RPCEndpoint::CopyFromRemote(DLTensor* from, void* to_bytes, uint64_t nbytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  RPCCode code = RPCCode::kCopyFromRemote;

  uint64_t tensor_total_size_bytes = static_cast<uint64_t>(GetDataSize(*from));
  ICHECK_LE(from->byte_offset + nbytes, tensor_total_size_bytes)
      << "CopyFromRemote: overflow in tensor size: (byte_offset=" << from->byte_offset
      << ", nbytes=" << nbytes << ", tensor_total_size=" << tensor_total_size_bytes << ")";

  uint64_t overhead = RemoteCopyCalculatePacketOverheadSize(from, code, nbytes);
  uint64_t packet_nbytes = overhead;

  handler_->Write(packet_nbytes);
  handler_->Write(code);
  RPCReference::SendDLTensor(handler_, from);
  handler_->Write(nbytes);
  ICHECK(HandleUntilReturnEvent(true, [](ffi::PackedArgs) {}) == RPCCode::kCopyAck);

  handler_->ReadArray(reinterpret_cast<char*>(to_bytes), nbytes);
  handler_->FinishCopyAck();
}

// SysCallEventHandler functions
void RPCGetGlobalFunc(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto name = args[0].cast<std::string>();
  *rv = handler->GetFunction(name);
}

void RPCFreeHandle(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  void* handle = args[0].cast<void*>();
  handler->FreeHandle(handle);
}

void RPCDevSetDevice(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  handler->GetDeviceAPI(dev)->SetDevice(dev);
}

void RPCDevGetAttr(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  DeviceAttrKind kind = static_cast<DeviceAttrKind>(args[1].cast<int>());
  if (kind == kExist) {
    DeviceAPI* api = handler->GetDeviceAPI(dev, true);
    if (api != nullptr) {
      api->GetAttr(dev, kind, rv);
    } else {
      *rv = 0;
    }
  } else {
    handler->GetDeviceAPI(dev)->GetAttr(dev, static_cast<DeviceAttrKind>(kind), rv);
  }
}

void RPCDevAllocData(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  uint64_t nbytes = args[1].cast<uint64_t>();
  uint64_t alignment = args[2].cast<uint64_t>();
  DLDataType type_hint = args[3].cast<DLDataType>();
  void* data = handler->GetDeviceAPI(dev)->AllocDataSpace(dev, nbytes, alignment, type_hint);
  *rv = data;
}

void RPCDevAllocDataWithScope(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto arr = args[0].cast<DLTensor*>();
  Device dev = arr->device;
  int ndim = arr->ndim;
  int64_t* shape = arr->shape;
  DLDataType dtype = arr->dtype;
  auto mem_scope = args[1].cast<Optional<String>>();
  void* data = handler->GetDeviceAPI(dev)->AllocDataSpace(dev, ndim, shape, dtype, mem_scope);
  *rv = data;
}

void RPCDevFreeData(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  void* ptr = args[1].cast<void*>();
  handler->GetDeviceAPI(dev)->FreeDataSpace(dev, ptr);
}

void RPCCopyAmongRemote(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto from = args[0].cast<DLTensor*>();
  auto to = args[1].cast<DLTensor*>();
  TVMStreamHandle stream = args[2].cast<void*>();

  Device dev = from->device;
  if (dev.device_type == kDLCPU) {
    dev = to->device;
  } else {
    ICHECK(to->device.device_type == kDLCPU || to->device.device_type == from->device.device_type)
        << "Can not copy across different dev types directly";
  }
  handler->GetDeviceAPI(dev)->CopyDataFromTo(from, to, stream);
}

void RPCDevCreateStream(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  void* data = handler->GetDeviceAPI(dev)->CreateStream(dev);
  *rv = data;
}

void RPCDevFreeStream(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  TVMStreamHandle stream = args[1].cast<void*>();
  handler->GetDeviceAPI(dev)->FreeStream(dev, stream);
}

void RPCDevSetStream(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  TVMStreamHandle stream = args[1].cast<void*>();
  handler->GetDeviceAPI(dev)->SetStream(dev, stream);
}

void RPCDevGetCurrentStream(RPCSession* handler, ffi::PackedArgs args, ffi::Any* rv) {
  auto dev = args[0].cast<Device>();
  *rv = handler->GetDeviceAPI(dev)->GetCurrentStream(dev);
}

void RPCEndpoint::EventHandler::HandleSyscall(RPCCode code) {
  // Event handler sit at clean state at this point.
  switch (code) {
    // system functions
    case RPCCode::kFreeHandle:
      SysCallHandler(RPCFreeHandle);
      break;
    case RPCCode::kGetGlobalFunc:
      SysCallHandler(RPCGetGlobalFunc);
      break;
    case RPCCode::kDevSetDevice:
      SysCallHandler(RPCDevSetDevice);
      break;
    case RPCCode::kDevGetAttr:
      SysCallHandler(RPCDevGetAttr);
      break;
    case RPCCode::kDevAllocData:
      SysCallHandler(RPCDevAllocData);
      break;
    case RPCCode::kDevAllocDataWithScope:
      SysCallHandler(RPCDevAllocDataWithScope);
      break;
    case RPCCode::kDevFreeData:
      SysCallHandler(RPCDevFreeData);
      break;
    case RPCCode::kDevCreateStream:
      SysCallHandler(RPCDevCreateStream);
      break;
    case RPCCode::kDevFreeStream:
      SysCallHandler(RPCDevFreeStream);
      break;
    case RPCCode::kDevStreamSync:
      this->HandleSyscallStreamSync();
      break;
    case RPCCode::kDevSetStream:
      SysCallHandler(RPCDevSetStream);
      break;
    case RPCCode::kDevGetCurrentStream:
      SysCallHandler(RPCDevGetCurrentStream);
      break;
    case RPCCode::kCopyAmongRemote:
      SysCallHandler(RPCCopyAmongRemote);
      break;
    default:
      LOG(FATAL) << "Unknown event " << static_cast<int>(code);
  }

  if (state_ != kWaitForAsyncCallback) {
    ICHECK_EQ(state_, kRecvPacketNumBytes);
  }
}

/*!
 * \brief RPC client session that proxies all calls to an endpoint.
 */
class RPCClientSession : public RPCSession, public DeviceAPI {
 public:
  /*!
   * \brief param endpoint The client endpoint of the session.
   */
  explicit RPCClientSession(std::shared_ptr<RPCEndpoint> endpoint) : endpoint_(endpoint) {}

  // function overrides
  PackedFuncHandle GetFunction(const std::string& name) final {
    return endpoint_->SysCallRemote(RPCCode::kGetGlobalFunc, name).cast<void*>();
  }

  void CallFunc(PackedFuncHandle func, ffi::PackedArgs args,
                const FEncodeReturn& fencode_return) final {
    endpoint_->CallFunc(func, args, fencode_return);
  }

  void CopyToRemote(void* local_from_bytes, DLTensor* remote_to, uint64_t nbytes) final {
    RPCCode code = RPCCode::kCopyToRemote;
    uint64_t overhead = RemoteCopyCalculatePacketOverheadSize(remote_to, code, nbytes);
    uint64_t rpc_max_size = GetRPCMaxTransferSize();
    ICHECK_GT(rpc_max_size, overhead) << "CopyToRemote: Invalid block size!";
    const uint64_t block_size = rpc_max_size - overhead;
    uint64_t block_count = 0;
    const uint64_t num_blocks = nbytes / block_size;
    void* from_bytes;

    for (block_count = 0; block_count < num_blocks; block_count++) {
      remote_to->byte_offset = block_count * block_size;
      from_bytes = reinterpret_cast<void*>(
          (reinterpret_cast<uint8_t*>(local_from_bytes) + block_count * block_size));
      endpoint_->CopyToRemote(from_bytes, remote_to, block_size);
    }

    const uint64_t remainder_bytes = nbytes % block_size;
    if (remainder_bytes != 0) {
      remote_to->byte_offset = block_count * block_size;
      from_bytes = reinterpret_cast<void*>(
          (reinterpret_cast<uint8_t*>(local_from_bytes) + block_count * block_size));
      endpoint_->CopyToRemote(from_bytes, remote_to, remainder_bytes);
    }
  }

  void CopyFromRemote(DLTensor* remote_from, void* local_to_bytes, uint64_t nbytes) final {
    RPCCode code = RPCCode::kCopyFromRemote;
    uint64_t overhead = RemoteCopyCalculatePacketOverheadSize(remote_from, code, nbytes);
    uint64_t rpc_max_size = GetRPCMaxTransferSize();
    ICHECK_GT(rpc_max_size, overhead) << "CopyFromRemote: Invalid block size!";
    const uint64_t block_size = rpc_max_size - overhead;
    uint64_t block_count = 0;
    const uint64_t num_blocks = nbytes / block_size;
    void* to_bytes;

    for (block_count = 0; block_count < num_blocks; block_count++) {
      remote_from->byte_offset = block_count * block_size;
      to_bytes = reinterpret_cast<void*>(
          (reinterpret_cast<uint8_t*>(local_to_bytes) + block_count * block_size));
      endpoint_->CopyFromRemote(remote_from, to_bytes, block_size);
    }

    const uint64_t remainder_bytes = nbytes % block_size;
    if (remainder_bytes != 0) {
      remote_from->byte_offset = block_count * block_size;
      to_bytes = reinterpret_cast<void*>(
          (reinterpret_cast<uint8_t*>(local_to_bytes) + block_count * block_size));
      endpoint_->CopyFromRemote(remote_from, to_bytes, remainder_bytes);
    }
  }

  void FreeHandle(void* handle) final { endpoint_->SysCallRemote(RPCCode::kFreeHandle, handle); }

  void SetDevice(Device dev) final { endpoint_->SysCallRemote(RPCCode::kDevSetDevice, dev); }

  void GetAttr(Device dev, DeviceAttrKind kind, ffi::Any* rv) final {
    if (dev.device_type == kDLCPU && kind == kExist) {
      // cpu always exists.
      *rv = 1;
    } else {
      *rv = endpoint_->SysCallRemote(RPCCode::kDevGetAttr, dev, static_cast<int>(kind));
    }
  }

  void* AllocDataSpace(Device dev, size_t nbytes, size_t alignment, DLDataType type_hint) final {
    return endpoint_->SysCallRemote(RPCCode::kDevAllocData, dev, nbytes, alignment, type_hint)
        .cast<void*>();
  }

  void* AllocDataSpace(Device dev, int ndim, const int64_t* shape, DLDataType dtype,
                       Optional<String> mem_scope) final {
    DLTensor temp;
    temp.data = nullptr;
    temp.device = dev;
    temp.ndim = ndim;
    temp.dtype = dtype;
    temp.shape = const_cast<int64_t*>(shape);
    temp.strides = nullptr;
    temp.byte_offset = 0;
    if (mem_scope.has_value()) {
      return endpoint_
          ->SysCallRemote(RPCCode::kDevAllocDataWithScope, &temp,
                          static_cast<std::string>(mem_scope.value()))
          .cast<void*>();
    } else {
      return endpoint_->SysCallRemote(RPCCode::kDevAllocDataWithScope, &temp, nullptr)
          .cast<void*>();
    }
  }

  void FreeDataSpace(Device dev, void* ptr) final {
    endpoint_->SysCallRemote(RPCCode::kDevFreeData, dev, ptr);
  }

  void CopyDataFromTo(DLTensor* from, DLTensor* to, TVMStreamHandle stream) final {
    endpoint_->SysCallRemote(RPCCode::kCopyAmongRemote, from, to, stream).cast<void*>();
  }

  TVMStreamHandle CreateStream(Device dev) final {
    return endpoint_->SysCallRemote(RPCCode::kDevCreateStream, dev).cast<void*>();
  }

  void FreeStream(Device dev, TVMStreamHandle stream) final {
    endpoint_->SysCallRemote(RPCCode::kDevFreeStream, dev, stream);
  }

  void StreamSync(Device dev, TVMStreamHandle stream) final {
    endpoint_->SysCallRemote(RPCCode::kDevStreamSync, dev, stream);
  }

  void SetStream(Device dev, TVMStreamHandle stream) final {
    endpoint_->SysCallRemote(RPCCode::kDevSetStream, dev, stream);
  }

  TVMStreamHandle GetCurrentStream(Device dev) final {
    return endpoint_->SysCallRemote(RPCCode::kDevGetCurrentStream, dev).cast<void*>();
  }

  DeviceAPI* GetDeviceAPI(Device dev, bool allow_missing) final { return this; }

  bool IsLocalSession() const final { return false; }

  void Shutdown() final { endpoint_->Shutdown(); }

 private:
  uint64_t GetRPCMaxTransferSize() {
    if (rpc_chunk_max_size_bytes_ > 0) {
      return (uint64_t)rpc_chunk_max_size_bytes_;
    }

    PackedFuncHandle rpc_func = GetFunction("tvm.rpc.server.GetCRTMaxPacketSize");
    if (rpc_func == nullptr) {
      rpc_chunk_max_size_bytes_ = (int64_t)kRPCMaxTransferSizeBytesDefault;
    } else {
      CallFunc(rpc_func, ffi::PackedArgs(nullptr, 0), [this](ffi::PackedArgs args) {
        // Use args[1] as return value, args[0] is tcode
        // Look at RPCWrappedFunc in src/runtime/rpc/rpc_module.cc
        rpc_chunk_max_size_bytes_ = args[1].cast<int64_t>();
        ICHECK_GT(rpc_chunk_max_size_bytes_, 0)
            << "RPC max transfer size is <= 0! (remote value = " << rpc_chunk_max_size_bytes_
            << ")";
      });
    }
    return (uint64_t)rpc_chunk_max_size_bytes_;
  }

  std::shared_ptr<RPCEndpoint> endpoint_;
  int64_t rpc_chunk_max_size_bytes_ = -1;
};

std::shared_ptr<RPCSession> CreateClientSession(std::shared_ptr<RPCEndpoint> endpoint) {
  return std::make_shared<RPCClientSession>(endpoint);
}

uint64_t RemoteCopyCalculatePacketOverheadSize(DLTensor* tensor, RPCCode code, uint64_t nbytes) {
  uint64_t shape_bytes = tensor->ndim * sizeof(int64_t);
  uint64_t to_data = reinterpret_cast<uint64_t>(static_cast<uint8_t*>(tensor->data));
  uint64_t overhead = sizeof(code) + sizeof(to_data) + sizeof(tensor->device) +
                      sizeof(tensor->ndim) + sizeof(tensor->dtype) + sizeof(tensor->byte_offset) +
                      shape_bytes + sizeof(nbytes);
  return overhead;
}

}  // namespace runtime
}  // namespace tvm
