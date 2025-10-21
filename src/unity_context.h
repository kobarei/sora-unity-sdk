#ifndef SORA_UNITY_SDK_UNITY_CONTEXT_H_INCLUDED
#define SORA_UNITY_SDK_UNITY_CONTEXT_H_INCLUDED

#include <mutex>

// webrtc
#include "rtc_base/log_sinks.h"

#include "unity/IUnityGraphics.h"
#include "unity/IUnityInterface.h"

#ifdef SORA_UNITY_SDK_WINDOWS
#include "unity/IUnityGraphicsD3D11.h"
#include "unity/IUnityGraphicsD3D12.h"
#endif

namespace sora_unity_sdk {

class UnityContext {
  std::mutex mutex_;
  std::unique_ptr<webrtc::FileRotatingLogSink> log_sink_;
  IUnityInterfaces* ifs_ = nullptr;
  IUnityGraphics* graphics_ = nullptr;

 private:
  // Unity のプラグインイベント
  static void UNITY_INTERFACE_API
  OnGraphicsDeviceEventStatic(UnityGfxDeviceEventType eventType);

  void UNITY_INTERFACE_API
  OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

 public:
  static UnityContext& Instance();

  bool IsInitialized();
  void Init(IUnityInterfaces* ifs);
  void Shutdown();

  IUnityInterfaces* GetInterfaces();

#ifdef SORA_UNITY_SDK_WINDOWS
 private:
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;
  ID3D12Device* device_d3d12_ = nullptr;
  ID3D12CommandQueue* command_queue_d3d12_ = nullptr;
  UnityGfxRenderer renderer_type_ = kUnityGfxRendererNull;

 public:
  ID3D11Device* GetDevice();
  ID3D11DeviceContext* GetDeviceContext();
  ID3D12Device* GetDeviceD3D12();
  ID3D12CommandQueue* GetCommandQueueD3D12();
  UnityGfxRenderer GetRendererType();
#endif
};

}  // namespace sora_unity_sdk

#endif
