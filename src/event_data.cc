//---------------------------------------------------------------------
//---------------------------------------------------------------------
#include <grpc_server.h>
#include <lv_message.h>
#include <logger.h>

//---------------------------------------------------------------------
//---------------------------------------------------------------------
using namespace google::protobuf::internal;

namespace grpc_labview
{  
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    CallData::CallData(LabVIEWgRPCServer* server, grpc::AsyncGenericService *service, grpc::ServerCompletionQueue *cq) :
        _server(server), 
        _service(service),
        _cq(cq),
        _stream(&_ctx),
        _status(CallStatus::Create),
        _writeSemaphore(0),
        _requestDataReady(false),
        _callStatus(grpc::Status::OK)
    {
        Proceed(true);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    std::shared_ptr<MessageMetadata> CallData::FindMetadata(const std::string& name)
    {
        std::string debug_message = "(event_data) - (CallData::FindMetadata)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        return _server->FindMetadata(name);    
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    CallFinishedData::CallFinishedData(CallData* callData)
    {
        _call = callData;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallFinishedData::Proceed(bool ok)
    {
        std::string debug_message = "(event_data) - (CallFinishedData::Proceed)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        delete this;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    bool CallData::Write()
    {
        std::string debug_message = "(event_data) - (CallData::Write)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (IsCancelled())
        {
            std::string debug_message = "(event_data) - (CallData::Write) - (IsCancelled1)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return false;
        }
        auto wb = _response->SerializeToByteBuffer();
        grpc::WriteOptions options;
        _status = CallStatus::Writing;
        _stream.Write(*wb, this);
        _writeSemaphore.wait();
        if (IsCancelled())
        {
            std::string debug_message = "(event_data) - (CallData::Write) - (IsCancelled2)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return false;
        }
        return true;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallData::SetCallStatusError(std::string errorMessage)
    {
        std::string debug_message = "(event_data) - (CallData::SetCallStatusError1)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        _callStatus = grpc::Status(grpc::StatusCode::INTERNAL, errorMessage);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallData::SetCallStatusError(grpc::StatusCode statusCode, std::string errorMessage)
    {
        std::string debug_message = "(event_data) - (CallData::SetCallStatusError2)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        _callStatus = grpc::Status(statusCode, errorMessage);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallData::Finish()
    {
        std::string debug_message = "(event_data) - (CallData::Finish)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (_status == CallStatus::PendingFinish)
        {
            std::string debug_message = "(event_data) - (CallData::Finish) - (_status == CallStatus::PendingFinish)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            _status = CallStatus::Finish;
            Proceed(false);
        }
        else
        {
            std::string debug_message = "(event_data) - (CallData::Finish) - (_status == else)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            _status = CallStatus::Finish;
            _stream.Finish(_callStatus, this);
        }
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    bool CallData::IsCancelled()
    {
        std::string debug_message = "(event_data) - (CallData::IsCancelled)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        return _ctx.IsCancelled();
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    bool CallData::IsActive()
    {
        std::string debug_message = "(event_data) - (CallData::IsActive)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        return !IsCancelled() && _status != CallStatus::Finish && _status != CallStatus::PendingFinish;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    bool CallData::ReadNext()
    {
        std::string debug_message = "(event_data) - (CallData::ReadNext)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (_requestDataReady)
        {
            std::string debug_message = "(event_data) - (CallData::ReadNext) - (_requestDataReady)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return true;
        }
        if (IsCancelled())
        {
            std::string debug_message = "(event_data) - (CallData::ReadNext) - (IsCancelled1)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return false;
        }
        auto tag = new ReadNextTag(this);
        _stream.Read(&_rb, tag);
        if (!tag->Wait())
        {
            std::string debug_message = "(event_data) - (CallData::ReadNext) - (!tag->Wait)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return false;
        }
        _request->ParseFromByteBuffer(_rb);
        _requestDataReady = true;
        if (IsCancelled())
        {
            std::string debug_message = "(event_data) - (CallData::ReadNext) - (IsCancelled2)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            return false;
        }
        return true;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallData::ReadComplete()
    {
        std::string debug_message = "(event_data) - (CallData::ReadComplete)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        _requestDataReady = false;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void CallData::Proceed(bool ok)
    {
        std::string debug_message = "(event_data) - (CallData::Proceed)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (!ok)
        {
            if (_status == CallStatus::Writing)
            {
                std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Writing)";
                grpc_labview::logger::LogDebug(debug_message.c_str());
                _writeSemaphore.notify();
            }
            if (_status != CallStatus::Finish)
            {
                std::string debug_message = "(event_data) - (CallData::Proceed) - (_status != CallStatus::Finish)";
                grpc_labview::logger::LogDebug(debug_message.c_str());
                _status = CallStatus::PendingFinish;
            }
        }
        if (_status == CallStatus::Create)
        {
            std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Create)";
            grpc_labview::logger::LogDebug(debug_message.c_str());
            // As part of the initial CREATE state, we *request* that the system
            // start processing SayHello requests. In this request, "this" acts are
            // the tag uniquely identifying the request (so that different CallData
            // instances can serve different requests concurrently), in this case
            // the memory address of this CallData instance.
            _service->RequestCall(&_ctx, &_stream, _cq, _cq, this);
            _ctx.AsyncNotifyWhenDone(new CallFinishedData(this));
            _status = CallStatus::Read;
        }
        else if (_status == CallStatus::Read)
        {
            /*std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Read)";
            grpc_labview::logger::LogDebug(debug_message.c_str());*/

            // Spawn a new CallData instance to serve new clients while we process
            // the one for this CallData. The instance will deallocate itself as
            // part of its FINISH state.
            new CallData(_server, _service, _cq);

            auto name = _ctx.method();

            std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Read) called for: " + name;
            grpc_labview::logger::LogDebug(debug_message.c_str());

            if (_server->HasRegisteredServerMethod(name) || _server->HasGenericMethodEvent())
            {
                _stream.Read(&_rb, this);
                _status = CallStatus::Process;
            }
            else
            {
                _status = CallStatus::Finish;
                _stream.Finish(grpc::Status(grpc::StatusCode::UNIMPLEMENTED, ""), this);
            }
        }
        else if (_status == CallStatus::Process)
        {
            auto name = _ctx.method();

            std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Process) called for: " + name;
            grpc_labview::logger::LogDebug(debug_message.c_str());

            LVEventData eventData;
            if (_server->FindEventData(name, eventData) || _server->HasGenericMethodEvent())
            {
                auto requestMetadata = _server->FindMetadata(eventData.requestMetadataName);
                auto responseMetadata = _server->FindMetadata(eventData.responseMetadataName);
                _request = std::make_shared<LVMessage>(requestMetadata);
                _response = std::make_shared<LVMessage>(responseMetadata);

                if (_request->ParseFromByteBuffer(_rb))
                {
                    std::string debug_message = "(event_data) - (CallData::Proceed) - (_status == CallStatus::Read) - (_request->ParseFromByteBuffer)";
                    grpc_labview::logger::LogDebug(debug_message.c_str());
                    _requestDataReady = true;
                    _methodData = std::make_shared<GenericMethodData>(this, &_ctx, _request, _response);
                    gPointerManager.RegisterPointer(_methodData);
                    _server->SendEvent(name, static_cast<gRPCid*>(_methodData.get()));
                }
                else
                {
                    _status = CallStatus::Finish;
                    _stream.Finish(grpc::Status(grpc::StatusCode::UNAVAILABLE, ""), this);
                }
            }
            else
            {
                _status = CallStatus::Finish;
                _stream.Finish(grpc::Status(grpc::StatusCode::UNIMPLEMENTED, ""), this);
            }
        }
        else if (_status == CallStatus::Writing)
        {
            _writeSemaphore.notify();
        }
        else if (_status == CallStatus::PendingFinish)
        {        
        }
        else
        {
            assert(_status == CallStatus::Finish);
            delete this;
        }
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    ReadNextTag::ReadNextTag(CallData* callData) :
        _readCompleteSemaphore(0),
        _success(false)
    {
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void ReadNextTag::Proceed(bool ok)
    {
        std::string debug_message = "(event_data) - (ReadNextTag::Proceed)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        _success = ok;
        _readCompleteSemaphore.notify();
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    bool ReadNextTag::Wait()
    {
        std::string debug_message = "(event_data) - (ReadNextTag::Wait)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        _readCompleteSemaphore.wait();
        return _success;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    EventData::EventData(ServerContext *_context) :
        _completed(false)
    {
        context = _context;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void EventData::WaitForComplete()
    {
        std::string debug_message = "(event_data) - (EventData::WaitForComplete)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        std::unique_lock<std::mutex> lck(lockMutex);
        while (!_completed) lock.wait(lck);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void EventData::NotifyComplete()
    {
        std::string debug_message = "(event_data) - (EventData::NotifyComplete)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        std::unique_lock<std::mutex> lck(lockMutex);
        _completed = true;
        lock.notify_all();
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    GenericMethodData::GenericMethodData(CallData* call, ServerContext *context, std::shared_ptr<LVMessage> request, std::shared_ptr<LVMessage> response)
        : EventData(context)
    {
        _call = call;
        _request = request;
        _response = response;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    std::shared_ptr<MessageMetadata> GenericMethodData::FindMetadata(const std::string& name)
    {
        std::string debug_message = "(event_data) - (GenericMethodData::FindMetadata)";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (_call != nullptr)
        {
            return _call->FindMetadata(name);
        }
        return nullptr;
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    ServerStartEventData::ServerStartEventData()
        : EventData(nullptr)
    {
        serverStartStatus = 0;
    }
}