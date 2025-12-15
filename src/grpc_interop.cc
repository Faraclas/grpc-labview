//---------------------------------------------------------------------
//---------------------------------------------------------------------
#include <grpc_server.h>
#include <cluster_copier.h>
#include <lv_interop.h>
#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <assert.h>
#include <feature_toggles.h>
#include <logger.h>

namespace grpc_labview
{
    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void OccurServerEvent(LVUserEventRef event, gRPCid* data)
    {
        grpc_labview::logger::LogDebug("OccurServerEvent(event, data) called");
        /*auto eventName = (data)->CastTo<grpc_labview::GenericMethodData>();
        auto metadata = eventName->FindMetadata("fieldName");
        if (metadata)
        {
            std::string fieldName = metadata->fieldName;
            grpc_labview::logger::LogDebug(fieldName.c_str());
        }*/
        auto error = PostUserEvent(event, &data);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------
    void OccurServerEvent(LVUserEventRef event, gRPCid* data, std::string eventMethodName)
    {
        std::string debug_message = "OccurServerEvent(event, data, eventMethodName) called";
        grpc_labview::logger::LogDebug(debug_message.c_str());
        grpc_labview::logger::LogDebug("OccurServerEvent(event, data) called");
        /*auto eventName = (data)->CastTo<grpc_labview::GenericMethodData>();
        auto metadata = eventName->FindMetadata("fieldName");
        if (metadata)
        {
            std::string fieldName = metadata->fieldName;
            grpc_labview::logger::LogDebug(fieldName.c_str());
        }*/
        GeneralMethodEventData eventData;
        eventData.methodData = data;
        eventData.methodName = nullptr;

        SetLVAsciiString(&eventData.methodName, eventMethodName);

        auto error = PostUserEvent(event, &eventData);

        DSDisposeHandle(eventData.methodName);
    }

    //---------------------------------------------------------------------
    //---------------------------------------------------------------------

    std::vector<std::string> SplitString(std::string s, std::string delimiter)
    {
        grpc_labview::logger::LogDebug("SplitString called");
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos)
        {
            token = s.substr(pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back(token);
        }

        res.push_back(s.substr(pos_start));
        return res;
    }

    std::map<uint32_t, int32_t> CreateMapBetweenLVEnumAndProtoEnumvalues(std::string enumValues)
    {
        grpc_labview::logger::LogDebug("CreateMapBetweenLVEnumAndProtoEnumvalues called");
        std::map<uint32_t, int32_t> lvEnumToProtoEnum;
        int seqLVEnumIndex = 0;
        for (std::string keyValuePair : SplitString(enumValues, ";"))
        {
            auto keyValue = SplitString(keyValuePair, "=");
            assert(keyValue.size() == 2);

            int protoEnumNumeric = std::stoi(keyValue[1]);
            lvEnumToProtoEnum.insert(std::pair<uint32_t, int32_t>(seqLVEnumIndex, protoEnumNumeric));
            seqLVEnumIndex += 1;
        }
        return lvEnumToProtoEnum;
    }

    void MapInsertOrAssign(std::map<int32_t, std::list<uint32_t>>*protoEnumToLVEnum, int protoEnumNumeric, std::list<uint32_t> lvEnumNumericValues)
    {
        grpc_labview::logger::LogDebug("MapInsertOrAssign called");
        auto existingElement = protoEnumToLVEnum->find(protoEnumNumeric);
        if (existingElement != protoEnumToLVEnum->end())
        {
            protoEnumToLVEnum->erase(protoEnumNumeric);
            protoEnumToLVEnum->insert(std::pair<int32_t, std::list<uint32_t>>(protoEnumNumeric, lvEnumNumericValues));
        }
        else
            protoEnumToLVEnum->insert(std::pair<int32_t, std::list<uint32_t>>(protoEnumNumeric, lvEnumNumericValues));
    }

    std::map<int32_t, std::list<uint32_t>> CreateMapBetweenProtoEnumAndLVEnumvalues(std::string enumValues)
    {
        grpc_labview::logger::LogDebug("CreateMapBetweenProtoEnumAndLVEnumvalues called");
        std::map<int32_t, std::list<uint32_t>> protoEnumToLVEnum;
        int seqLVEnumIndex = 0;
        for (std::string keyValuePair : SplitString(enumValues, ";"))
        {
            auto keyValue = SplitString(keyValuePair, "=");
            int protoEnumNumeric = std::stoi(keyValue[1]);
            assert(keyValue.size() == 2);

            std::list<uint32_t> lvEnumNumericValues;
            auto existingElement = protoEnumToLVEnum.find(protoEnumNumeric);
            if (existingElement != protoEnumToLVEnum.end())
                lvEnumNumericValues = existingElement->second;

            lvEnumNumericValues.push_back(seqLVEnumIndex); // Add the new element

            MapInsertOrAssign(&protoEnumToLVEnum, protoEnumNumeric, lvEnumNumericValues);

            seqLVEnumIndex += 1;
        }
        return protoEnumToLVEnum;
    }

    std::shared_ptr<EnumMetadata> CreateEnumMetadata2(IMessageElementMetadataOwner* metadataOwner, LVEnumMetadata2* lvMetadata)
    {
        grpc_labview::logger::LogDebug("CreateEnumMetadata2 called");
        std::shared_ptr<EnumMetadata> enumMetadata(new EnumMetadata());

        enumMetadata->messageName = GetLVAsciiString(lvMetadata->messageName);
        enumMetadata->typeUrl = GetLVAsciiString(lvMetadata->typeUrl);
        enumMetadata->elements = GetLVAsciiString(lvMetadata->elements);
        enumMetadata->allowAlias = lvMetadata->allowAlias;

        // Create the map between LV enum and proto enum values
        enumMetadata->LVEnumToProtoEnum = CreateMapBetweenLVEnumAndProtoEnumvalues(enumMetadata->elements);
        enumMetadata->ProtoEnumToLVEnum = CreateMapBetweenProtoEnumAndLVEnumvalues(enumMetadata->elements);

        return enumMetadata;
    }
}

int32_t ServerCleanupProc(grpc_labview::gRPCid* serverId);

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t IsFeatureEnabled(const char* featureName, uint8_t* featureEnabled)
{
    try {
        std::string debug_message = std::string("IsFeatureEnabled called for feature: ") + (featureName ? featureName : "<null>");
        grpc_labview::logger::LogDebug(debug_message.c_str());
        *featureEnabled = grpc_labview::FeatureConfig::getInstance().IsFeatureEnabled(featureName);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t LVCreateServer(grpc_labview::gRPCid** id)
{
    try {
        grpc_labview::InitCallbacks();
        auto server = new grpc_labview::LabVIEWgRPCServer();
        grpc_labview::gPointerManager.RegisterPointer(server);
        *id = server;
        grpc_labview::RegisterCleanupProc(ServerCleanupProc, server);
        grpc_labview::logger::InitializeLoggerFromEnv();
        grpc_labview::logger::LogDebug("LVCreateServer called");

        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t LVStartServer(char* address, char* serverCertificatePath, char* serverKeyPath, grpc_labview::gRPCid** id)
{
    try {
		std::string desc = "LVStartServer called for address: ";
        std::string char_str = address;
        std::string debug_message = desc + char_str;
        grpc_labview::logger::LogDebug(debug_message.c_str());
        auto server = (*id)->CastTo<grpc_labview::LabVIEWgRPCServer>();
        if (server == nullptr)
        {
            return -1;
        }
        return server->Run(address, serverCertificatePath, serverKeyPath);
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t LVGetServerListeningPort(grpc_labview::gRPCid** id, int* listeningPort)
{
    try {
        grpc_labview::logger::LogDebug("LVGetServerListeningPort called");
        auto server = (*id)->CastTo<grpc_labview::LabVIEWgRPCServer>();
        if (server == nullptr)
        {
            return -1;
        }
        *listeningPort = server->ListeningPort();
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t LVStopServer(grpc_labview::gRPCid** id)
{
    try {
        grpc_labview::logger::LogDebug("LVStopServer called");
        auto server = (*id)->CastTo<grpc_labview::LabVIEWgRPCServer>();
        if (server == nullptr)
        {
            return -1;
        }
        server->StopServer();

        grpc_labview::DeregisterCleanupProc(ServerCleanupProc, *id);
        grpc_labview::gPointerManager.UnregisterPointer(server.get());
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

int32_t ServerCleanupProc(grpc_labview::gRPCid* serverId)
{
	grpc_labview::logger::LogDebug("ServerCleanupProc called");
    return LVStopServer(&serverId);
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t RegisterMessageMetadata(grpc_labview::gRPCid** id, grpc_labview::LVMessageMetadata* lvMetadata)
{
    try {
        grpc_labview::logger::LogDebug("RegisterMessageMetadata called");
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        auto metadata = std::make_shared<grpc_labview::MessageMetadata>(server.get(), lvMetadata);
        server->RegisterMetadata(metadata);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t RegisterMessageMetadata2(grpc_labview::gRPCid** id, grpc_labview::LVMessageMetadata2* lvMetadata)
{
    try {
        //lvMetadata->messageName
        grpc_labview::logger::LogDebug("RegisterMessageMetadata2 called");
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        auto metadata = std::make_shared<grpc_labview::MessageMetadata>(server.get(), lvMetadata);
        server->RegisterMetadata(metadata);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t RegisterEnumMetadata2(grpc_labview::gRPCid** id, grpc_labview::LVEnumMetadata2* lvMetadata)
{
    try {
        grpc_labview::logger::LogDebug("RegisterEnumMetadata2 called");
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        auto metadata = CreateEnumMetadata2(server.get(), lvMetadata);
        server->RegisterMetadata(metadata);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT uint32_t GetLVEnumValueFromProtoValue(grpc_labview::gRPCid** id, const char* enumName, int protoValue, uint32_t* lvEnumValue)
{
    try {
		std::string desc = "GetLVEnumValueFromProtoValue called for enum: ";
		std::string char_str(enumName);
        std::string debug_message = desc + char_str;
        grpc_labview::logger::LogDebug(debug_message.c_str());
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        auto metadata = (server.get())->FindEnumMetadata(std::string(enumName));
        *(uint32_t*)lvEnumValue = metadata.get()->GetLVEnumValueFromProtoValue(protoValue);

        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t GetProtoValueFromLVEnumValue(grpc_labview::gRPCid** id, const char* enumName, int lvEnumValue, int32_t* protoValue)
{
    try {
		std::string desc = "GetProtoValueFromLVEnumValue called for enum: ";
		std::string char_str(enumName);
        std::string debug_message = desc + char_str;
        grpc_labview::logger::LogDebug(debug_message.c_str());
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        auto metadata = (server.get())->FindEnumMetadata(std::string(enumName));
        *(int32_t*)protoValue = metadata.get()->GetProtoValueFromLVEnumValue(lvEnumValue);

        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t CompleteMetadataRegistration(grpc_labview::gRPCid** id)
{
    try {
        grpc_labview::logger::LogDebug("CompleteMetadataRegistration called");
        auto server = (*id)->CastTo<grpc_labview::MessageElementMetadataOwner>();
        if (server == nullptr)
        {
            return -1;
        }
        server->FinalizeMetadata();
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t RegisterServerEvent(grpc_labview::gRPCid** id, const char* name, grpc_labview::LVUserEventRef* item, const char* requestMessageName, const char* responseMessageName)
{
    try {
		std::string desc = "RegisterServerEvent called for event: ";
		std::string char_str = name;
		std::string debug_message = desc + char_str;
		grpc_labview::logger::LogDebug(debug_message.c_str());
        auto server = (*id)->CastTo<grpc_labview::LabVIEWgRPCServer>();
        if (server == nullptr)
        {
            return -1;
        }

        server->RegisterEvent(name, *item, requestMessageName, responseMessageName);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t RegisterGenericMethodServerEvent(grpc_labview::gRPCid** id, grpc_labview::LVUserEventRef* item)
{
    try {
        grpc_labview::logger::LogDebug("RegisterGenericMethodServerEvent called");
        auto server = (*id)->CastTo<grpc_labview::LabVIEWgRPCServer>();
        if (server == nullptr)
        {
            return -1;
        }

        server->RegisterGenericMethodEvent(*item);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t GetRequestData(grpc_labview::gRPCid** id, int8_t* lvRequest)
{
    try {
        grpc_labview::logger::LogDebug("GetRequestData called");
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }
        if (data->_call->IsCancelled())
        {
            return -(1000 + grpc::StatusCode::CANCELLED);
        }
        if (data->_call->IsActive() && data->_call->ReadNext())
        {
            try
            {
                grpc_labview::ClusterDataCopier::CopyToCluster(*data->_request, lvRequest);
            }
            catch (const std::exception&)
            {
                // Before returning, set the call to complete, otherwise the server hangs waiting for the call.
                data->_call->ReadComplete();
                throw;
            }
            /*auto metadata = data->FindMetadata("fieldName");
            if (metadata)
            {
                std::string fieldName = metadata->fieldName;
                grpc_labview::logger::LogDebug(fieldName.c_str());
            }*/
            data->_call->ReadComplete();
            return 0;
        }
        return -2;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t SetResponseData(grpc_labview::gRPCid** id, int8_t* lvRequest)
{
    try {
        grpc_labview::logger::LogDebug("SetResponseData called");
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }

        if (data->_call->IsCancelled())
        {
            return -(1000 + grpc::StatusCode::CANCELLED);
        }

        grpc_labview::ClusterDataCopier::CopyFromCluster(*data->_response, lvRequest);

        if (!data->_call->IsActive() || !data->_call->Write())
        {
            return -2;
        }
        /*auto metadata = data->FindMetadata("fieldName");
        if (metadata)
        {
            std::string fieldName = metadata->fieldName;
            grpc_labview::logger::LogDebug(fieldName.c_str());
        }*/
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t CloseServerEvent(grpc_labview::gRPCid** id)
{
    try {
        grpc_labview::logger::LogDebug("CloseServerEvent called");
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }

        if (data->_call->IsCancelled())
        {
            return -(1000 + grpc::StatusCode::CANCELLED);
        }
        /*auto metadata = data->FindMetadata("fieldName");
        if (metadata)
        {
            std::string fieldName = metadata->fieldName;
            grpc_labview::logger::LogDebug(fieldName.c_str());
        }*/
        data->NotifyComplete();
        data->_call->Finish();
        grpc_labview::gPointerManager.UnregisterPointer(*id);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t SetCallStatus(grpc_labview::gRPCid** id, int grpcErrorCode, const char* errorMessage)
{
    try {
        grpc_labview::logger::LogDebug("SetCallStatus called");
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }
        data->_call->SetCallStatusError((grpc::StatusCode)grpcErrorCode, errorMessage);
        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t IsCancelled(grpc_labview::gRPCid** id)
{
    try {
        grpc_labview::logger::LogDebug("IsCancelled called");
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }
        return data->_call->IsCancelled();
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}

//---------------------------------------------------------------------
// Allows for definition of the LVRT DLL path to be used for callback functions
// This function should be called prior to any other gRPC functions in this library
   //---------------------------------------------------------------------
LIBRARY_EXPORT int32_t SetLVRTModulePath(const char* modulePath)
{
    try {
		std::string desc = "SetLVRTModulePath called with path: ";
        std::string char_str(modulePath);
        std::string debug_message = desc + char_str;
        grpc_labview::logger::LogDebug(debug_message.c_str());
        if (modulePath == nullptr)
        {
            return -1;
        }

        grpc_labview::SetLVRTModulePath(modulePath);

        return 0;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}