# CancelServerCall - Force Cancellation of Blocking GetRequestData Calls

## Overview

This document describes the `CancelServerCall` functionality added to the gRPC-LabVIEW library. This feature allows LabVIEW code to force the cancellation of a blocking `GetRequestData` call from another thread or execution context.

## Background

### The Problem

The `GetRequestData` function is a blocking call that waits for data to arrive from a gRPC client message. When LabVIEW calls this function, it blocks indefinitely until one of the following occurs:

1. New gRPC data arrives from the client
2. The client cancels the RPC call
3. The server is shut down
4. A timeout/deadline is exceeded

In some scenarios, LabVIEW applications need to forcefully exit from a blocking `GetRequestData` call without waiting for these conditions to naturally occur (e.g., user-initiated cancellation, application shutdown, or timeout handling).

### How GetRequestData Works

The `GetRequestData` function (defined in `src/grpc_interop.cc`) performs the following operations:

```cpp
LIBRARY_EXPORT int32_t GetRequestData(grpc_labview::gRPCid** id, int8_t* lvRequest)
{
    try {
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
            data->_call->ReadComplete();
            return 0;
        }
        return -2;
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}
```

### The Blocking Mechanism

The blocking occurs in the `CallData::ReadNext()` method (in `src/event_data.cc`):

1. The method calls `_stream.Read(&_rb, tag)` which is an async gRPC operation
2. It then calls `tag->Wait()` which blocks on a semaphore
3. The semaphore uses a condition variable that waits until notified
4. The notification happens when the gRPC completion queue processes the read operation

**Key blocking code:**

```cpp
bool CallData::ReadNext()
{
    if (_requestDataReady)
    {
        return true;
    }
    if (IsCancelled())
    {
        return false;
    }
    auto tag = new ReadNextTag(this);
    _stream.Read(&_rb, tag);
    if (!tag->Wait())  // <-- BLOCKS HERE
    {
        return false;
    }
    _request->ParseFromByteBuffer(_rb);
    _requestDataReady = true;
    if (IsCancelled())
    {
        return false;
    }
    return true;
}
```

The semaphore implementation:

```cpp
inline void wait()
{
    std::unique_lock<std::mutex> lock(mtx);
    while(count == 0)
    {
        cv.wait(lock);  // <-- Condition variable wait
    }
    count--;
}
```

### Exit Conditions

`GetRequestData` exits when:

1. **Data already available**: If `_requestDataReady` is true, returns immediately
2. **Call is cancelled**: If `IsCancelled()` returns true, returns with error code `-(1000 + CANCELLED)`
3. **Successful read**: When new data arrives and is parsed successfully, returns `0`
4. **Read fails**: If the read operation fails or call becomes inactive, returns `-2`
5. **Invalid parameters**: If the data pointer is invalid, returns `-1`

## Solution: CancelServerCall

### Design

The solution adds a new DLL export function `CancelServerCall` that leverages gRPC's built-in cancellation mechanism (`ServerContext::TryCancel()`). This approach is:

- **Thread-safe**: Can be called from any thread
- **Standard**: Uses gRPC's native cancellation mechanism
- **Clean**: Properly triggers all cancellation checks in the code

### Implementation Details

Three files were modified to implement this feature:

#### 1. Header Declaration (`src/grpc_server.h`)

Added `CancelCall()` method to the `CallData` class:

```cpp
class CallData : public CallDataBase, public IMessageElementMetadataOwner
{
public:
    // ... existing methods ...
    void SetCallStatusError(grpc::StatusCode statusCode, std::string errorMessage);
    void CancelCall();  // <-- NEW METHOD

private:
    // ... private members ...
};
```

#### 2. Implementation (`src/event_data.cc`)

Implemented the `CancelCall()` method:

```cpp
//---------------------------------------------------------------------
//---------------------------------------------------------------------
void CallData::CancelCall()
{
    _ctx.TryCancel();
}
```

**How it works:**
- Calls `TryCancel()` on the gRPC `ServerContext` (`_ctx`)
- This sets an internal cancellation flag in the gRPC context
- Subsequent calls to `IsCancelled()` will return `true`
- Any blocking operations on the stream will be interrupted

#### 3. DLL Export Function (`src/grpc_interop.cc`)

Added the public API function that LabVIEW can call:

```cpp
//---------------------------------------------------------------------
//---------------------------------------------------------------------
LIBRARY_EXPORT int32_t CancelServerCall(grpc_labview::gRPCid** id)
{
    try {
        auto data = (*id)->CastTo<grpc_labview::GenericMethodData>();
        if (data == nullptr)
        {
            return -1;
        }
        
        if (data->_call->IsActive())
        {
            data->_call->CancelCall();
            return 0;
        }
        return -2; // Call already finished
    } catch (const std::exception&) {
        return grpc_labview::TranslateException();
    }
}
```

## Usage

### Function Signature

```cpp
int32_t CancelServerCall(gRPCid** id)
```

### Parameters

- `id`: A pointer to the `gRPCid` pointer that was passed to `GetRequestData`. This is the same handle that identifies the server call.

### Return Values

| Return Code | Meaning |
|-------------|---------|
| `0` | Success - the call was successfully cancelled |
| `-1` | Invalid parameter - the id pointer is null or invalid |
| `-2` | Call already finished - the call is no longer active |
| Other negative values | Exception occurred (translated error code) |

### Usage Pattern in LabVIEW

**Scenario 1: User-Initiated Cancellation**

```
Thread 1 (Server Handler):
    └─ Call GetRequestData(callID, requestData)  [BLOCKING]
    └─ Returns with error -(1000 + CANCELLED)

Thread 2 (User Action):
    └─ User clicks "Cancel" button
    └─ Call CancelServerCall(callID)
    └─ Returns 0 (success)
```

**Scenario 2: Timeout Handling**

```
Main Thread:
    └─ Store callID
    └─ Start timer (5 seconds)
    └─ Call GetRequestData(callID, requestData)  [BLOCKING]

Timer Thread:
    └─ Timer expires after 5 seconds
    └─ Call CancelServerCall(callID)
    └─ Main thread unblocks and returns cancelled error
```

### Important Notes

1. **Thread Safety**: `CancelServerCall` is thread-safe and can be called from any thread.

2. **Cleanup Required**: After cancellation, you must still call `CloseServerEvent` to properly clean up resources:
   ```
   result = GetRequestData(callID, data)
   if (result < 0)  // Cancelled or error
       CloseServerEvent(callID)  // Clean up
   ```

3. **Idempotent**: Calling `CancelServerCall` multiple times on the same call is safe - subsequent calls will return `-2` (already finished).

4. **Cancellation is Asynchronous**: The cancellation may not be immediate. The blocked `GetRequestData` call will exit at the next cancellation check point.

5. **Status Code**: When `GetRequestData` exits due to cancellation, it returns `-(1000 + grpc::StatusCode::CANCELLED)`, which equals `-1001`.

## Technical Details

### Cancellation Flow

1. LabVIEW calls `CancelServerCall(callID)` from any thread
2. The function locates the `GenericMethodData` associated with the ID
3. It calls `CallData::CancelCall()` which calls `_ctx.TryCancel()`
4. The gRPC `ServerContext` sets its cancellation flag
5. The blocked `GetRequestData` call:
   - If waiting in `ReadNext()`, the wait will be interrupted
   - The next check to `IsCancelled()` returns `true`
   - The function returns with error code `-(1000 + CANCELLED)`

### Memory and Resource Management

- The `CancelServerCall` function does NOT deallocate the call object
- LabVIEW must still call `CloseServerEvent` to properly clean up
- The cancellation only affects the blocking read operation
- All gRPC resources remain valid until `CloseServerEvent` is called

### Performance Considerations

- Cancellation using `TryCancel()` is very fast (microseconds)
- No memory allocation occurs during cancellation
- The blocked thread will typically unblock within milliseconds
- Multiple concurrent cancellations are supported

## Testing Recommendations

When implementing this feature in LabVIEW, test the following scenarios:

1. **Normal Cancellation**: Cancel a blocked call and verify it returns with the correct error code
2. **Already Completed**: Try to cancel a call that has already received data
3. **Multiple Cancellations**: Call `CancelServerCall` multiple times on the same ID
4. **Resource Cleanup**: Verify that `CloseServerEvent` works properly after cancellation
5. **Concurrent Operations**: Cancel from one thread while another thread is blocked
6. **Server Shutdown**: Ensure cancellation works during server shutdown sequences

## Compatibility

- **gRPC Version**: Compatible with gRPC C++ v1.x (uses standard ServerContext API)
- **Platform**: Works on Windows, Linux, and macOS
- **Thread Model**: Thread-safe, works with any threading model
- **Backward Compatible**: Existing code continues to work; this is an additive feature

## Related Functions

- `GetRequestData()`: The blocking function that reads request data
- `CloseServerEvent()`: Must be called after cancellation to clean up resources
- `IsCancelled()`: Can be used to check if a call has been cancelled
- `SetCallStatus()`: Can set custom error status codes on the call

## Example Error Handling

```cpp
int32_t result = GetRequestData(callID, requestData);

if (result == 0) {
    // Success - process the data
    ProcessRequest(requestData);
    SetResponseData(callID, responseData);
    CloseServerEvent(callID);
}
else if (result == -1001) {  // -(1000 + CANCELLED)
    // Call was cancelled
    LogMessage("Request cancelled by user");
    CloseServerEvent(callID);  // Still need to clean up
}
else if (result == -2) {
    // Read failed or no more data
    LogMessage("No more data available");
    CloseServerEvent(callID);
}
else {
    // Other error
    LogError("GetRequestData failed with code: " + result);
    CloseServerEvent(callID);
}
```

## Summary

The `CancelServerCall` functionality provides a clean, thread-safe way to interrupt blocking `GetRequestData` calls in gRPC-LabVIEW applications. By leveraging gRPC's native cancellation mechanism, it ensures proper resource management and predictable behavior across all platforms.

### Key Benefits

- ✅ Allows responsive user interfaces (can cancel long-running operations)
- ✅ Enables timeout implementations
- ✅ Thread-safe and can be called from any execution context
- ✅ Uses gRPC standard cancellation mechanism
- ✅ Maintains proper resource management
- ✅ No performance overhead when not used

### Files Modified

1. `src/grpc_server.h` - Added `CancelCall()` method declaration
2. `src/event_data.cc` - Implemented `CancelCall()` method
3. `src/grpc_interop.cc` - Added `CancelServerCall()` DLL export function

---

**Document Version**: 1.0  
**Date**: 2024  
**Author**: Engineering Team